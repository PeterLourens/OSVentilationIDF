#ifndef STATEMACHINE_H
#define STATEMACHINE_H

#include "globals.h"
#include "general.h"
#include "statemachine_helper.h"
#include "fancontrol.h"

void run_statemachine(void);
void init_transitions(void);
void stopped_transitions(void);
void day_transitions(void);
void night_transitions(void);
void high_co2_day_transitions(void);
void high_co2_night_transitions(void);
void high_rh_day_transitions(void);
void high_rh_night_transitions(void);
void cooking_transitions(void);
void valve_cycle_day_transitions(void);
void valve_cycle_night_transitions(void);
void manual_high_speed_transitions(void);

#endif