#include "mqtt.h"

static const char *TAG = "MQTT";

// Global MQTT client + connection state
esp_mqtt_client_handle_t s_mqtt = NULL;
bool s_mqtt_connected = false;
static MQTT_config_t s_last_cfg = {0};
static bool s_mqtt_running = false;

// ---------------------------------------------------------------------------
// Mutex-protected config snapshot
// ---------------------------------------------------------------------------
static void mqtt_get_config_snapshot(MQTT_config_t *out)
{
	xSemaphoreTake(mqtt_config_mutex, portMAX_DELAY);
	memcpy(out, &mqtt_config, sizeof(MQTT_config_t));
	xSemaphoreGive(mqtt_config_mutex);
}

static inline bool mqtt_is_enabled(const MQTT_config_t *cfg)
{
	return strcmp(cfg->enable_mqtt, "On") == 0;
}

// ---------------------------------------------------------------------------
// MQTT event handler
// ---------------------------------------------------------------------------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	switch (event_id)
	{
	case MQTT_EVENT_CONNECTED:
		s_mqtt_connected = true;
		ESP_LOGI(TAG, "MQTT connected");
		break;

	case MQTT_EVENT_DISCONNECTED:
		s_mqtt_connected = false;
		ESP_LOGW(TAG, "MQTT disconnected");
		break;

	default:
		break;
	}
}

void mqtt_apply_config_change(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	bool should_run = (strcmp(cfg.enable_mqtt, "On") == 0);

	// ---------------------------------------------------------
	// CASE 1: MQTT should be OFF
	// ---------------------------------------------------------
	if (!should_run)
	{
		if (s_mqtt_running)
		{
			ESP_LOGI(TAG, "MQTT disabled → stopping client");
			esp_mqtt_client_stop(s_mqtt);
			s_mqtt_running = false;
			s_mqtt_connected = false;
		}

		// Update last config snapshot
		s_last_cfg = cfg;
		return;
	}

	// ---------------------------------------------------------
	// CASE 2: MQTT should be ON but not running → start it
	// ---------------------------------------------------------
	if (!s_mqtt_running)
	{
		ESP_LOGI(TAG, "MQTT enabled → starting client");
		if (mqtt_start_client() == ESP_OK)
		{
			s_mqtt_running = true;
			s_last_cfg = cfg;
		}
		return;
	}

	// ---------------------------------------------------------
	// CASE 3: MQTT is running → detect config changes
	// ---------------------------------------------------------
	bool server_changed = strcmp(cfg.mqtt_server, s_last_cfg.mqtt_server) != 0;
	bool port_changed = strcmp(cfg.mqtt_port, s_last_cfg.mqtt_port) != 0;
	bool base_changed = strcmp(cfg.mqtt_base_topic, s_last_cfg.mqtt_base_topic) != 0;

	if (server_changed || port_changed || base_changed)
	{
		ESP_LOGW(TAG, "MQTT config changed → restarting client");

		esp_mqtt_client_stop(s_mqtt);
		s_mqtt_running = false;
		s_mqtt_connected = false;

		if (mqtt_start_client() == ESP_OK)
		{
			s_mqtt_running = true;
			ESP_LOGI(TAG, "MQTT restarted successfully");
		}
		else
		{
			ESP_LOGE(TAG, "MQTT restart failed");
		}

		s_last_cfg = cfg;
		return;
	}

	// No changes → nothing to do
}

// ---------------------------------------------------------------------------
// Start MQTT client
// ---------------------------------------------------------------------------
esp_err_t mqtt_start_client(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg))
	{
		ESP_LOGW(TAG, "MQTT disabled");
		return ESP_OK;
	}

	char uri[256];
	snprintf(uri, sizeof(uri), "mqtt://%s:%s", cfg.mqtt_server, cfg.mqtt_port);

	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = uri,
	};

	s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
	if (!s_mqtt)
		return ESP_FAIL;

	esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

	return esp_mqtt_client_start(s_mqtt);
}

// ---------------------------------------------------------------------------
// Helpers for publishing
// ---------------------------------------------------------------------------
static inline void mqtt_pubf(const char *topic, float value)
{
	char payload[32];
	snprintf(payload, sizeof(payload), "%.2f", value);
	esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 0);
}

static inline void mqtt_topic(char *out, size_t out_sz, const char *base, int bus, int slot, const char *type, const char *field)
{
	snprintf(out, out_sz, "%s/sensors/bus%d/slot%d/%s/%s", base, bus, slot, type, field);
}

void normalize_base_topic(const char *in, char *out, size_t out_size)
{
	if (!in || !out || out_size == 0)
		return;

	size_t len = strlen(in);

	// Skip leading slashes
	size_t start = 0;
	while (start < len && in[start] == '/')
		start++;

	// Copy into output
	size_t pos = 0;
	for (size_t i = start; i < len && pos < out_size - 1; i++)
	{
		// Collapse multiple slashes into one
		if (in[i] == '/')
		{
			// Skip repeated slashes
			while (i + 1 < len && in[i + 1] == '/')
				i++;
		}

		out[pos++] = in[i];
	}

	// Remove trailing slash
	while (pos > 0 && out[pos - 1] == '/')
		pos--;

	out[pos] = '\0';
}

// ---------------------------------------------------------------------------
// Publish all sensor values
// ---------------------------------------------------------------------------
void mqtt_publish_sensor_values(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt || !s_mqtt_connected)
	{
		ESP_LOGW(TAG, "MQTT not connected; skipping publish");
		return;
	}

	float raw[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
	float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

	if (xQueuePeek(sensor_queue, raw, 0) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to read raw sensor queue");
		return;
	}

	if (xQueuePeek(sensor_avg_queue, avg, 0) != pdTRUE)
	{
		ESP_LOGE(TAG, "Failed to read avg sensor queue");
		return;
	}

	char base[256] = {0};
	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	char topic[512];

	for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
	{
		for (int slot = 0; slot < CHANNELS_PER_BUS; slot++)
		{
			if (raw[bus][slot][IDX_VALID] != 1)
				continue;

			// RAW
			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "temperature");
			mqtt_pubf(topic, raw[bus][slot][IDX_TEMP]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "humidity");
			mqtt_pubf(topic, raw[bus][slot][IDX_HUMIDITY]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "co2");
			mqtt_pubf(topic, raw[bus][slot][IDX_CO2]);

			// AVG
			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "temperature");
			mqtt_pubf(topic, avg[bus][slot][IDX_TEMP]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "humidity");
			mqtt_pubf(topic, avg[bus][slot][IDX_HUMIDITY]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "co2");
			mqtt_pubf(topic, avg[bus][slot][IDX_CO2]);
		}
	}
}