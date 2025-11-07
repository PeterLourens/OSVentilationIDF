#ifndef VALVECONTROL_H
#define VALVECONTROL_H

#include "globals.h"
#include "general.h"

// Function declarations
esp_err_t gpio_setup_3pins(int, int, int);
void short_delay_us(int us);
void shift_out_msbfist(int, int, uint8_t);
void latch_pulse(int);
void all_outputs_off(int, int, int);
void valvecontrol(int, int, int, int, int, int);

#endif