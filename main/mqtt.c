/*
	This file is part of the OSVentilationIDF project.

	Copyright (C) 2023-2026 Peter Lourens
*/

#include "mqtt.h"

static const char *TAG = "MQTT";

// Global MQTT client + connection state
esp_mqtt_client_handle_t s_mqtt = NULL;

static MQTT_config_t s_last_cfg = {0};

// client started (start() called)
static bool s_mqtt_running = false;

// broker connection up (event based)
bool s_mqtt_connected = false;

// Semaphore used to signal a successful MQTT connection to waiting tasks
static SemaphoreHandle_t mqtt_connected_sem = NULL;

// Cached node id (derived from sm_actuals.hostname or fallback)
static char mqtt_node_id[64] = {0};

// Utility: create / cleanup connection semaphore
static void ensure_mqtt_connected_sem(void)
{
	if (mqtt_connected_sem == NULL)
	{
		mqtt_connected_sem = xSemaphoreCreateBinary();
		if (mqtt_connected_sem == NULL)
		{
			ESP_LOGW(TAG, "Failed to create mqtt_connected_sem");
		}
		else
		{
			ESP_LOGD(TAG, "mqtt_connected_sem created");
		}
	}
}

// Public helper: wait for MQTT connection (fast path + semaphore)
bool mqtt_wait_for_connection(TickType_t ticks_to_wait)
{
	// Fast path: already connected
	if (s_mqtt_connected)
		return true;

	if (mqtt_connected_sem == NULL)
		return false;

	return (xSemaphoreTake(mqtt_connected_sem, ticks_to_wait) == pdTRUE);
}

// Node id helper: prefer sm_actuals.hostname, then mqtt base topic, then MAC address, then final fallback
static void sanitize_node(char *dst, const char *src, size_t dst_len)
{
	if (!dst || !src || dst_len == 0)
		return;

	size_t i = 0;
	for (; *src && i + 1 < dst_len; ++src)
	{
		char c = *src;
		if (isalnum((unsigned char)c) || c == '-' || c == '_')
			dst[i++] = (char)tolower((unsigned char)c);
		else
			dst[i++] = '_';
	}
	dst[i] = '\0';
}

static void format_mac_compact(uint8_t mac[6], char *out, size_t out_len)
{
	if (!out || out_len < 13)
		return;
	snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X",
			 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char *mqtt_get_node_id(void)
{
	if (mqtt_node_id[0] != '\0')
		return mqtt_node_id;

	// 1) Try hostname from statemachine snapshot
	if (xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
	{
		/* hostname is a fixed-size array; check first char */
		if (sm_actuals.hostname[0] != '\0')
		{
			sanitize_node(mqtt_node_id, sm_actuals.hostname, sizeof(mqtt_node_id));
		}
		xSemaphoreGive(statemachine_mutex);
	}

	// 2) Fallback: normalized MQTT base topic
	if (mqtt_node_id[0] == '\0')
	{
		MQTT_config_t cfg;
		xSemaphoreTake(mqtt_config_mutex, portMAX_DELAY);
		memcpy(&cfg, &mqtt_config, sizeof(cfg));
		xSemaphoreGive(mqtt_config_mutex);

		char base[128] = {0};
		normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));
		if (base[0] != '\0')
		{
			for (char *p = base; *p; ++p)
				if (*p == '/')
					*p = '_';
			sanitize_node(mqtt_node_id, base, sizeof(mqtt_node_id));
		}
	}

	// 3) Fallback: MAC address
	if (mqtt_node_id[0] == '\0')
	{
		uint8_t mac[6];
		if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
		{
			char macid[16] = {0};
			format_mac_compact(mac, macid, sizeof(macid));
			snprintf(mqtt_node_id, sizeof(mqtt_node_id), "osvent_%s", macid);
		}
	}

	// 4) Final fallback
	if (mqtt_node_id[0] == '\0')
	{
		strncpy(mqtt_node_id, "osventilation", sizeof(mqtt_node_id) - 1);
		mqtt_node_id[sizeof(mqtt_node_id) - 1] = '\0';
	}

	return mqtt_node_id;
}

// Mutex-protected config snapshot
static void mqtt_get_config_snapshot(MQTT_config_t *out)
{
	if (!out)
		return;

	xSemaphoreTake(mqtt_config_mutex, portMAX_DELAY);
	memcpy(out, &mqtt_config, sizeof(MQTT_config_t));
	xSemaphoreGive(mqtt_config_mutex);
}

// Set manual high speed mode (mutex-protected write to sm_config)
static void statemachine_set_speed(const char *payload, size_t len)
{
	// Normalize payload to "on"/"off"
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

// Check if MQTT is enabled helper
static inline bool mqtt_is_enabled(const MQTT_config_t *cfg)
{
	return strcmp(cfg->enable_mqtt, "On") == 0;
}

// Subscribe to topic helpers
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

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	esp_mqtt_event_handle_t event = event_data;

	switch (event_id)
	{
	case MQTT_EVENT_CONNECTED:
		s_mqtt_connected = true;
		LOGI(TAG, "MQTT connected");

		// Signal any waiters that the client is connected
		if (mqtt_connected_sem)
		{
			xSemaphoreGive(mqtt_connected_sem);
		}

		// Publish Home Assistant discovery configs (retained)
		ESP_LOGI("MQTT", "Waiting for sensors to become ready...");

		while (!sensors_ready())
		{
			vTaskDelay(pdMS_TO_TICKS(1000)); // check every second
		}

		ESP_LOGI("MQTT", "Sensors ready — publishing Home Assistant discovery");
		mqtt_publish_homeassistant_discovery();

		// Subscribe to control topic
		mqtt_subscribe("zigbee2mqtt/Ventilation/action", 1);
		break;

	case MQTT_EVENT_DISCONNECTED:
		s_mqtt_connected = false;
		LOGW(TAG, "MQTT disconnected");

		// Drain the semaphore so waiters will block until next connect
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

	// CASE 1: MQTT should be OFF
	if (!should_run)
	{
		if (s_mqtt_running)
		{
			LOGD(TAG, "MQTT disabled → stopping client");
			esp_mqtt_client_stop(s_mqtt);
			s_mqtt_running = false;
			s_mqtt_connected = false;
		}

		// Update last config snapshot
		s_last_cfg = cfg;
		return;
	}

	// CASE 2: MQTT should be ON but not running → start it
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

	// CASE 3: MQTT is running → detect config changes
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

// Start MQTT client
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

	// Ensure semaphore exists before starting client to avoid race
	ensure_mqtt_connected_sem();

	esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

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

// Helpers for publishing float, int, string values to MQTT with error checking
static inline void mqtt_pubf(const char *topic, float value)
{
	char payload[32];
	snprintf(payload, sizeof(payload), "%.2f", value);
	esp_mqtt_client_publish(s_mqtt, topic, payload, 0, 1, 0);
}

// Publish string value to MQTT
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

// Publish integer value to MQTT
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

	// Copy into output while collapsing multiple slashes and removing trailing slashes
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

// Publish all sensor values (raw and avg) for all valid sensors; topic format: {base}/sensors/bus{N}/slot{M}/{raw|avg}/{field})
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

			/* RAW */
			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "temperature");
			mqtt_pubf(topic, raw[bus][slot][IDX_TEMP]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "humidity");
			mqtt_pubf(topic, raw[bus][slot][IDX_HUMIDITY]);

			/* CO2 */
			if (raw[bus][slot][IDX_CO2] > 0)
			{
				mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "co2");
				mqtt_pubf(topic, raw[bus][slot][IDX_CO2]);
			}

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "pressure");
			mqtt_pubf(topic, raw[bus][slot][IDX_PRESSURE]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "raw", "gas_resistance");
			mqtt_pubf(topic, raw[bus][slot][IDX_GAS_RES]);

			/* AVG */
			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "temperature");
			mqtt_pubf(topic, avg[bus][slot][IDX_TEMP]);

			mqtt_topic(topic, sizeof(topic), base, bus, slot, "avg", "humidity");
			mqtt_pubf(topic, avg[bus][slot][IDX_HUMIDITY]);

			/* CO2 */
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

// Publish one valve position (reads NVS key "valveN")
void mqtt_publish_valve_positions(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt || !s_mqtt_connected)
	{
		LOGW(TAG, "MQTT not connected; skipping valve publish");
		return;
	}

	char base[256] = {0};
	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	// Open NVS namespace for valve positions
	nvs_handle_t nvs;
	esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);
	if (err != ESP_OK)
	{
		LOGE(TAG, "Failed to open NVS for valve positions: %s", esp_err_to_name(err));
		return;
	}

	char topic[400];

	for (int id = 0; id < MAX_VALVE_COUNT; id++)
	{
		int32_t pos = 0;
		char key[16];
		snprintf(key, sizeof(key), "valve%d", id);

		err = nvs_get_i32(nvs, key, &pos);
		if (err != ESP_OK)
		{
			LOGW(TAG, "Valve %d position not found in NVS (err=%s), defaulting to 0",
				 id, esp_err_to_name(err));
			pos = 0;
		}

		// Publish numeric position
		snprintf(topic, sizeof(topic),
				 "%s/valve%d/position", base, id);
		mqtt_pubi(topic, (int)pos);

		// Publish availability
		snprintf(topic, sizeof(topic),
				 "%s/valve%d/availability", base, id);
		mqtt_pubs(topic, "online");

		LOGD(TAG, "Published valve %d position: %ld", id, pos);
	}

	nvs_close(nvs);
}

// Publish system state and fan speed (mutex-protected read from sm_actuals)
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

// Home Assistant discovery helpers and publisher
bool sensors_ready(void)
{
	float snapshot[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];

	if (xQueuePeek(sensor_queue, snapshot, 0) != pdTRUE)
		return false;

	for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
	{
		for (int slot = 0; slot < CHANNELS_PER_BUS; slot++)
		{
			if (configured_sensor(bus, slot) &&
				snapshot[bus][slot][IDX_VALID] == 0.0f)
			{
				return false; // at least one configured sensor not ready
			}
		}
	}

	return true;
}

// Retained publish helper
static inline void mqtt_pubs_retained(const char *topic, const char *value)
{
	if (!s_mqtt || !s_mqtt_connected)
	{
		ESP_LOGW(TAG, "mqtt_pubs_retained: MQTT not connected; skipping %s", topic);
		return;
	}
	if (!value)
	{
		ESP_LOGW(TAG, "mqtt_pubs_retained: NULL value for topic %s", topic);
		return;
	}
	esp_err_t err = esp_mqtt_client_publish(s_mqtt, topic, value, 0, 0, 1);
	if (err < 0)
	{
		ESP_LOGW(TAG, "mqtt_pubs_retained: publish failed for topic %s", topic);
	}
}

// Publish discovery config for a sensor entity
static void mqtt_publish_ha_sensor(const char *node_id, const char *object_id, const char *name, const char *unit, const char *device_class, const char *state_topic, const char *value_template, const char *unique_id, const char *icon)
{
	char payload[400] = {0};
	char topic[200] = {0};

	snprintf(topic, sizeof(topic),
			 "homeassistant/sensor/%s/%s/config",
			 node_id, object_id);

	snprintf(payload, sizeof(payload),
			 "{"
			 "\"name\":\"%s\","
			 "\"state_topic\":\"%s\","
			 "\"unique_id\":\"%s\","
			 "\"value_template\":\"%s\",",
			 name, state_topic, unique_id,
			 value_template ? value_template : "{{ value }}");

	if (unit)
	{
		strlcat(payload, "\"unit_of_measurement\":\"", sizeof(payload));
		strlcat(payload, unit, sizeof(payload));
		strlcat(payload, "\",", sizeof(payload));
	}

	if (device_class)
	{
		strlcat(payload, "\"device_class\":\"", sizeof(payload));
		strlcat(payload, device_class, sizeof(payload));
		strlcat(payload, "\",", sizeof(payload));
	}

	if (icon)
	{
		strlcat(payload, "\"icon\":\"", sizeof(payload));
		strlcat(payload, icon, sizeof(payload));
		strlcat(payload, "\",", sizeof(payload));
	}

	// Device block (groups all entities)
	strlcat(payload,
			"\"device\":{"
			"\"identifiers\":[\"",
			sizeof(payload));
	strlcat(payload, node_id, sizeof(payload));
	strlcat(payload,
			"\"],"
			"\"name\":\"",
			sizeof(payload));
	strlcat(payload, node_id, sizeof(payload));
	strlcat(payload,
			"\",\"model\":\"OSVentilationIDF\","
			"\"manufacturer\":\"Peter Lourens\""
			"}"
			"}",
			sizeof(payload));

	mqtt_pubs_retained(topic, payload);
}

static void mqtt_publish_ha_valve_sensor(const char *node_id, const char *object_id, const char *name, const char *state_topic, const char *availability_topic, const char *unique_id)
{
	char payload[350];
	char topic[200];

	snprintf(topic, sizeof(topic),
			 "homeassistant/sensor/%s/%s/config",
			 node_id, object_id);

	snprintf(payload, sizeof(payload),
			 "{"
			 "\"name\":\"%s\","
			 "\"unique_id\":\"%s\","
			 "\"state_topic\":\"%s\","
			 "\"availability_topic\":\"%s\","
			 "\"icon\":\"mdi:valve\","
			 "\"device\":{"
			 "\"identifiers\":[\"%s\"],"
			 "\"name\":\"%s\","
			 "\"model\":\"OSVentilationIDF\","
			 "\"manufacturer\":\"Peter Lourens\""
			 "}"
			 "}",
			 name, unique_id,
			 state_topic, availability_topic,
			 node_id, node_id);

	mqtt_pubs_retained(topic, payload);
}

// Main discovery publisher — publishes retained discovery messages for HA
void mqtt_publish_homeassistant_discovery(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg) || !s_mqtt || !s_mqtt_connected)
		return;

	const char *node_id = mqtt_get_node_id();

	char base[120] = {0};
	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	float raw[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
	// float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

	bool have_raw = xQueuePeek(sensor_queue, &raw, 0) == pdTRUE;
	// bool have_avg = xQueuePeek(sensor_avg_queue, &avg, 0) == pdTRUE;

	for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
	{
		for (int slot = 0; slot < CHANNELS_PER_BUS; slot++)
		{
			if (!have_raw || raw[bus][slot][IDX_VALID] != 1)
				continue;

			// ---------------- TEMPERATURE ----------------
			{
				char state_topic[200];
				snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/temperature", base, bus, slot);

				char object_id[60];
				snprintf(object_id, sizeof(object_id), "bus%d_slot%d_temperature", bus, slot);

				char unique_id[120];
				snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

				char name[60];
				snprintf(name, sizeof(name), "Bus%d Slot%d Temperature", bus, slot);

				mqtt_publish_ha_sensor(node_id, object_id, name, "°C", "temperature", state_topic, "{{ value }}", unique_id, "mdi:thermometer");
			}

			// ---------------- HUMIDITY ----------------
			{
				char state_topic[200];
				snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/humidity", base, bus, slot);

				char object_id[60];
				snprintf(object_id, sizeof(object_id), "bus%d_slot%d_humidity", bus, slot);

				char unique_id[120];
				snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

				char name[60];
				snprintf(name, sizeof(name), "Bus%d Slot%d Humidity", bus, slot);

				mqtt_publish_ha_sensor(node_id, object_id, name, "%", "humidity", state_topic, "{{ value }}", unique_id, "mdi:water-percent");
			}

			// ---------------- CO2 (ONLY SCD41) ----------------
			if (sensor_supports_co2(bus, slot) &&
				raw[bus][slot][IDX_CO2] > 0)
			{
				char state_topic[150];
				snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/co2", base, bus, slot);

				char object_id[60];
				snprintf(object_id, sizeof(object_id), "bus%d_slot%d_co2", bus, slot);

				char unique_id[120];
				snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

				char name[60];
				snprintf(name, sizeof(name), "Bus%d Slot%d CO2", bus, slot);

				mqtt_publish_ha_sensor(node_id, object_id, name, "ppm", "carbon_dioxide", state_topic, "{{ value }}", unique_id, "mdi:molecule-co2");
			}
		}
	}

	// ---------------- VALVES ----------------
	for (int id = 0; id < MAX_VALVE_COUNT; id++)
	{
		char state_topic[150];
		snprintf(state_topic, sizeof(state_topic), "%s/valve%d/position", base, id);

		char availability_topic[150];
		snprintf(availability_topic, sizeof(availability_topic), "%s/valve%d/availability", base, id);

		char object_id[60];
		snprintf(object_id, sizeof(object_id), "valve_%d_position", id);

		char unique_id[120];
		snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

		char name[60];
		snprintf(name, sizeof(name), "Valve %d Position", id);

		mqtt_publish_ha_valve_sensor(node_id, object_id, name, state_topic, availability_topic, unique_id);
	}

	// ---------------- SYSTEM STATE ----------------
	{
		char object_id[40] = "system_state";
		char unique_id[120];
		char state_topic[150];

		snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
		snprintf(state_topic, sizeof(state_topic), "%s/system/state", base);

		mqtt_publish_ha_sensor(node_id, object_id, "OSVentilation System State", NULL, NULL, state_topic, "{{ value }}", unique_id, "mdi:information-outline");
	}

	// ---------------- FAN SPEED ----------------
	{
		char object_id[40] = "system_fanspeed";
		char unique_id[120];
		snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);

		char state_topic[150];
		snprintf(state_topic, sizeof(state_topic), "%s/system/fanspeed", base);

		mqtt_publish_ha_sensor(node_id, object_id, "OSVentilation Fan Speed", NULL, NULL, state_topic, "{{ value }}", unique_id, "mdi:fan");
	}
}
