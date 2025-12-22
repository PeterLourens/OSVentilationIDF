#ifndef STATEMACHINE_HELPER_H
#define STATEMAHINE_HELPER_H

#include "globals.h"
#include "general.h"

void sensor_data_average(void);
static void fifoPush(int bus, int slot, int measurement, float value, int pos);
// int fanspeed_char_to_int(char);

#endif