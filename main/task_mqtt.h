#ifndef TASK_MQTT_H
#define TASK_MQTT_H

#include "globals.h"
#include "rgb_leds.h"
#include "mqtt.h"
#include "logging.h"

// Function declarations
void start_task_mqtt(void);
void task_mqtt_code(void *);

#endif