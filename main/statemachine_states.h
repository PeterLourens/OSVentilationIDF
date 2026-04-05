/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#pragma once

// Unified state definitions
typedef enum
{
    STATE_NR_ERROR = 0,
    STATE_NR_INIT,
    STATE_NR_DAY,
    STATE_NR_NIGHT,
    STATE_NR_HIGHCO2DAY,
    STATE_NR_HIGHCO2NIGHT,
    STATE_NR_HIGHRHDAY,
    STATE_NR_HIGHRHNIGHT,
    STATE_NR_COOKING,
    STATE_NR_CYCLINGDAY,
    STATE_NR_CYCLINGNIGHT,
    STATE_NR_MANUALHS,
    STATE_NR_STOPPED,
    STATE_NR_TEMP,
    STATE_NR_MAX
} state_nr_t;

// Fan speed levels
typedef enum
{
    FAN_SPEED_ERROR = 0,
    FAN_SPEED_LOW,
    FAN_SPEED_MEDIUM,
    FAN_SPEED_HIGH
} fan_speed_t;

// Optional: forward declaration for translator
const char *state_nr_to_str(state_nr_t state);
const char *fan_speed_to_str(fan_speed_t f);