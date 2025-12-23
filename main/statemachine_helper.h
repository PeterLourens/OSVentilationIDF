#ifndef STATEMACHINE_HELPER_H
#define STATEMAHINE_HELPER_H

#include "globals.h"
#include "general.h"

void sensor_data_average(void);
void fifoPush(int, int, int, float, int);
bool is_day(void);
bool is_weekend(void);
bool cooking_times(void);
bool valve_cycle_times_day(void);
bool valve_cycle_times_night(void);
// int fanspeed_char_to_int(char);

#endif