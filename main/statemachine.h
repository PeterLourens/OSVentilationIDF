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

/* Sensor helpers exposed so other translation units can inspect selected sensors */
typedef struct
{
    char *valve;
    float co2_reading;
} CO2_Sensors_t;

typedef struct
{
    char *valve;
    float rh_reading;
} RH_Sensors_t;

extern CO2_Sensors_t co2_sensors[MAX_SENSORS];
extern RH_Sensors_t rh_sensors[MAX_SENSORS];
extern int co2_sensor_counter;
extern int rh_sensor_counter;
extern int co2_sensors_high;
extern int rh_sensors_high;

#endif