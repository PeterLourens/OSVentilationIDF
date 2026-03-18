/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#ifndef STATEMACHINE_HELPER_H
#define STATEMACHINE_HELPER_H

#include <math.h> // For isfinite()

#include "globals.h"
#include "general.h"
#include "statemachine.h"
#include "logging.h"

// Sensor averaging helpers
void sensor_data_average(void);

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

#endif /* STATEMACHINE_HELPER_H */