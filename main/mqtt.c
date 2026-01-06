#include "mqtt.h"

static const char *TAG = "MQTT";

// MQTT client state
static esp_mqtt_client_handle_t s_mqtt = NULL;
volatile bool s_mqtt_connected = false;

// Interpret enable_mqtt string
bool is_enabled_str(const char *s)
{
  	if (!s)
    {
    	return false;
	}

    char buf[16] = {0};
  	strlcpy(buf, s, sizeof(buf));
        for (size_t i = 0; buf[i]; ++i)
        {
    		if (buf[i] >= 'A' && buf[i] <= 'Z')
      		buf[i] = (char)(buf[i] - 'A' + 'a');
  		}

    return (strcmp(buf, "1") == 0) || (strcmp(buf, "true") == 0) || (strcmp(buf, "on") == 0) || (strcmp(buf, "yes") == 0);
}

// Normalize base topic (remove trailing '/')
void normalize_base_topic(const char *in, char *out, size_t out_len)
{
  	if (!in || !out || out_len == 0)
    {
    	return;
	}

    strlcpy(out, in, out_len);
    size_t n = strlen(out);

    while (n > 0 && out[n - 1] == '/')
    {
    	out[n - 1] = '\0';
    	n--;
  	}
}

// MQTT event handler
void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
	esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event_id)
    {
  		case MQTT_EVENT_CONNECTED:
    		s_mqtt_connected = true;
    		ESP_LOGI(TAG, "MQTT v5 connected");
    		break;
  		case MQTT_EVENT_DISCONNECTED:
    		s_mqtt_connected = false;
    		ESP_LOGW(TAG, "MQTT disconnected");
    		break;
  		case MQTT_EVENT_ERROR:
    		s_mqtt_connected = false;
    		ESP_LOGE(TAG, "MQTT error");
    		break;
  		case MQTT_EVENT_PUBLISHED:
    		ESP_LOGD(TAG, "MQTT published msg_id=%d", event->msg_id);
    		break;
  		default:
    		break;
  	}
}

esp_err_t mqtt_start(const MQTT_config_t *cfg)
{
  	if (!cfg)
    {
		ESP_LOGE(TAG, "MQTT cfg is NULL");
		return ESP_ERR_INVALID_ARG;
	}
	if (!is_enabled_str(cfg->enable_mqtt)) {
		ESP_LOGW(TAG, "MQTT disabled by config (enable_mqtt=%s)", cfg->enable_mqtt);
		return ESP_OK;
	}
    if (s_mqtt)
    {
		ESP_LOGI(TAG, "MQTT already started");
		return ESP_OK;
	}

    int port = atoi(cfg->mqtt_port);

    if (port <= 0 || port > 65535)
    {
		ESP_LOGE(TAG, "Invalid MQTT port: %s", cfg->mqtt_port);
		return ESP_ERR_INVALID_ARG;
	}

	char uri[256];
	snprintf(uri, sizeof(uri), "mqtt://%s:%d", cfg->mqtt_server, port);

	// Enable MQTT v5 via session.protocol_ver
	esp_mqtt_client_config_t mqtt_cfg = {
		.broker.address.uri = uri,
		.session.protocol_ver = MQTT_PROTOCOL_V_5,
	};

	s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
	if (!s_mqtt) {
		ESP_LOGE(TAG, "Failed to init MQTT client");
		return ESP_FAIL;
	}

	esp_err_t err = esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to register MQTT event handler: %s",
				esp_err_to_name(err));
		return err;
	}

	// Set default MQTT v5 publish properties (JSON payload hints)
	esp_mqtt5_publish_property_config_t default_pub_props = {
		.payload_format_indicator = true,
		.content_type = "application/json",
	};
	esp_mqtt5_client_set_publish_property(s_mqtt, &default_pub_props);

	err = esp_mqtt_client_start(s_mqtt);
	if (err != ESP_OK) {
		ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
		return err;
	}

	ESP_LOGI(TAG, "MQTT v5 client started (uri=%s)", uri);
	return ESP_OK;
}

void mqtt_stop(void)
{
	if (s_mqtt) {
		esp_mqtt_client_stop(s_mqtt);
		esp_mqtt_client_destroy(s_mqtt);
		s_mqtt = NULL;
		s_mqtt_connected = false;
		ESP_LOGI(TAG, "MQTT client stopped");
	}
}

bool mqtt_is_connected(void)
{
	  return s_mqtt && s_mqtt_connected;
}

// --- JSON for 3D float array ---
// Produces:
// {"dims":{"buses":B,"channels":C,"fields":F},"values":[[[...],[...]],[[...],...]]}
char *format_sensor_3d_json(const float a[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS], size_t *out_len)
{
	const int B = MAX_I2C_BUSES;
	const int C = CHANNELS_PER_BUS;
	const int F = DATA_FIELDS;
	const size_t total = (size_t)B * C * F;

	// Conservative capacity estimate: ~24 bytes per float + overhead
	size_t capacity =
		64 + total * 24 + (size_t)B * 4 + (size_t)C * 4 + (size_t)F * 4;
	char *buf = (char *)malloc(capacity);
	if (!buf)
		return NULL;

	size_t pos = 0;
	int n = snprintf(
		buf + pos, capacity - pos,
		"{\"dims\":{\"buses\":%d,\"channels\":%d,\"fields\":%d},\"values\":[", B,
		C, F);
	if (n < 0) {
		free(buf);
		return NULL;
	}
	pos += (size_t)n;

	for (int b = 0; b < B; ++b) {
		n = snprintf(buf + pos, capacity - pos, "%s[", (b ? "," : ""));
		if (n < 0) {
		free(buf);
		return NULL;
		}
		pos += (size_t)n;

		for (int c = 0; c < C; ++c) {
		n = snprintf(buf + pos, capacity - pos, "%s[", (c ? "," : ""));
		if (n < 0) {
			free(buf);
			return NULL;
		}
		pos += (size_t)n;

		for (int f = 0; f < F; ++f) {
			// Print float with 3 decimals (adjust as needed)
			n = snprintf(buf + pos, capacity - pos, "%s%.3f", (f ? "," : ""),
						a[b][c][f]);
			if (n < 0) {
			free(buf);
			return NULL;
			}
			pos += (size_t)n;
		}

		n = snprintf(buf + pos, capacity - pos, "]");
		if (n < 0) {
			free(buf);
			return NULL;
		}
		pos += (size_t)n;
		}

		n = snprintf(buf + pos, capacity - pos, "]");
		if (n < 0) {
		free(buf);
		return NULL;
		}
		pos += (size_t)n;
	}

	n = snprintf(buf + pos, capacity - pos, "]}");
	if (n < 0) {
		free(buf);
		return NULL;
	}
	pos += (size_t)n;

	if (out_len)
		*out_len = pos;
	return buf;
}

void mqtt_publish_sensor_values(const MQTT_config_t *cfg)
{
	if (!cfg) {
		ESP_LOGE(TAG, "MQTT cfg is NULL");
		return;
	}
	if (!is_enabled_str(cfg->enable_mqtt)) {
		return; // disabled
	}
	if (!s_mqtt || !s_mqtt_connected) {
		ESP_LOGW(TAG, "MQTT not connected; skipping publish");
		return;
	}

	// --- Local 3D arrays to hold queue data ---
	float raw[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
	float avg[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

	// Peek queues (non-destructive)
	if (xQueuePeek(sensor_queue, &raw, 0) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to read data from sensor queue.");
		return;
	}
	ESP_LOGI(TAG, "Read raw sensor data from queue for statemachine.");

	if (xQueuePeek(sensor_avg_queue, &avg, 0) != pdTRUE) {
		ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
		return;
	}
	ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");

	// Build topics
	char base[XLARGE_CONFIG_ITEM] = {0};
	normalize_base_topic(cfg->mqtt_base_topic, base, sizeof(base));

	char topic_raw[XLARGE_CONFIG_ITEM + 32] = {0};
	char topic_avg[XLARGE_CONFIG_ITEM + 32] = {0};
	snprintf(topic_raw, sizeof(topic_raw), "%s/sensor/raw", base);
	snprintf(topic_avg, sizeof(topic_avg), "%s/sensor/avg", base);

	// Optional: set per-message topic aliases (reduce payload on repeated
	// publishes)
	esp_mqtt5_publish_property_config_t props_raw = {
		.payload_format_indicator = true,
		.content_type = "application/json",
		.topic_alias = 1,
	};
	esp_mqtt5_publish_property_config_t props_avg = {
		.payload_format_indicator = true,
		.content_type = "application/json",
		.topic_alias = 2,
	};

	// Format RAW JSON
	size_t len_raw = 0;
	char *payload_raw = format_sensor_3d_json(raw, &len_raw);
	if (!payload_raw) {
		ESP_LOGE(TAG, "Failed to format RAW JSON payload");
		return;
	}

	// Publish RAW
	esp_mqtt5_client_set_publish_property(s_mqtt, &props_raw);
	int msg_id_raw =
		esp_mqtt_client_publish(s_mqtt, topic_raw, payload_raw, (int)len_raw,
								1 /* QoS */, 0 /* retain */);
	if (msg_id_raw >= 0) {
		ESP_LOGI(TAG, "Published RAW to %s (msg_id=%d)", topic_raw, msg_id_raw);
	} else {
		ESP_LOGE(TAG, "Failed to publish RAW to %s", topic_raw);
	}
	free(payload_raw);

	// Format AVG JSON
	size_t len_avg = 0;
	char *payload_avg = format_sensor_3d_json(avg, &len_avg);
	if (!payload_avg) {
		ESP_LOGE(TAG, "Failed to format AVG JSON payload");
		return;
	}

	// Publish AVG
	esp_mqtt5_client_set_publish_property(s_mqtt, &props_avg);
	int msg_id_avg = esp_mqtt_client_publish(s_mqtt, topic_avg, payload_avg,
											(int)len_avg, 1, 0);
	if (msg_id_avg >= 0) {
		ESP_LOGI(TAG, "Published AVG to %s (msg_id=%d)", topic_avg, msg_id_avg);
	} else {
		ESP_LOGE(TAG, "Failed to publish AVG to %s", topic_avg);
	}
	free(payload_avg);
}
