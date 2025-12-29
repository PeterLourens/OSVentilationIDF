#ifndef STATEMACHINE_HELPER_H
#define STATEMACHINE_HELPER_H

#include "globals.h"
#include "general.h"

extern float (*statemachine_sensor_data)[CHANNELS_PER_BUS][DATA_FIELDS];
extern float (*statemachine_avg_sensor_data)[CHANNELS_PER_BUS][DATA_FIELDS];

// Sensor averaging helpers
void sensor_data_average(void);

// Push sensor value into FIFO for moving average calculation
void fifoPush(int bus, int slot, int measurement, float value, int pos);

// Return index of a state by case-insensitive name, or -1 if not found
int find_state_index_by_name_ci(const char *name);

// Time and mode helpers
bool is_day(void);
bool is_weekend(void);
bool cooking_times(void);

// Cycling valve helpers: return true when valve should be active in day or night cycling states
bool valve_cycle_times_day(void);
bool valve_cycle_times_night(void);

// Select sensors which have high readings
void select_sensors(void);

// Memory allocation helpers for statemachine sensor data buffers
esp_err_t statemachine_alloc_buffers(void);
void statemachine_free_buffers(void);

#endif /* STATEMACHINE_HELPER_H */