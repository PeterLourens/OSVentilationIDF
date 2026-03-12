/*
	This file is part of the OSVentilationIDF project.

	Copyright (C) 2023-2026 Peter Lourens
*/

#include "mqtt.h"
#include "freertos/semphr.h"

static const char *TAG = "MQTT";

// Global MQTT client + connection state
esp_mqtt_client_handle_t s_mqtt = NULL;

static MQTT_config_t s_last_cfg = {0};
static bool s_mqtt_running = false; // client started (start() called)

bool s_mqtt_connected = false; // broker connection up (event based)

// Semaphore used to signal a successful MQTT connection to waiting tasks
static SemaphoreHandle_t mqtt_connected_sem = NULL;

// Public helper: wait for MQTT connection (returns true if connected within timeout)
bool mqtt_wait_for_connection(TickType_t ticks_to_wait)
{
	// Fast path: already connected
	if (s_mqtt_connected)
	{
		return true;
	}

	if (mqtt_connected_sem == NULL)
		return false;

	if (xSemaphoreTake(mqtt_connected_sem, ticks_to_wait) == pdTRUE)
		return true;

	return false;
}

// Internal helper to create the semaphore if needed
static void ensure_mqtt_connected_sem(void)
{
	if (mqtt_connected_sem == NULL)
	{
		mqtt_connected_sem = xSemaphoreCreateBinary();
		if (mqtt_connected_sem == NULL)
		{
			LOGW(TAG, "Failed to create mqtt_connected_sem (out of memory)");
		}
	}
}

// Internal helper to delete the semaphore
static void cleanup_mqtt_connected_sem(void)
{
	if (mqtt_connected_sem)
	{
		// drain then delete
		xSemaphoreTake(mqtt_connected_sem, 0);
		vSemaphoreDelete(mqtt_connected_sem);
		mqtt_connected_sem = NULL;
	}
}

// ---------------------------------------------------------------------------
// Mutex-protected config snapshot
// ---------------------------------------------------------------------------
static void mqtt_get_config_snapshot(MQTT_config_t *out)
{
	if (!out)
		return;

	xSemaphoreTake(mqtt_config_mutex, portMAX_DELAY);
	memcpy(out, &mqtt_config, sizeof(MQTT_config_t));
	xSemaphoreGive(mqtt_config_mutex);
}

// ---------------------------------------------------------------------------
// Set manual high speed mode (mutex-protected write to sm_config)
// ---------------------------------------------------------------------------
static void statemachine_set_speed(const char *payload, size_t len)
{
	// Normalize payload
	bool is_on = (len == 2 && strncasecmp(payload, "on", 2) == 0);
	bool is_off = (len == 3 && strncasecmp(payload, "off", 3) == 0);

	if (!is_on && !is_off)
	{
		LOGW("STATEMACHINE", "Unknown MQTT payload: %.*s", (int)len, payload);
		return;
	}

	if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) != pdTRUE)
	{
		LOGW("STATEMACHINE", "Could not lock mutex for MQTT manual control");
		return;
	}

	if (sm_actuals.valve_lock != 0)
	{
		LOGW(TAG, "MQTT command ignored: valve_locked=%d", sm_actuals.valve_lock);
		xSemaphoreGive(statemachine_mutex);
		return;
	}

	if (is_off)
	{
		sm_actuals.state = is_day() ? STATE_NR_DAY : STATE_NR_NIGHT;
		LOGI(TAG, "MQTT: MANUAL HIGH SPEED deactivated, returned to %s", is_day() ? "DAY" : "NIGHT");
	}
	else
	{
		sm_actuals.state = STATE_NR_MANUALHS;
		LOGI(TAG, "MQTT: MANUAL HIGH SPEED activated");
	}

	xSemaphoreGive(statemachine_mutex);
}

// ---------------------------------------------------------------------------
// Check if MQTT is enabled helper
// ---------------------------------------------------------------------------
static inline bool mqtt_is_enabled(const MQTT_config_t *cfg)
{
	return strcmp(cfg->enable_mqtt, "On") == 0;
}

// ---------------------------------------------------------------------------
// Subscribe to topic helper
// ---------------------------------------------------------------------------
void mqtt_subscribe(const char *topic, int qos)
{
	if (!s_mqtt)
	{
		LOGE("MQTT", "Client not initialized");
		return;
	}

	int msg_id = esp_mqtt_client_subscribe(s_mqtt, topic, qos);
	if (msg_id < 0)
	{
		LOGE("MQTT", "Failed to subscribe to %s", topic);
	}
	else
	{
		LOGI("MQTT", "Subscribed to %s (msg_id=%d, qos=%d)", topic, msg_id, qos);
	}
}

// ---------------------------------------------------------------------------
// MQTT event handler
// ---------------------------------------------------------------------------
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	esp_mqtt_event_handle_t event = event_data;

	switch (event_id)
	{
	case MQTT_EVENT_CONNECTED:
		s_mqtt_connected = true;
		LOGI(TAG, "MQTT connected");

		// Signal any waiters that the client is connected (task context)
		if (mqtt_connected_sem)
		{
			xSemaphoreGive(mqtt_connected_sem);
		}

		mqtt_subscribe("zigbee2mqtt/Ventilation/action", 1);
		break;

	case MQTT_EVENT_DISCONNECTED:
		s_mqtt_connected = false;
		LOGW(TAG, "MQTT disconnected");

		if (mqtt_connected_sem)
		{
			xSemaphoreTake(mqtt_connected_sem, 0);
		}
		break;

	case MQTT_EVENT_DATA:
	{
		LOGI(TAG, "MQTT message received");
		const char *topic = event->topic;
		const char *payload = event->data;
		int len = event->data_len;

		LOGD(TAG, "Topic (%d): %.*s", event->topic_len, event->topic_len, topic);
		LOGD(TAG, "Payload (%d): %.*s", len, len, payload);

		if (event->topic_len == (int)strlen("zigbee2mqtt/Ventilation/action") &&
			strncmp(topic, "zigbee2mqtt/Ventilation/action", event->topic_len) == 0)
		{
			statemachine_set_speed(payload, len);
		}

		break;
	}
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
			LOGD(TAG, "MQTT disabled → stopping client");
			esp_mqtt_client_stop(s_mqtt);
			s_mqtt_running = false;
			s_mqtt_connected = false;

			/* Clean up connection semaphore */
			cleanup_mqtt_connected_sem();
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
		LOGD(TAG, "MQTT enabled → starting client");
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
		LOGW(TAG, "MQTT config changed → restarting client");

		esp_mqtt_client_stop(s_mqtt);
		s_mqtt_running = false;
		s_mqtt_connected = false;

		if (mqtt_start_client() == ESP_OK)
		{
			s_mqtt_running = true;
			LOGI(TAG, "MQTT restarted successfully");
		}
		else
		{
			LOGE(TAG, "MQTT restart failed");
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
	if (s_mqtt_running)
	{
		// Already started
		return ESP_OK;
	}

	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	if (strcmp(cfg.enable_mqtt, "On") != 0)
	{
		LOGW(TAG, "MQTT start requested, but MQTT is disabled");
		return ESP_OK;
	}

	char uri[256];
	snprintf(uri, sizeof(uri), "mqtt://%s:%s", cfg.mqtt_server, cfg.mqtt_port);

	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = uri,
	};

	if (s_mqtt)
	{
		// Just in case – destroy any previous client
		esp_mqtt_client_destroy(s_mqtt);
		s_mqtt = NULL;
	}

	s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
	if (!s_mqtt)
	{
		LOGE(TAG, "Failed to init MQTT client");
		return ESP_FAIL;
	}

	esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

	/* Create connection semaphore if not present */
	ensure_mqtt_connected_sem();

	esp_err_t err = esp_mqtt_client_start(s_mqtt);
	if (err != ESP_OK)
	{
		LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
		esp_mqtt_client_destroy(s_mqtt);
		s_mqtt = NULL;
		return err;
	}

	s_mqtt_running = true;
	s_last_cfg = cfg; // snapshot config used for this client

	LOGI(TAG, "MQTT client started (uri=%s)", uri);
	return ESP_OK;
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

// ---------------------------------------------------------------------------
// Publish string value to MQTT
// ---------------------------------------------------------------------------
static inline void mqtt_pubs(const char *topic, const char *value)
{
	if (!s_mqtt || !s_mqtt_connected)
	{
		LOGW(TAG, "mqtt_pubs: MQTT not connected; skipping publish");
		return;
	}

	if (!value)
	{
		LOGW(TAG, "mqtt_pubs: NULL value for topic %s", topic);
		return;
	}

	esp_err_t err = esp_mqtt_client_publish(s_mqtt, topic, value, 0, 1, 0);
	if (err < 0)
	{
		LOGW(TAG, "mqtt_pubs: publish failed for topic %s", topic);
	}
}

// ---------------------------------------------------------------------------
// Publish integer value to MQTT
// ---------------------------------------------------------------------------
static inline void mqtt_pubi(const char *topic, int value)
{
	if (!s_mqtt || !s_mqtt_connected)
	{
		LOGW(TAG, "mqtt_pubi: MQTT not connected; skipping publish");
		return;
	}

	char payload[16];
	snprintf(payload, sizeof(payload), "%d", value);

	esp_err_t err = esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 0);
	if (err < 0)
	{
		LOGW(TAG, "mqtt_pubi: publish failed for topic %s", topic);
	}
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
		LOGW(TAG, "MQTT not connected; skipping publish");
		return;
	}

	float raw[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
	float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

	if (xQueuePeek(sensor_queue, raw, 0) != pdTRUE)
	{
		LOGE(TAG, "Failed to read raw sensor queue");
		return;
	}

	if (xQueuePeek(sensor_avg_queue, avg, 0) != pdTRUE)
	{
		LOGE(TAG, "Failed to read avg sensor queue");
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

			// CO2 is always higher than 0 and no need to publish if sensor does not supply this value
			if (raw[bus][slot][IDX_CO2] > 0)
			{
				mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "co2");
				mqtt_pubf(topic, raw[bus][slot][IDX_CO2]);
			}

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "pressure");
			mqtt_pubf(topic, raw[bus][slot][IDX_PRESSURE]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "gas_resistance");
			mqtt_pubf(topic, raw[bus][slot][IDX_GAS_RES]);

			// AVG
			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "temperature");
			mqtt_pubf(topic, avg[bus][slot][IDX_TEMP]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "humidity");
			mqtt_pubf(topic, avg[bus][slot][IDX_HUMIDITY]);

			// CO2 is always higher than 0 and no need to publish if sensor does not supply this value
			if (avg[bus][slot][IDX_CO2] > 0)
			{
				mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "co2");
				mqtt_pubf(topic, avg[bus][slot][IDX_CO2]);
			}

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "pressure");
			mqtt_pubf(topic, avg[bus][slot][IDX_PRESSURE]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "gas_resistance");
			mqtt_pubf(topic, avg[bus][slot][IDX_GAS_RES]);
		}
	}
}

// ---------------------------------------------------------------------------
// Publish all valve positions
// ---------------------------------------------------------------------------
void mqtt_publish_valve_positions(void)
{
	char base[256] = {0};
	char topic[512] = {0};

	MQTT_config_t cfg;

	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt || !s_mqtt_connected)
	{
		LOGW(TAG, "MQTT not connected; skipping valve publish");
		return;
	}

	// Open NVS namespace for valve positions
	nvs_handle_t nvs;
	esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);
	if (err != ESP_OK)
	{
		LOGE(TAG, "Failed to open NVS for valve positions: %s",
			 esp_err_to_name(err));
		return;
	}

	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	for (int id = 0; id < MAX_VALVE_COUNT; id++)
	{
		int32_t pos = 0;

		char key[16];
		snprintf(key, sizeof(key), "valve%d", id);

		esp_err_t r = nvs_get_i32(nvs, key, &pos);
		if (r == ESP_ERR_NVS_NOT_FOUND)
		{
			// Valve not configured → skip
			continue;
		}
		else if (r != ESP_OK)
		{
			LOGW(TAG, "Valve[%d]: NVS read failed (%s); assuming 0", id, esp_err_to_name(r));
			pos = 0;
		}

		// Build topic manually
		snprintf(topic, sizeof(topic), "%s/position/valve%d", base, id);
		mqtt_pubi(topic, pos);

		LOGD(TAG, "Publishing valve %d position: %ld", id, pos);
		LOGD(TAG, "Topic: %s", topic);
	}

	nvs_close(nvs);
}

// ---------------------------------------------------------------------------
// Publish system state and fan speed (mutex-protected read from sm_actuals)
// ---------------------------------------------------------------------------
void mqtt_publish_state_and_fanspeed(void)
{
	int state = 0;
	int fanspeed = 0;

	char base[256] = {0};
	char topic[512] = {0};
	char uptime_str[64] = {0};

	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	formatted_uptime(uptime_str, sizeof(uptime_str));

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt || !s_mqtt_connected)
	{
		LOGW(TAG, "MQTT not connected; skipping state/fanspeed publish");
		return;
	}

	if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
	{
		state = sm_actuals.state;
		fanspeed = sm_actuals.fanspeed;
		xSemaphoreGive(statemachine_mutex);
	}
	else
	{
		LOGW(TAG, "Failed to lock statemachine_mutex; skipping publish");
		return;
	}

	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	snprintf(topic, sizeof(topic), "%s/system/state", base);
	mqtt_pubs(topic, state_nr_to_str(state));

	snprintf(topic, sizeof(topic), "%s/system/fanspeed", base);
	mqtt_pubs(topic, fan_speed_to_str(fanspeed));

	snprintf(topic, sizeof(topic), "%s/system/uptime", base);
	mqtt_pubs(topic, uptime_str);
}
