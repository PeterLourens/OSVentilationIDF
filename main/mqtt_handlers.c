#include "mqtt_handlers.h"
#include "statemachine.h"
#include "esp_log.h"

static const char *TAG = "mqtt_handlers";

void mqtt_handle_incoming(esp_mqtt_event_handle_t event)
{
    const char *topic = event->topic;
    const char *payload = event->data;
    int tlen = event->topic_len;
    int plen = event->data_len;

    LOGI(TAG, "MQTT message received");
    LOGD(TAG, "Topic (%d): %.*s", tlen, tlen, topic);
    LOGD(TAG, "Payload (%d): %.*s", plen, plen, payload);

    if (tlen == strlen("zigbee2mqtt/Ventilation/action") &&
        strncmp(topic, "zigbee2mqtt/Ventilation/action", tlen) == 0)
    {
        statemachine_set_speed(payload, plen);
    }
}
