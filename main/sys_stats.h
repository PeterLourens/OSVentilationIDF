#ifndef SYS_STATS_H
#define SYS_STATS_H

#include <driver/temperature_sensor.h>

#include "globals.h"
#include "general.h"
#include "logging.h"

// Function definitions
const char *state_char_from_enum(eTaskState);
const char *reset_reason_str(esp_reset_reason_t);
void show_system_and_task_stats(void);

#endif