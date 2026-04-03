/*
	This file is part of the OSVentilationIDF project.

	Copyright (C) 2023-2026 Peter Lourens
*/

#include "mqtt.h"

static const char *TAG = "mqtt_drv";

static esp_mqtt_client_handle_t s_mqtt_client = NULL;
static bool s_mqtt_started = false;
static bool s_mqtt_connected = false;
static char mqtt_node_id[64] = {0}; // Cached node id (derived from sm_actuals.hostname or fallback)

static volatile bool s_discovery_requested = false;

// Retained publish helper
static inline void mqtt_pubs_retained(const char *topic, const char *value)
{
	if (!s_mqtt_client || !s_mqtt_connected)
	{
		LOGW(TAG, "mqtt_pubs_retained: MQTT not connected; skipping %s", topic);
		return;
	}
	if (!value)
	{
		LOGW(TAG, "mqtt_pubs_retained: NULL value for topic %s", topic);
		return;
	}
	esp_err_t err = esp_mqtt_client_publish(s_mqtt_client, topic, value, 0, 0, 1);
	if (err < 0)
	{
		LOGW(TAG, "mqtt_pubs_retained: publish failed for topic %s", topic);
	}
}

// Publish discovery config for a sensor entity
static void mqtt_publish_ha_sensor(const char *node_id, const char *object_id, const char *name, const char *unit, const char *device_class, const char *state_topic, const char *value_template, const char *unique_id, const char *icon)
{
	char payload[500] = {0};
	char topic[200] = {0};

	snprintf(topic, sizeof(topic),
			 "homeassistant/sensor/%s/%s/config",
			 node_id, object_id);

	// Base fields
	snprintf(payload, sizeof(payload),
			 "{"
			 "\"name\":\"%s\","
			 "\"state_topic\":\"%s\","
			 "\"unique_id\":\"%s\","
			 "\"value_template\":\"%s\"",
			 name,
			 state_topic,
			 unique_id,
			 value_template ? value_template : "{{ value }}");

	// Optional fields are appended with a leading comma
	if (unit)
	{
		strlcat(payload, ",\"unit_of_measurement\":\"", sizeof(payload));
		strlcat(payload, unit, sizeof(payload));
		strlcat(payload, "\"", sizeof(payload));
	}

	if (device_class)
	{
		strlcat(payload, ",\"device_class\":\"", sizeof(payload));
		strlcat(payload, device_class, sizeof(payload));
		strlcat(payload, "\"", sizeof(payload));
	}

	if (icon)
	{
		strlcat(payload, ",\"icon\":\"", sizeof(payload));
		strlcat(payload, icon, sizeof(payload));
		strlcat(payload, "\"", sizeof(payload));
	}

	// Device block (always present, prepend with comma)
	strlcat(payload,
			",\"device\":{"
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

// Publish discovery config for a valve sensor (position + availability)
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

// Helper function to publish Home Assistant discovery config for one sensor
static void mqtt_publish_discovery_for_sensor(int bus, int slot,
											  const char *base,
											  const char *node_id,
											  const float snapshot[DATA_FIELDS])
{
	if (!snapshot || snapshot[IDX_VALID] != 1.0f)
		return;

	char state_topic[200];
	char object_id[60];
	char unique_id[120];
	char name[60];

	// ---------------- TEMPERATURE ----------------
	snprintf(state_topic, sizeof(state_topic),
			 "%s/sensors/bus%d/slot%d/avg/temperature", base, bus, slot);
	snprintf(object_id, sizeof(object_id),
			 "bus%d_slot%d_temperature", bus, slot);
	snprintf(unique_id, sizeof(unique_id),
			 "%s_%s", node_id, object_id);
	snprintf(name, sizeof(name),
			 "Bus%d Slot%d Temperature", bus, slot);

	mqtt_publish_ha_sensor(node_id, object_id, name,
						   "°C", "temperature",
						   state_topic, "{{ value }}",
						   unique_id, "mdi:thermometer");

	// ---------------- HUMIDITY ----------------
	snprintf(state_topic, sizeof(state_topic),
			 "%s/sensors/bus%d/slot%d/avg/humidity", base, bus, slot);
	snprintf(object_id, sizeof(object_id),
			 "bus%d_slot%d_humidity", bus, slot);
	snprintf(unique_id, sizeof(unique_id),
			 "%s_%s", node_id, object_id);
	snprintf(name, sizeof(name),
			 "Bus%d Slot%d Humidity", bus, slot);

	mqtt_publish_ha_sensor(node_id, object_id, name,
						   "%", "humidity",
						   state_topic, "{{ value }}",
						   unique_id, "mdi:water-percent");

	// ---------------- CO2 (ONLY SCD41) ----------------
	if (sensor_supports_co2(bus, slot) && snapshot[IDX_CO2] > 0.0f)
	{
		snprintf(state_topic, sizeof(state_topic),
				 "%s/sensors/bus%d/slot%d/avg/co2", base, bus, slot);
		snprintf(object_id, sizeof(object_id),
				 "bus%d_slot%d_co2", bus, slot);
		snprintf(unique_id, sizeof(unique_id),
				 "%s_%s", node_id, object_id);
		snprintf(name, sizeof(name),
				 "Bus%d Slot%d CO2", bus, slot);

		mqtt_publish_ha_sensor(node_id, object_id, name,
							   "ppm", "carbon_dioxide",
							   state_topic, "{{ value }}",
							   unique_id, "mdi:molecule-co2");
	}
}

// Helper function to publish Home Assistant discovery config for one valve
static void mqtt_publish_discovery_for_valve(int id, const char *base, const char *node_id)
{
	char state_topic[150];
	char availability_topic[150];
	char object_id[60];
	char unique_id[120];
	char name[60];

	snprintf(state_topic, sizeof(state_topic), "%s/valve%d/position", base, id);
	snprintf(availability_topic, sizeof(availability_topic), "%s/valve%d/availability", base, id);
	snprintf(object_id, sizeof(object_id), "valve_%d_position", id);
	snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
	snprintf(name, sizeof(name), "Valve %d Position", id);

	mqtt_publish_ha_valve_sensor(node_id, object_id, name, state_topic, availability_topic, unique_id);
}

// Helper function to publish Home Assistant discovery config for system-level sensors (state, fan speed)
static void mqtt_publish_discovery_for_system(const char *base, const char *node_id)
{
	char object_id[40];
	char unique_id[120];
	char state_topic[150];

	// System state
	strcpy(object_id, "system_state");
	snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
	snprintf(state_topic, sizeof(state_topic), "%s/system/state", base);

	mqtt_publish_ha_sensor(node_id, object_id, "OSVentilation System State",
						   NULL, NULL, state_topic, "{{ value }}",
						   unique_id, "mdi:information-outline");

	// Fan speed
	strcpy(object_id, "system_fanspeed");
	snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
	snprintf(state_topic, sizeof(state_topic), "%s/system/fanspeed", base);

	mqtt_publish_ha_sensor(node_id, object_id, "OSVentilation Fan Speed",
						   NULL, NULL, state_topic, "{{ value }}",
						   unique_id, "mdi:fan");
}

// Helpers for publishing
static inline void mqtt_pubf(const char *topic, float value)
{
	if (!s_mqtt_client || !s_mqtt_connected)
	{
		LOGW(TAG, "mqtt_pubf: MQTT not connected; skipping publish for %s", topic);
		return;
	}

	char payload[32];
	snprintf(payload, sizeof(payload), "%.2f", value);

	int err = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
	if (err < 0)
	{
		LOGW(TAG, "mqtt_pubf: publish failed for topic %s", topic);
	}
}

// Publish string value to MQTT
static inline void mqtt_pubs(const char *topic, const char *value)
{
	if (!s_mqtt_client || !s_mqtt_connected)
	{
		LOGW(TAG, "mqtt_pubs: MQTT not connected; skipping publish");
		return;
	}

	if (!value)
	{
		LOGW(TAG, "mqtt_pubs: NULL value for topic %s", topic);
		return;
	}

	esp_err_t err = esp_mqtt_client_publish(s_mqtt_client, topic, value, 0, 1, 0);
	if (err < 0)
	{
		LOGW(TAG, "mqtt_pubs: publish failed for topic %s", topic);
	}
}

// Publish integer value to MQTT
static inline void mqtt_pubi(const char *topic, int value)
{
	if (!s_mqtt_client || !s_mqtt_connected)
	{
		LOGW(TAG, "mqtt_pubi: MQTT not connected; skipping publish");
		return;
	}

	char payload[16];
	snprintf(payload, sizeof(payload), "%d", value);

	esp_err_t err = esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 1, 0);
	if (err < 0)
	{
		LOGW(TAG, "mqtt_pubi: publish failed for topic %s", topic);
	}
}

static inline void mqtt_topic(char *out, size_t out_sz, const char *base, int bus, int slot, const char *type, const char *field)
{
	snprintf(out, out_sz, "%s/sensors/bus%d/slot%d/%s/%s", base, bus, slot, type, field);
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
	snprintf(out, out_len, "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void mqtt_request_homeassistant_discovery(void)
{
	s_discovery_requested = true;
}

bool mqtt_discovery_requested(void)
{
	return s_discovery_requested;
}

void mqtt_clear_discovery_request(void)
{
	s_discovery_requested = false;
}

void mqtt_set_connected(bool state)
{
	s_mqtt_connected = state;
}

bool mqtt_is_connected(void)
{
	return s_mqtt_connected;
}

esp_mqtt_client_handle_t mqtt_get_client(void)
{
	return s_mqtt_client;
}

esp_err_t mqtt_start(const MQTT_config_t *cfg)
{
	if (s_mqtt_started)
	{
		LOGI(TAG, "mqtt_start: already started");
		return ESP_OK;
	}

	if (!cfg)
	{
		LOGE(TAG, "mqtt_start: cfg is NULL");
		return ESP_ERR_INVALID_ARG;
	}

	char uri[128] = {0};

	// Prefer cfg->uri if it already contains a scheme
	if (cfg->uri[0] != '\0')
	{
		strlcpy(uri, cfg->uri, sizeof(uri));
	}
	else if (cfg->mqtt_server[0] != '\0' && cfg->mqtt_port[0] != '\0')
	{
		// Backward‑compatible: build mqtt://host:port
		snprintf(uri, sizeof(uri), "mqtt://%s:%s", cfg->mqtt_server, cfg->mqtt_port);

		// Also store into global config so future snapshots have a proper URI
		if (xSemaphoreTake(mqtt_config_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
		{
			strlcpy(mqtt_config.uri, uri, sizeof(mqtt_config.uri));
			xSemaphoreGive(mqtt_config_mutex);
		}
	}
	else
	{
		LOGE(TAG, "mqtt_start: no valid URI or server/port in config");
		return ESP_ERR_INVALID_ARG;
	}

	LOGI(TAG, "mqtt_start: using URI '%s'", uri);

	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = uri,
		.credentials.username = cfg->username,
		.credentials.authentication.password = cfg->password,
		.session.keepalive = cfg->keepalive,
		.session.last_will.topic = cfg->will_topic,
		.session.last_will.msg = cfg->will_message,
		.session.last_will.qos = cfg->will_qos,
		.session.last_will.retain = cfg->will_retain,
	};

	s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
	if (s_mqtt_client == NULL)
	{
		LOGE(TAG, "esp_mqtt_client_init failed");
		return ESP_FAIL;
	}

	esp_err_t err = esp_mqtt_client_register_event(
		s_mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_forwarder, NULL);
	if (err != ESP_OK)
	{
		LOGE(TAG, "esp_mqtt_client_register_event failed: %s", esp_err_to_name(err));
		esp_mqtt_client_destroy(s_mqtt_client);
		s_mqtt_client = NULL;
		return err;
	}

	err = esp_mqtt_client_start(s_mqtt_client);
	if (err != ESP_OK)
	{
		LOGE(TAG, "esp_mqtt_client_start failed: %s", esp_err_to_name(err));
		esp_mqtt_client_destroy(s_mqtt_client);
		s_mqtt_client = NULL;
		return err;
	}

	s_mqtt_started = true;
	LOGI(TAG, "MQTT client started");
	return ESP_OK;
}

esp_err_t mqtt_stop(void)
{
	if (!s_mqtt_started || s_mqtt_client == NULL)
	{
		LOGW(TAG, "mqtt_stop: not started");
		return ESP_OK;
	}

	// Make sure publishers see us as down
	s_mqtt_connected = false;

	esp_err_t err = esp_mqtt_client_stop(s_mqtt_client);
	if (err != ESP_OK)
	{
		LOGE(TAG, "esp_mqtt_client_stop failed: %s", esp_err_to_name(err));
	}

	err = esp_mqtt_client_destroy(s_mqtt_client);
	if (err != ESP_OK)
	{
		LOGE(TAG, "esp_mqtt_client_destroy failed: %s", esp_err_to_name(err));
	}

	s_mqtt_client = NULL;
	s_mqtt_started = false;
	LOGI(TAG, "MQTT client stopped");
	return ESP_OK;
}

int mqtt_publish(const char *topic, const char *data, int len, int qos, int retain)
{
	if (!s_mqtt_client)
	{
		LOGW(TAG, "mqtt_publish: client not started");
		return -1;
	}
	return esp_mqtt_client_publish(s_mqtt_client, topic, data, len, qos, retain);
}

int mqtt_subscribe(const char *topic, int qos)
{
	if (!s_mqtt_client)
	{
		LOGW(TAG, "mqtt_subscribe: client not started");
		return -1;
	}
	return esp_mqtt_client_subscribe(s_mqtt_client, topic, qos);
}

// This function is called by esp-mqtt and forwards events to the global event loop
void mqtt_event_forwarder(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

	// Handle incoming data here
	if (event_id == MQTT_EVENT_DATA)
	{
		mqtt_handle_incoming(event);
	}

	// Forward to system event loop for network_manager.c
	esp_event_post(MQTT_EVENTS, event_id, event, sizeof(*event), portMAX_DELAY);
}

// Mutex-protected config snapshot
void mqtt_get_config_snapshot(MQTT_config_t *out)
{
	if (!out)
		return;

	xSemaphoreTake(mqtt_config_mutex, portMAX_DELAY);
	memcpy(out, &mqtt_config, sizeof(MQTT_config_t));
	xSemaphoreGive(mqtt_config_mutex);
}

// Check if MQTT is enabled helper
static inline bool mqtt_is_enabled(const MQTT_config_t *cfg)
{
	return strcmp(cfg->enable_mqtt, "On") == 0;
}

// Set manual high speed mode (mutex-protected write to sm_config)
void statemachine_set_speed(const char *payload, size_t len)
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

void normalize_base_topic(const char *in, char *out, size_t out_size)
{
	size_t pos = 0;
	size_t start = 0;

	if (!in || !out || out_size == 0)
		return;

	size_t len = strlen(in);

	while (start < len && in[start] == '/')
		start++;

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

// Publish all sensor values
void mqtt_publish_sensor_values(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	char base[150] = {0};
	char topic[200] = {0};

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt_client || !s_mqtt_connected)
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

	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

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

			// CO2 only for SCD41, and only if valid (>0)
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

			// CO2
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
	int32_t pos = 0;
	char topic[200] = {0};
	char base[150] = {0};
	char key[16] = {0};

	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt_client || !s_mqtt_connected)
	{
		LOGW(TAG, "... not connected; skipping ...");
		return;
	}

	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	// Open NVS namespace for valve positions
	nvs_handle_t nvs;
	esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);
	if (err != ESP_OK)
	{
		LOGE(TAG, "Failed to open NVS for valve positions: %s", esp_err_to_name(err));
		return;
	}

	for (int id = 0; id < MAX_VALVE_COUNT; id++)
	{
		pos = 0;

		snprintf(key, sizeof(key), "valve%d", id);

		err = nvs_get_i32(nvs, key, &pos);
		if (err != ESP_OK)
		{
			LOGW(TAG, "Valve %d position not found in NVS (err=%s), defaulting to 0", id, esp_err_to_name(err));
			pos = 0;
		}

		// Publish numeric position
		snprintf(topic, sizeof(topic), "%s/valve%d/position", base, id);
		mqtt_pubi(topic, (int)pos);

		// Publish availability
		snprintf(topic, sizeof(topic), "%s/valve%d/availability", base, id);
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

	char base[200] = {0};
	char topic[300] = {0};
	char uptime_str[64] = {0};

	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	formatted_uptime(uptime_str, sizeof(uptime_str));

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt_client || !s_mqtt_connected)
	{
		LOGW(TAG, "... not connected; skipping ...");
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

void mqtt_publish_homeassistant_discovery(void)
{
	MQTT_config_t cfg;
	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg))
		return;

	if (!s_mqtt_client || !s_mqtt_connected)
	{
		LOGW(TAG, "... not connected; skipping ...");
		return;
	}

	char base[120] = {0};
	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	const char *node_id = mqtt_get_node_id();

	float raw[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

	if (xQueuePeek(sensor_queue, raw, 0) != pdTRUE)
	{
		LOGW(TAG, "mqtt_publish_homeassistant_discovery: sensor_queue empty, skipping sensors");
	}
	else
	{
		for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
		{
			for (int slot = 0; slot < CHANNELS_PER_BUS; slot++)
			{
				mqtt_publish_discovery_for_sensor(bus, slot, base, node_id,
												  raw[bus][slot]);
			}
		}
	}

	// Valves
	for (int id = 0; id < MAX_VALVE_COUNT; id++)
	{
		mqtt_publish_discovery_for_valve(id, base, node_id);
	}

	// System
	mqtt_publish_discovery_for_system(base, node_id);
}

// Main discovery publisher — publishes retained discovery messages for HA
/*void mqtt_publish_homeassistant_discovery(void)
{
	MQTT_config_t cfg;
	char base[120] = {0};
	const char *node_id = mqtt_get_node_id();
	float raw[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

	mqtt_get_config_snapshot(&cfg);

	if (!mqtt_is_enabled(&cfg) || !s_mqtt_client || !s_mqtt_connected)
		return;

	normalize_base_topic(cfg.mqtt_base_topic, base, sizeof(base));

	bool have_raw = xQueuePeek(sensor_queue, &raw, 0) == pdTRUE;

	for (int bus = 0; bus < MAX_I2C_BUSES; bus++)
	{
		for (int slot = 0; slot < CHANNELS_PER_BUS; slot++)
		{
			if (!have_raw || raw[bus][slot][IDX_VALID] != 1)
				continue;

			// ---------------- TEMPERATURE ----------------
			{
				char state_topic[200];
				char object_id[60];
				char unique_id[120];
				char name[60];

				snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/temperature", base, bus, slot);
				snprintf(object_id, sizeof(object_id), "bus%d_slot%d_temperature", bus, slot);
				snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
				snprintf(name, sizeof(name), "Bus%d Slot%d Temperature", bus, slot);

				mqtt_publish_ha_sensor(node_id, object_id, name, "°C", "temperature", state_topic, "{{ value }}", unique_id, "mdi:thermometer");
			}

			// ---------------- HUMIDITY ----------------
			{
				char state_topic[200];
				char object_id[60];
				char unique_id[120];
				char name[60];

				snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/humidity", base, bus, slot);
				snprintf(object_id, sizeof(object_id), "bus%d_slot%d_humidity", bus, slot);
				snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
				snprintf(name, sizeof(name), "Bus%d Slot%d Humidity", bus, slot);

				mqtt_publish_ha_sensor(node_id, object_id, name, "%", "humidity", state_topic, "{{ value }}", unique_id, "mdi:water-percent");
			}

			// ---------------- CO2 (ONLY SCD41) ----------------
			if (sensor_supports_co2(bus, slot) &&
				raw[bus][slot][IDX_CO2] > 0)
			{
				char state_topic[150];
				char object_id[60];
				char unique_id[120];
				char name[60];

				snprintf(state_topic, sizeof(state_topic), "%s/sensors/bus%d/slot%d/avg/co2", base, bus, slot);
				snprintf(object_id, sizeof(object_id), "bus%d_slot%d_co2", bus, slot);
				snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
				snprintf(name, sizeof(name), "Bus%d Slot%d CO2", bus, slot);

				mqtt_publish_ha_sensor(node_id, object_id, name, "ppm", "carbon_dioxide", state_topic, "{{ value }}", unique_id, "mdi:molecule-co2");
			}
		}
	}

	// ---------------- VALVES ----------------
	for (int id = 0; id < MAX_VALVE_COUNT; id++)
	{
		char state_topic[150];
		char availability_topic[150];
		char object_id[60];
		char unique_id[120];
		char name[60];

		snprintf(state_topic, sizeof(state_topic), "%s/valve%d/position", base, id);
		snprintf(availability_topic, sizeof(availability_topic), "%s/valve%d/availability", base, id);
		snprintf(object_id, sizeof(object_id), "valve_%d_position", id);
		snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
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
		char state_topic[150];

		snprintf(unique_id, sizeof(unique_id), "%s_%s", node_id, object_id);
		snprintf(state_topic, sizeof(state_topic), "%s/system/fanspeed", base);

		mqtt_publish_ha_sensor(node_id, object_id, "OSVentilation Fan Speed", NULL, NULL, state_topic, "{{ value }}", unique_id, "mdi:fan");
	}
}*/
