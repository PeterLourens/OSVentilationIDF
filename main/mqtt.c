/*
	This file is part of the OSVentilationIDF project.

	Copyright (C) 2023-2026 Peter Lourens
*/

#include "mqtt.h"

static const char *TAG = "MQTT";

// Global MQTT client + connection state
esp_mqtt_client_handle_t s_mqtt = NULL;

static MQTT_config_t s_last_cfg = {0};
static bool s_mqtt_running = false; /// client started (start() called)

bool s_mqtt_connected = false; // broker connection up (event based)

// Semaphore used to signal a successful MQTT connection to waiting tasks
static SemaphoreHandle_t mqtt_connected_sem = NULL;

// Cached node id (derived from sm_actuals.hostname or fallback)
static char mqtt_node_id[64] = {0};

/* ---------------------------------------------------------------------------
   Utility: create / cleanup connection semaphore
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   Public helper: wait for MQTT connection (fast path + semaphore)
   --------------------------------------------------------------------------- */
bool mqtt_wait_for_connection(TickType_t ticks_to_wait)
{
	/* Fast path: already connected */
	if (s_mqtt_connected)
		return true;

	if (mqtt_connected_sem == NULL)
		return false;

	return (xSemaphoreTake(mqtt_connected_sem, ticks_to_wait) == pdTRUE);
}

/* ---------------------------------------------------------------------------
   Node id helper: prefer sm_actuals.hostname, then mqtt base topic, then MAC
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   Mutex-protected config snapshot
   --------------------------------------------------------------------------- */
static void mqtt_get_config_snapshot(MQTT_config_t *out)
{
	if (!out)
		return;

	xSemaphoreTake(mqtt_config_mutex, portMAX_DELAY);
	memcpy(out, &mqtt_config, sizeof(MQTT_config_t));
	xSemaphoreGive(mqtt_config_mutex);
}

/* ---------------------------------------------------------------------------
   Set manual high speed mode (mutex-protected write to sm_config)
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   Check if MQTT is enabled helper
   --------------------------------------------------------------------------- */
static inline bool mqtt_is_enabled(const MQTT_config_t *cfg)
{
	return strcmp(cfg->enable_mqtt, "On") == 0;
}

/* ---------------------------------------------------------------------------
   Subscribe to topic helper
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   MQTT event handler
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   Start MQTT client
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   Helpers for publishing
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   Publish all sensor values
   --------------------------------------------------------------------------- */
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

/* ---------------------------------------------------------------------------
   Publish all valve positions
   --------------------------------------------------------------------------- */

// Helper: read one valve pos from NVS and publish it as a plain integer
#include <inttypes.h> /* ensure this is included at top of mqtt.c */

/* Publish one valve position (reads NVS key "valveN") */
/*static void publish_valve_from_nvs(nvs_handle_t nvs, const char *base, int id)
{
	char key[16];
	int32_t raw_pos = 0;
	char pos_topic[256];
	char state_topic[256];
	char payload_pos[16];
	const char *state_str;
	esp_err_t r;

	if (!base)
		return;

	// Build NVS key "valveN"
snprintf(key, sizeof(key), "valve%d", id);
key[sizeof(key) - 1] = '\0';

// Read position from NVS
r = nvs_get_i32(nvs, key, &raw_pos);
if (r == ESP_ERR_NVS_NOT_FOUND)
{
	// Valve not configured → skip quietly
	return;
}
else if (r != ESP_OK)
{
	LOGW(TAG, "Valve[%d]: NVS read failed (%s); assuming 0", id, esp_err_to_name(r));
	raw_pos = 0;
}

// Optional scaling: if your stored range is different (e.g. 0..1023),
// uncomment and adjust the following line:
// if (raw_pos > 100)
// 	raw_pos = (raw_pos * 100) / 1023;


// Clamp to 0..100
if (raw_pos < 0)
	raw_pos = 0;
if (raw_pos > 100)
	raw_pos = 100;

// Build topics (position = numeric, state = textual OPEN/CLOSED)
snprintf(pos_topic, sizeof(pos_topic), "%s/position/valve%d", base, id);
pos_topic[sizeof(pos_topic) - 1] = '\0';

snprintf(state_topic, sizeof(state_topic), "%s/state/valve%d", base, id);
state_topic[sizeof(state_topic) - 1] = '\0';

// Prepare payloads
snprintf(payload_pos, sizeof(payload_pos), "%" PRId32, raw_pos);
payload_pos[sizeof(payload_pos) - 1] = '\0';

state_str = (raw_pos > 0) ? "OPEN" : "CLOSED";

// Ensure MQTT client connected before publishing
if (!s_mqtt || !s_mqtt_connected)
{
	LOGW(TAG, "MQTT not connected; skipping publish for valve %d", id);
	return;
}

// Debug log exact payloads and topics
LOGD(TAG, "Valve %d -> %s = \"%s\", %s = \"%s\"", id, pos_topic, payload_pos, state_topic, state_str);

// Publish numeric position (non-retained, QoS 1)
esp_err_t err = esp_mqtt_client_publish(s_mqtt, pos_topic, payload_pos, 0, 1, 0);
if (err < 0)
{
	LOGW(TAG, "Failed to publish valve %d position (err=%d)", id, err);
}

// Publish textual state (non-retained, QoS 1)
err = esp_mqtt_client_publish(s_mqtt, state_topic, state_str, 0, 1, 0);
if (err < 0)
{
	LOGW(TAG, "Failed to publish valve %d state (err=%d)", id, err);
}
}
*/

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

/* ---------------------------------------------------------------------------
   Publish system state and fan speed (mutex-protected read from sm_actuals)
   --------------------------------------------------------------------------- */
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

/*-------- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -
Home Assistant discovery helpers and publisher
-- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -- -*/

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
static void mqtt_publish_ha_sensor(const char *node_id, const char *object_id, const char *name, const char *unit, const char *device_class, const char *state_topic, const char *value_template, const char *unique_id)
{
	// safe discovery payload builder: node_id, optional mac, optional sw/hw versions
	char payload[512] = {0};
	char macid[32] = {0};
	uint8_t mac[6];

	// try to read MAC (non-fatal)
	if (esp_read_mac(mac, ESP_MAC_WIFI_STA) == ESP_OK)
	{
		snprintf(macid, sizeof(macid), "%02X%02X%02X%02X%02X%02X",
				 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
		macid[sizeof(macid) - 1] = '\0';
	}

	// Build base payload with device object; choose branch depending on mac presence
	if (macid[0] != '\0')
	{
		// identifiers: node_id and mac
		snprintf(payload, sizeof(payload),
				 "{"
				 "\"name\":\"%s\","
				 "\"state_topic\":\"%s\","
				 "\"unique_id\":\"%s\","
				 "\"unit_of_measurement\":\"%s\","
				 "\"device_class\":\"%s\","
				 "\"value_template\":\"%s\","
				 "\"device\":{"
				 "\"identifiers\":[\"%s\",\"%s\"],"
				 "\"name\":\"%s\","
				 "\"model\":\"%s\","
				 "\"manufacturer\":\"%s\"",
				 name, state_topic, unique_id,
				 unit ? unit : "",
				 device_class ? device_class : "",
				 value_template ? value_template : "{{ value }}",
				 node_id, macid, node_id, DEVICE_MODEL, DEVICE_MANUFACTURER);
	}
	else
	{
		// identifiers: node_id only
		snprintf(payload, sizeof(payload),
				 "{"
				 "\"name\":\"%s\","
				 "\"state_topic\":\"%s\","
				 "\"unique_id\":\"%s\","
				 "\"unit_of_measurement\":\"%s\","
				 "\"device_class\":\"%s\","
				 "\"value_template\":\"%s\","
				 "\"device\":{"
				 "\"identifiers\":[\"%s\"],"
				 "\"name\":\"%s\","
				 "\"model\":\"%s\","
				 "\"manufacturer\":\"%s\"",
				 name, state_topic, unique_id,
				 unit ? unit : "",
				 device_class ? device_class : "",
				 value_template ? value_template : "{{ value }}",
				 node_id, node_id, DEVICE_MODEL, DEVICE_MANUFACTURER);
	}

// Append optional fields safely
#ifdef DEVICE_SW_VERSION
	strlcat(payload, ",\"sw_version\":\"", sizeof(payload));
	strlcat(payload, DEVICE_SW_VERSION, sizeof(payload));
	strlcat(payload, "\"", sizeof(payload));
#endif

#ifdef DEVICE_HW_VERSION
	strlcat(payload, ",\"hw_version\":\"", sizeof(payload));
	strlcat(payload, DEVICE_HW_VERSION, sizeof(payload));
	strlcat(payload, "\"", sizeof(payload));
#endif

	// Close device and outer object
	strlcat(payload, "}}", sizeof(payload));

	char topic[256];
	snprintf(topic, sizeof(topic), "homeassistant/sensor/%s/%s/config", node_id, object_id);
	topic[sizeof(topic) - 1] = '\0';

	// publish retained
	mqtt_pubs_retained(topic, payload);
}

static void mqtt_publish_ha_valve_sensor(const char *node_id, const char *object_id,
										 const char *name, const char *state_topic,
										 const char *availability_topic,
										 const char *unique_id)
{
	char payload[512];
	char topic[256];

	snprintf(topic, sizeof(topic),
			 "homeassistant/sensor/%s/%s/config",
			 node_id, object_id);

	snprintf(payload, sizeof(payload),
			 "{"
			 "\"name\":\"%s\","
			 "\"unique_id\":\"%s\","
			 "\"state_topic\":\"%s\","
			 "\"availability_topic\":\"%s\","
			 "\"device\":{"
			 "\"identifiers\":[\"%s\"],"
			 "\"name\":\"%s\","
			 "\"model\":\"OSVentilation\","
			 "\"manufacturer\":\"Peter Lourens\""
			 "}"
			 "}",
			 name,
			 unique_id,
			 state_topic,
			 availability_topic,
			 node_id,
			 node_id);

	mqtt_pubs_retained(topic, payload);
}

// Main discovery publisher — publishes retained discovery messages for HA
void mqtt_publish_homeassistant_discovery(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt || !s_mqtt_connected)
	{
		ESP_LOGW(TAG, "mqtt_publish_homeassistant_discovery: MQTT not connected; skipping");
		return;
	}

	const char *node_id = mqtt_get_node_id();
	char base[150] = {0};
	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	// --- Determine which sensors are present by peeking the queues ---
	float raw[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
	float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
	bool have_raw = false;
	bool have_avg = false;

	if (xQueuePeek(sensor_queue, raw, 0) == pdTRUE)
	{
		have_raw = true;
	}
	if (xQueuePeek(sensor_avg_queue, avg, 0) == pdTRUE)
	{
		have_avg = true;
	}

	// Buffers
	char state_topic[300];
	// char position_topic[300];
	char availability_topic[300];
	char object_id[64];
	char unique_id[150];
	char name[64];

	// Publish discovery only for slots that are present (based on IDX_VALID or CO2 heuristic)
	for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
	{
		for (int slot = 0; slot < CHANNELS_PER_BUS; slot++)
		{
			bool slot_present = false;

			// 1. avg valid flag
			if (have_avg && avg[bus][slot][IDX_VALID] == 1)
				slot_present = true;

			// 2. raw valid flag
			if (!slot_present && have_raw && raw[bus][slot][IDX_VALID] == 1)
				slot_present = true;

			// 3. avg temperature or humidity present
			if (!slot_present && have_avg &&
				(avg[bus][slot][IDX_TEMP] != 0 || avg[bus][slot][IDX_HUMIDITY] != 0))
				slot_present = true;

			// 4. raw temperature or humidity present
			if (!slot_present && have_raw &&
				(raw[bus][slot][IDX_TEMP] != 0 || raw[bus][slot][IDX_HUMIDITY] != 0))
				slot_present = true;

			// 5. CO2 heuristic (avg)
			if (!slot_present && have_avg && avg[bus][slot][IDX_CO2] > 0)
				slot_present = true;

			// 6. CO2 heuristic (raw)
			if (!slot_present && have_raw && raw[bus][slot][IDX_CO2] > 0)
				slot_present = true;

			if (!slot_present)
				continue;

			// Temperature (avg)
			snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/temperature", base, bus, slot);
			state_topic[sizeof(state_topic) - 1] = '\0';

			snprintf(object_id, sizeof(object_id), "bus%d_slot%d_avg_temperature", bus, slot);
			object_id[sizeof(object_id) - 1] = '\0';

			unique_id[0] = '\0';
			strlcpy(unique_id, node_id, sizeof(unique_id));
			strlcat(unique_id, "_", sizeof(unique_id));
			strlcat(unique_id, object_id, sizeof(unique_id));

			snprintf(name, sizeof(name), "Bus%d Slot%d Temperature", bus, slot);
			name[sizeof(name) - 1] = '\0';

			mqtt_publish_ha_sensor(node_id, object_id, name, "°C", "temperature", state_topic, "{{ value }}", unique_id);

			// Humidity (avg)
			snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/humidity", base, bus, slot);
			state_topic[sizeof(state_topic) - 1] = '\0';

			snprintf(object_id, sizeof(object_id), "bus%d_slot%d_avg_humidity", bus, slot);
			object_id[sizeof(object_id) - 1] = '\0';

			unique_id[0] = '\0';
			strlcpy(unique_id, node_id, sizeof(unique_id));
			strlcat(unique_id, "_", sizeof(unique_id));
			strlcat(unique_id, object_id, sizeof(unique_id));

			snprintf(name, sizeof(name), "Bus%d Slot%d Humidity", bus, slot);
			name[sizeof(name) - 1] = '\0';

			mqtt_publish_ha_sensor(node_id, object_id, name, "%", "humidity", state_topic, "{{ value }}", unique_id);

			// CO2 (avg) — only if sensor provides it (based on valid flag or CO2 > 0)
			// if ((have_avg && avg[bus][slot][IDX_CO2] > 0) || (have_raw && raw[bus][slot][IDX_CO2] > 0))
			//{
			snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/co2", base, bus, slot);
			state_topic[sizeof(state_topic) - 1] = '\0';

			snprintf(object_id, sizeof(object_id), "bus%d_slot%d_avg_co2", bus, slot);
			object_id[sizeof(object_id) - 1] = '\0';

			unique_id[0] = '\0';
			strlcpy(unique_id, node_id, sizeof(unique_id));
			strlcat(unique_id, "_", sizeof(unique_id));
			strlcat(unique_id, object_id, sizeof(unique_id));

			snprintf(name, sizeof(name), "Bus%d Slot%d CO2", bus, slot);
			name[sizeof(name) - 1] = '\0';

			mqtt_publish_ha_sensor(node_id, object_id, name, "ppm", "carbon_dioxide", state_topic, "{{ value }}", unique_id);
			//}

			// Pressure (avg)
			// snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/pressure", base, bus, slot);
			// state_topic[sizeof(state_topic) - 1] = '\0';

			// snprintf(object_id, sizeof(object_id), "bus%d_slot%d_avg_pressure", bus, slot);
			// object_id[sizeof(object_id) - 1] = '\0';

			// unique_id[0] = '\0';
			// strlcpy(unique_id, node_id, sizeof(unique_id));
			// strlcat(unique_id, "_", sizeof(unique_id));
			// strlcat(unique_id, object_id, sizeof(unique_id));

			// snprintf(name, sizeof(name), "Bus%d Slot%d Pressure", bus, slot);
			// name[sizeof(name) - 1] = '\0';

			// mqtt_publish_ha_sensor(node_id, object_id, name, "hPa", "pressure", state_topic, "{{ value }}", unique_id);

			// Gas resistance (avg)
			// snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/gas_resistance", base, bus, slot);
			// state_topic[sizeof(state_topic) - 1] = '\0';

			// snprintf(object_id, sizeof(object_id), "bus%d_slot%d_avg_gas", bus, slot);
			// object_id[sizeof(object_id) - 1] = '\0';

			// unique_id[0] = '\0';
			// strlcpy(unique_id, node_id, sizeof(unique_id));
			// strlcat(unique_id, "_", sizeof(unique_id));
			// strlcat(unique_id, object_id, sizeof(unique_id));

			// snprintf(name, sizeof(name), "Bus%d Slot%d Gas Resistance", bus, slot);
			// name[sizeof(name) - 1] = '\0';

			// mqtt_publish_ha_sensor(node_id, object_id, name, "Ω", NULL, state_topic, "{{ value }}", unique_id);
		}
	}

	// publish valves as read-only valves (no command_topic)
	for (int id = 0; id < MAX_VALVE_COUNT; id++)
	{
		snprintf(state_topic, sizeof(state_topic), "%s/valve%d/position", base, id);
		snprintf(availability_topic, sizeof(availability_topic), "%s/valve%d/availability", base, id);
		snprintf(object_id, sizeof(object_id), "valve_%d_position", id);
		snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
		snprintf(name, sizeof(name), "Valve %d Position", id);
		mqtt_publish_ha_valve_sensor(node_id, object_id, name, state_topic, availability_topic, unique_id);
	}

	snprintf(object_id, sizeof(object_id), "system_state");
	object_id[sizeof(object_id) - 1] = '\0';

	unique_id[0] = '\0';
	strlcpy(unique_id, node_id, sizeof(unique_id));
	strlcat(unique_id, "_", sizeof(unique_id));
	strlcat(unique_id, object_id, sizeof(unique_id));

	snprintf(state_topic, sizeof(state_topic), "%s/system/state", base);
	state_topic[sizeof(state_topic) - 1] = '\0';

	// publish as read-only sensor
	// mqtt_publish_ha_select(node_id, object_id, "OSVentilation System State", state_topic, unique_id, state_options);
	mqtt_publish_ha_sensor(node_id, object_id, "OSVentilation System State", "", NULL, state_topic, "{{ value }}", unique_id);

	// Fan speed (select)
	// const char *fan_options = "[\"ERROR\",\"LOW\",\"MEDIUM\",\"HIGH\"]";

	snprintf(object_id, sizeof(object_id), "system_fanspeed");
	object_id[sizeof(object_id) - 1] = '\0';

	unique_id[0] = '\0';
	strlcpy(unique_id, node_id, sizeof(unique_id));
	strlcat(unique_id, "_", sizeof(unique_id));
	strlcat(unique_id, object_id, sizeof(unique_id));

	snprintf(state_topic, sizeof(state_topic), "%s/system/fanspeed", base);
	state_topic[sizeof(state_topic) - 1] = '\0';

	// publish as read-only sensor
	// mqtt_publish_ha_select(node_id, object_id, "OSVentilation Fan Speed", state_topic, unique_id, fan_options);
	mqtt_publish_ha_sensor(node_id, object_id, "OSVentilation Fan Speed", "", NULL, state_topic, "{{ value }}", unique_id);

	ESP_LOGI(TAG, "Published Home Assistant discovery configs (node=%s)", node_id);
}