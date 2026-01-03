#ifndef VALVECONTROL_H
#define VALVECONTROL_H

#include "globals.h"
#include "general.h"

#include "driver/gpio.h"

// Function declarations
esp_err_t gpio_setup_3pins(int, int, int);
void short_delay_us(int us);
void shift_out_msbfist(int, int, uint8_t);
void latch_pulse(int);
void all_outputs_off(int, int, int);
void read_valve_pos_in_valvecontrol(void);
void control_valves(void);
void valve_position_statemachine(int);
esp_err_t valvecontrol(int, int, int, int, int, int);

#endif