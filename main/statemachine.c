/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "statemachine.h"

static const char *TAG = "sm";

int old_time = 0;
int elapsed_time = 0;

int co2_sensor_counter = 0;
int rh_sensor_counter = 0;

CO2_Sensors_t co2_sensors[MAX_SENSORS] = {};
RH_Sensors_t rh_sensors[MAX_SENSORS] = {};

// Functions for the state machine would go here
void init_statemachine(void)
{
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_INIT;
        sm_actuals.fanspeed = FANSPEED_LOW;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "init_statemachine: statemachine_mutex unavailable; applying defaults locally");
    }

    set_fanspeed(FANSPEED_LOW);
}

void run_statemachine(void)
{
    int temp_state = STATE_NR_INIT;

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        temp_state = sm_actuals.state;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "run_statemachine: could not read sm_actuals; using last known state %d", temp_state);
    }

    switch (temp_state)
    {
    case STATE_NR_INIT:
        init_transitions();
        break;
    case STATE_NR_STOPPED:
        stopped_transitions();
        break;
    case STATE_NR_DAY:
        day_transitions();
        break;
    case STATE_NR_NIGHT:
        night_transitions();
        break;
    case STATE_NR_HIGHCO2DAY:
        high_co2_day_transitions();
        break;
    case STATE_NR_HIGHCO2NIGHT:
        high_co2_night_transitions();
        break;
    case STATE_NR_HIGHRHDAY:
        high_rh_day_transitions();
        break;
    case STATE_NR_HIGHRHNIGHT:
        high_rh_night_transitions();
        break;
    case STATE_NR_COOKING:
        cooking_transitions();
        break;
    case STATE_NR_CYCLINGDAY:
        valve_cycle_day_transitions();
        break;
    case STATE_NR_CYCLINGNIGHT:
        valve_cycle_night_transitions();
        break;
    case STATE_NR_MANUALHS:
        manual_high_speed_transitions();
        break;
    default:
        LOGE(TAG, "run_statemachine: unknown state %d, resetting to INIT", temp_state);
        init_transitions();
        break;
    }
}

void stopped_transitions(void)
{
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_STOPPED;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "stopped_transitions: could not take statemachine_mutex");
    }

    LOGI(TAG, "State is stopped. Use valve control web page to start.");
}

void init_transitions(void)
{
    int new_state = STATE_NR_DAY;

    if (is_day())
    {
        LOGI(TAG, "init_transitions: transit to DAY");
        new_state = STATE_NR_DAY;
    }
    else
    {
        LOGI(TAG, "init_transitions: transit to NIGHT");
        new_state = STATE_NR_NIGHT;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = new_state;
        sm_actuals.fanspeed = FANSPEED_LOW;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "init_transitions: could not update sm_actuals");
    }

    set_fanspeed(FANSPEED_LOW);
}

void day_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    int state_config_index = -1;
    int state_fanspeed = FANSPEED_LOW;
    int minimum_state_time = 0;
    int co2highlevel = 0;
    int rhhighlevel = 0;
    bool valve_move_locked = false;
    int co2_sensors_high = 0;
    int rh_sensors_high = 0;
    int new_state = STATE_NR_DAY;

    if (!sensors_queues_ready || sensor_avg_queue == NULL)
    {
        LOGW(TAG, "day_transitions: sensor queues not ready; skipping sensor-dependent logic");
    }
    else if (sensor_avg_item_size != sizeof(statemachine_avg_sensor_data))
    {
        LOGE(TAG, "day_transitions: sensor_avg_item_size mismatch; skipping sensor-dependent logic");
    }
    else
    {
        if (xQueuePeek(sensor_avg_queue, &statemachine_avg_sensor_data, pdMS_TO_TICKS(50)) != pdTRUE)
        {
            LOGW(TAG, "day_transitions: failed to peek sensor_avg_queue");
        }
    }

    // Find state config index
    {
        char state_name[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name, STATE_DAY, sizeof(state_name) - 1);
        state_config_index = find_state_index_by_name_ci(state_name);
        if (state_config_index < 0)
        {
            LOGW(TAG, "day_transitions: state '%s' not configured", state_name);
            state_config_index = 0; // fallback to index 0 to avoid out-of-bounds
        }
    }

    // Read config under mutex
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        state_fanspeed = statemachine_config[state_config_index].fanspeed;
        minimum_state_time = statemachine_config[state_config_index].duration_minutes;
        co2highlevel = statemachine_gen_config.co2_high_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "day_transitions: could not take state_config_mutex; using defaults");
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_DAY;
        sm_actuals.fanspeed = state_fanspeed;
        valve_move_locked = sm_actuals.valve_lock;
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;

        // Count high sensors using selected lists
        for (int i = 0; i < co2_sensor_counter; ++i)
        {
            if (co2_sensors[i].co2_reading > co2highlevel)
                sm_actuals.co2_sensors_high++;
        }
        for (int i = 0; i < rh_sensor_counter; ++i)
        {
            if (rh_sensors[i].rh_reading > rhhighlevel)
                sm_actuals.rh_sensors_high++;
        }

        co2_sensors_high = sm_actuals.co2_sensors_high;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "day_transitions: could not take statemachine_mutex");
    }

    set_fanspeed(state_fanspeed);

    // Update elapsed time
    {
        int new_time = (int)(esp_timer_get_time() / 1000000ULL);
        if (new_time > old_time)
        {
            elapsed_time += new_time - old_time;
            old_time = new_time;
        }
    }

    // Move valves if allowed
    if (!valve_move_locked)
    {
        valve_position_statemachine(state_config_index);
    }
    else
    {
        LOGW(TAG, "day_transitions: valves locked; skipping move");
    }

    // Decide transitions
    if (co2_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHCO2DAY;
    }
    else if (rh_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHRHDAY;
    }
    else if (!is_day())
    {
        new_state = STATE_NR_NIGHT;
    }
    else if (cooking_times())
    {
        new_state = STATE_NR_COOKING;
    }
    else if (valve_cycle_times_day())
    {
        new_state = STATE_NR_CYCLINGDAY;
    }
    else
    {
        new_state = STATE_NR_DAY;
    }

    if (new_state != STATE_NR_DAY)
    {
        elapsed_time = 0;
        old_time = (int)(esp_timer_get_time() / 1000000ULL);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "day_transitions: could not update sm_actuals to new state %d", new_state);
    }
}

void night_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    int state_config_index = -1;
    int state_fanspeed = FANSPEED_LOW;
    int minimum_state_time = 0;
    int co2highlevel = 0;
    int rhhighlevel = 0;
    bool valve_move_locked = false;
    int co2_sensors_high = 0;
    int rh_sensors_high = 0;
    int new_state = STATE_NR_NIGHT;

    if (!sensors_queues_ready || sensor_avg_queue == NULL)
    {
        LOGW(TAG, "night_transitions: sensor queues not ready; skipping sensor-dependent logic");
    }
    else if (sensor_avg_item_size != sizeof(statemachine_avg_sensor_data))
    {
        LOGE(TAG, "night_transitions: sensor_avg_item_size mismatch; skipping sensor-dependent logic");
    }
    else
    {
        if (xQueuePeek(sensor_avg_queue, &statemachine_avg_sensor_data, pdMS_TO_TICKS(50)) != pdTRUE)
        {
            LOGW(TAG, "night_transitions: failed to peek sensor_avg_queue");
        }
    }

    // Find state config index
    {
        char state_name[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name, STATE_NIGHT, sizeof(state_name) - 1);
        state_config_index = find_state_index_by_name_ci(state_name);
        if (state_config_index < 0)
        {
            LOGW(TAG, "night_transitions: state '%s' not configured", state_name);
            state_config_index = 0;
        }
    }

    // Read config under mutex
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        state_fanspeed = statemachine_config[state_config_index].fanspeed;
        minimum_state_time = statemachine_config[state_config_index].duration_minutes;
        co2highlevel = statemachine_gen_config.co2_high_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "night_transitions: could not take state_config_mutex; using defaults");
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_NIGHT;
        sm_actuals.fanspeed = state_fanspeed;
        valve_move_locked = sm_actuals.valve_lock;
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;

        for (int i = 0; i < co2_sensor_counter; ++i)
        {
            if (co2_sensors[i].co2_reading > co2highlevel)
                sm_actuals.co2_sensors_high++;
        }
        for (int i = 0; i < rh_sensor_counter; ++i)
        {
            if (rh_sensors[i].rh_reading > rhhighlevel)
                sm_actuals.rh_sensors_high++;
        }

        co2_sensors_high = sm_actuals.co2_sensors_high;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "night_transitions: could not take statemachine_mutex");
    }

    set_fanspeed(state_fanspeed);

    // Update elapsed time
    {
        int new_time = (int)(esp_timer_get_time() / 1000000ULL);
        if (new_time > old_time)
        {
            elapsed_time += new_time - old_time;
            old_time = new_time;
        }
    }

    // Move valves if allowed
    if (!valve_move_locked)
    {
        valve_position_statemachine(state_config_index);
    }
    else
    {
        LOGW(TAG, "night_transitions: valves locked; skipping move");
    }

    // Decide transitions
    if (co2_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHCO2NIGHT;
    }
    else if (rh_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHRHNIGHT;
    }
    else if (is_day())
    {
        new_state = STATE_NR_DAY;
    }
    else if (valve_cycle_times_night())
    {
        new_state = STATE_NR_CYCLINGNIGHT;
    }
    else
    {
        new_state = STATE_NR_NIGHT;
    }

    if (new_state != STATE_NR_NIGHT)
    {
        elapsed_time = 0;
        old_time = (int)(esp_timer_get_time() / 1000000ULL);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "night_transitions: could not update sm_actuals to new state %d", new_state);
    }
}

void high_co2_day_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    int state_config_index = -1;
    int state_config_index_temp = -1;
    int state_fanspeed = FANSPEED_LOW;
    int minimum_state_time = 0;
    int co2lowlevel = 0;
    int co2highlevel = 0;
    bool valve_move_locked = false;
    // int co2_sensors_high = 0;
    int state_valve_position[MAX_VALVE_COUNT] = {0};
    bool valve_open[MAX_VALVE_COUNT] = {false};

    if (!sensors_queues_ready || sensor_avg_queue == NULL)
    {
        LOGW(TAG, "high_co2_day_transitions: sensor queues not ready; skipping sensor-dependent logic");
    }
    else if (sensor_avg_item_size != sizeof(statemachine_avg_sensor_data))
    {
        LOGE(TAG, "high_co2_day_transitions: sensor_avg_item_size mismatch; skipping sensor-dependent logic");
    }
    else
    {
        if (xQueuePeek(sensor_avg_queue, &statemachine_avg_sensor_data, pdMS_TO_TICKS(50)) != pdTRUE)
        {
            LOGW(TAG, "high_co2_day_transitions: failed to peek sensor_avg_queue");
        }
    }

    // Find config indices
    {
        char state_name[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name, STATE_HIGHCO2DAY, sizeof(state_name) - 1);
        state_config_index = find_state_index_by_name_ci(state_name);
        if (state_config_index < 0)
        {
            LOGW(TAG, "high_co2_day_transitions: state '%s' not configured", state_name);
            state_config_index = 0;
        }

        char state_name_temp[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name_temp, STATE_TEMP, sizeof(state_name_temp) - 1);
        state_config_index_temp = find_state_index_by_name_ci(state_name_temp);
        if (state_config_index_temp < 0)
        {
            LOGW(TAG, "high_co2_day_transitions: temp state '%s' not configured", state_name_temp);
            state_config_index_temp = state_config_index; // fallback
        }
    }

    // Read config under mutex
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        state_fanspeed = statemachine_config[state_config_index].fanspeed;
        minimum_state_time = statemachine_config[state_config_index].duration_minutes;
        co2lowlevel = statemachine_gen_config.co2_low_level;
        co2highlevel = statemachine_gen_config.co2_high_level;

        // copy default valve positions for this state
        for (int i = 0; i < MAX_VALVE_COUNT; ++i)
            state_valve_position[i] = statemachine_config[state_config_index].valve_position[i];

        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_day_transitions: could not take state_config_mutex; using defaults");
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_HIGHCO2DAY;
        sm_actuals.fanspeed = state_fanspeed;
        valve_move_locked = sm_actuals.valve_lock;
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_day_transitions: could not take statemachine_mutex");
    }

    set_fanspeed(state_fanspeed);

    // Update elapsed time in the same way as the other state handlers
    {
        int new_time = (int)(esp_timer_get_time() / 1000000ULL);
        if (new_time > old_time)
        {
            elapsed_time += new_time - old_time;
            old_time = new_time;
        }
    }

    // Hysteresis per valve based on selected CO2 sensors
    for (int i = 0; i < co2_sensor_counter; ++i)
    {
        const char *vname = co2_sensors[i].valve ? co2_sensors[i].valve : "(unknown)";
        float reading = co2_sensors[i].co2_reading;
        char valve_copy[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(valve_copy, vname, sizeof(valve_copy) - 1);

        // Extract numeric valve index if present
        int valve_nr = -1;
        char *substr = strstr(valve_copy, "valve");
        if (substr)
        {
            char *num = substr + 5;
            valve_nr = atoi(num);
        }

        if (valve_nr < 0 || valve_nr >= MAX_VALVE_COUNT)
        {
            LOGW(TAG, "high_co2_day_transitions: invalid valve parsed from '%s'", vname);
            continue;
        }

        // Apply hysteresis
        if (valve_open[valve_nr])
        {
            if (reading < co2lowlevel)
            {
                valve_open[valve_nr] = false;
                state_valve_position[valve_nr] = 4;
            }
            else
            {
                state_valve_position[valve_nr] = 20;
            }
        }
        else
        {
            if (reading > co2highlevel)
            {
                valve_open[valve_nr] = true;
                state_valve_position[valve_nr] = 20;
            }
            else
            {
                state_valve_position[valve_nr] = 4;
            }
        }

        if (reading > co2lowlevel)
        {
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
            {
                sm_actuals.co2_sensors_high++;
                xSemaphoreGive(statemachine_mutex);
            }
        }
    }

    // Apply temporary valve positions to a temp state config (safe under mutex)
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; ++i)
            statemachine_config[state_config_index_temp].valve_position[i] = state_valve_position[i];
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_day_transitions: could not take state_config_mutex to write temp valve positions");
    }

    if (!valve_move_locked)
    {
        valve_position_statemachine(state_config_index_temp);
    }
    else
    {
        LOGW(TAG, "high_co2_day_transitions: valves locked; skipping move");
    }

    // Decide transition
    int new_state = STATE_NR_HIGHCO2DAY;
    if (sm_actuals.co2_sensors_high == 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_DAY;
    }
    else if (!is_day() && sm_actuals.co2_sensors_high > 0)
    {
        new_state = STATE_NR_HIGHCO2NIGHT;
    }

    if (new_state != STATE_NR_HIGHCO2DAY)
    {
        elapsed_time = 0;
        old_time = (int)(esp_timer_get_time() / 1000000ULL);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_day_transitions: could not update sm_actuals to new state %d", new_state);
    }
}

void high_co2_night_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    int state_config_index = -1;
    int state_config_index_temp = -1;
    int state_fanspeed = FANSPEED_LOW;
    int minimum_state_time = 0;
    int co2lowlevel = 0;
    int co2highlevel = 0;
    bool valve_move_locked = false;
    int co2_sensors_high = 0;
    int state_valve_position[MAX_VALVE_COUNT] = {0};
    bool valve_open[MAX_VALVE_COUNT] = {false};

    if (!sensors_queues_ready || sensor_avg_queue == NULL)
    {
        LOGW(TAG, "high_co2_night_transitions: sensor queues not ready; skipping sensor-dependent logic");
    }
    else if (sensor_avg_item_size != sizeof(statemachine_avg_sensor_data))
    {
        LOGE(TAG, "high_co2_night_transitions: sensor_avg_item_size mismatch; skipping sensor-dependent logic");
    }
    else
    {
        if (xQueuePeek(sensor_avg_queue, &statemachine_avg_sensor_data, pdMS_TO_TICKS(50)) != pdTRUE)
        {
            LOGW(TAG, "high_co2_night_transitions: failed to peek sensor_avg_queue");
        }
    }

    // Find config indices
    {
        char state_name[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name, STATE_HIGHCO2NIGHT, sizeof(state_name) - 1);
        state_config_index = find_state_index_by_name_ci(state_name);
        if (state_config_index < 0)
        {
            LOGW(TAG, "high_co2_night_transitions: state '%s' not configured", state_name);
            state_config_index = 0;
        }

        char state_name_temp[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name_temp, STATE_TEMP, sizeof(state_name_temp) - 1);
        state_config_index_temp = find_state_index_by_name_ci(state_name_temp);
        if (state_config_index_temp < 0)
        {
            LOGW(TAG, "high_co2_night_transitions: temp state '%s' not configured", state_name_temp);
            state_config_index_temp = state_config_index;
        }
    }

    // Read config under mutex
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        state_fanspeed = statemachine_config[state_config_index].fanspeed;
        minimum_state_time = statemachine_config[state_config_index].duration_minutes;
        co2lowlevel = statemachine_gen_config.co2_low_level;
        co2highlevel = statemachine_gen_config.co2_high_level;

        for (int i = 0; i < MAX_VALVE_COUNT; ++i)
            state_valve_position[i] = statemachine_config[state_config_index].valve_position[i];

        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_night_transitions: could not take state_config_mutex; using defaults");
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_HIGHCO2NIGHT;
        sm_actuals.fanspeed = state_fanspeed;
        valve_move_locked = sm_actuals.valve_lock;
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_night_transitions: could not take statemachine_mutex");
    }

    set_fanspeed(state_fanspeed);

    // Update elapsed time in the same way as the other state handlers
    {
        int new_time = (int)(esp_timer_get_time() / 1000000ULL);
        if (new_time > old_time)
        {
            elapsed_time += new_time - old_time;
            old_time = new_time;
        }
    }

    // Aggregate high flags and apply valve logic
    bool any_high = false;
    for (int i = 0; i < co2_sensor_counter; ++i)
    {
        float reading = co2_sensors[i].co2_reading;
        if (reading > co2highlevel)
            any_high = true;
        if (reading > co2lowlevel)
            co2_sensors_high++;
    }

    // Default behavior: if no sensor is high, open CO2 valves to 20
    for (int i = 0; i < co2_sensor_counter; ++i)
    {
        const char *vname = co2_sensors[i].valve ? co2_sensors[i].valve : "(unknown)";
        char valve_copy[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(valve_copy, vname, sizeof(valve_copy) - 1);

        char *substr = strstr(valve_copy, "valve");
        int valve_nr = -1;
        if (substr)
            valve_nr = atoi(substr + 5);

        if (valve_nr < 0 || valve_nr >= MAX_VALVE_COUNT)
        {
            LOGW(TAG, "high_co2_night_transitions: invalid valve parsed from '%s'", vname);
            continue;
        }

        float reading = co2_sensors[i].co2_reading;

        if (!any_high)
        {
            valve_open[valve_nr] = true;
            state_valve_position[valve_nr] = 20;
        }
        else
        {
            if (valve_open[valve_nr])
            {
                if (reading < co2lowlevel)
                {
                    valve_open[valve_nr] = false;
                    state_valve_position[valve_nr] = 4;
                }
                else
                {
                    state_valve_position[valve_nr] = 20;
                }
            }
            else
            {
                if (reading > co2highlevel)
                {
                    valve_open[valve_nr] = true;
                    state_valve_position[valve_nr] = 20;
                }
                else
                {
                    state_valve_position[valve_nr] = 4;
                }
            }
        }
    }

    // Write temp valve positions under mutex
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; ++i)
            statemachine_config[state_config_index_temp].valve_position[i] = state_valve_position[i];
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_night_transitions: could not take state_config_mutex to write temp valve positions");
    }

    if (!valve_move_locked)
    {
        valve_position_statemachine(state_config_index_temp);
    }
    else
    {
        LOGW(TAG, "high_co2_night_transitions: valves locked; skipping move");
    }

    // Decide transition
    int new_state = STATE_NR_HIGHCO2NIGHT;
    if (co2_sensors_high == 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_DAY;
    }
    else if (is_day() && co2_sensors_high > 0)
    {
        new_state = STATE_NR_HIGHCO2DAY;
    }

    if (new_state != STATE_NR_HIGHCO2NIGHT)
    {
        elapsed_time = 0;
        old_time = (int)(esp_timer_get_time() / 1000000ULL);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
    else
    {
        LOGW(TAG, "high_co2_night_transitions: could not update sm_actuals to new state %d", new_state);
    }
}

// High RH day transitions
void high_rh_day_transitions(void)
{
    float avg_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    int state_config_index = -1;
    int temp_state_index = -1;
    int state_fanspeed = FANSPEED_LOW;
    int minimum_state_time = 0;
    // int rh_low = 0;
    int rh_high = 0;
    bool valve_move_locked = false;
    int rh_sensors_high = 0;
    int new_state = STATE_NR_HIGHRHDAY;

    // Try to read averaged sensor data (non-fatal)
    if (sensors_queues_ready && sensor_avg_queue != NULL &&
        sensor_avg_item_size == sizeof(avg_data))
    {
        (void)xQueuePeek(sensor_avg_queue, &avg_data, pdMS_TO_TICKS(50));
    }

    // Find config indices
    {
        char state_name[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name, STATE_HIGHRHDAY, sizeof(state_name) - 1);
        state_config_index = find_state_index_by_name_ci(state_name);
        if (state_config_index < 0)
            state_config_index = 0;

        // temp state to apply valve positions if needed (reuse TEMP state)
        strncpy(state_name, STATE_TEMP, sizeof(state_name) - 1);
        temp_state_index = find_state_index_by_name_ci(state_name);
        if (temp_state_index < 0)
            temp_state_index = state_config_index;
    }

    // Read config under mutex
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        state_fanspeed = statemachine_config[state_config_index].fanspeed;
        minimum_state_time = statemachine_config[state_config_index].duration_minutes;
        // rh_low = statemachine_gen_config.rh_low_level;
        rh_high = statemachine_gen_config.rh_high_level;
        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_HIGHRHDAY;
        sm_actuals.fanspeed = state_fanspeed;
        valve_move_locked = sm_actuals.valve_lock;
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    // Count RH sensors above thresholds using selected list
    for (int i = 0; i < rh_sensor_counter; ++i)
    {
        float r = rh_sensors[i].rh_reading;
        if (!isfinite(r))
            r = 0.0f;
        if (r > rh_high)
        {
            rh_sensors_high++;
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
            {
                sm_actuals.rh_sensors_high++;
                xSemaphoreGive(statemachine_mutex);
            }
        }
    }

    // Basic valve behavior: if RH high, open valves for affected sensors (simple mapping)
    if (!valve_move_locked)
    {
        // Build temporary valve positions from state defaults
        int temp_positions[MAX_VALVE_COUNT];
        if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            for (int i = 0; i < MAX_VALVE_COUNT; ++i)
                temp_positions[i] = statemachine_config[state_config_index].valve_position[i];
            xSemaphoreGive(state_config_mutex);
        }
        // For each RH sensor that is high, try to open corresponding valve to a larger opening
        for (int i = 0; i < rh_sensor_counter; ++i)
        {
            const char *v = rh_sensors[i].valve ? rh_sensors[i].valve : "(unknown)";
            char copy[MEDIUM_CONFIG_ITEM] = {0};
            strncpy(copy, v, sizeof(copy) - 1);
            char *substr = strstr(copy, "valve");
            if (substr)
            {
                int vid = atoi(substr + 5);
                if (vid >= 0 && vid < MAX_VALVE_COUNT)
                {
                    temp_positions[vid] = 20; // open more for humidity control
                }
            }
        }
        // Write temp positions into TEMP state config and apply
        if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            for (int i = 0; i < MAX_VALVE_COUNT; ++i)
                statemachine_config[temp_state_index].valve_position[i] = temp_positions[i];
            xSemaphoreGive(state_config_mutex);
        }
        valve_position_statemachine(temp_state_index);
    }

    // Update elapsed time
    {
        int new_time = (int)(esp_timer_get_time() / 1000000ULL);
        if (new_time > old_time)
        {
            elapsed_time += new_time - old_time;
            old_time = new_time;
        }
    }

    // Decide transitions
    if (rh_sensors_high == 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_DAY;
    }
    else if (!is_day() && rh_sensors_high > 0)
    {
        new_state = STATE_NR_HIGHRHNIGHT;
    }
    else
    {
        new_state = STATE_NR_HIGHRHDAY;
    }

    if (new_state != STATE_NR_HIGHRHDAY)
    {
        elapsed_time = 0;
        old_time = (int)(esp_timer_get_time() / 1000000ULL);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

// High RH night transitions
void high_rh_night_transitions(void)
{
    float avg_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    int state_config_index = -1;
    int temp_state_index = -1;
    int state_fanspeed = FANSPEED_LOW;
    int minimum_state_time = 0;
    // int rh_low = 0;
    int rh_high = 0;
    bool valve_move_locked = false;
    int rh_sensors_high = 0;
    int new_state = STATE_NR_HIGHRHNIGHT;

    if (sensors_queues_ready && sensor_avg_queue != NULL &&
        sensor_avg_item_size == sizeof(avg_data))
    {
        (void)xQueuePeek(sensor_avg_queue, &avg_data, pdMS_TO_TICKS(50));
    }

    // Find config indices
    {
        char state_name[MEDIUM_CONFIG_ITEM] = {0};
        strncpy(state_name, STATE_HIGHRHNIGHT, sizeof(state_name) - 1);
        state_config_index = find_state_index_by_name_ci(state_name);
        if (state_config_index < 0)
            state_config_index = 0;

        strncpy(state_name, STATE_TEMP, sizeof(state_name) - 1);
        temp_state_index = find_state_index_by_name_ci(state_name);
        if (temp_state_index < 0)
            temp_state_index = state_config_index;
    }

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        state_fanspeed = statemachine_config[state_config_index].fanspeed;
        minimum_state_time = statemachine_config[state_config_index].duration_minutes;
        // rh_low = statemachine_gen_config.rh_low_level;
        rh_high = statemachine_gen_config.rh_high_level;
        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_HIGHRHNIGHT;
        sm_actuals.fanspeed = state_fanspeed;
        valve_move_locked = sm_actuals.valve_lock;
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    // Count RH sensors above thresholds
    for (int i = 0; i < rh_sensor_counter; ++i)
    {
        float r = rh_sensors[i].rh_reading;
        if (!isfinite(r))
            r = 0.0f;
        if (r > rh_high)
        {
            rh_sensors_high++;
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(20)) == pdTRUE)
            {
                sm_actuals.rh_sensors_high++;
                xSemaphoreGive(statemachine_mutex);
            }
        }
    }

    // Valve logic similar to day: open valves for high RH sensors
    if (!valve_move_locked)
    {
        int temp_positions[MAX_VALVE_COUNT];
        if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            for (int i = 0; i < MAX_VALVE_COUNT; ++i)
                temp_positions[i] = statemachine_config[state_config_index].valve_position[i];
            xSemaphoreGive(state_config_mutex);
        }
        for (int i = 0; i < rh_sensor_counter; ++i)
        {
            const char *v = rh_sensors[i].valve ? rh_sensors[i].valve : "(unknown)";
            char copy[MEDIUM_CONFIG_ITEM] = {0};
            strncpy(copy, v, sizeof(copy) - 1);
            char *substr = strstr(copy, "valve");
            if (substr)
            {
                int vid = atoi(substr + 5);
                if (vid >= 0 && vid < MAX_VALVE_COUNT)
                    temp_positions[vid] = 20;
            }
        }
        if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            for (int i = 0; i < MAX_VALVE_COUNT; ++i)
                statemachine_config[temp_state_index].valve_position[i] = temp_positions[i];
            xSemaphoreGive(state_config_mutex);
        }
        valve_position_statemachine(temp_state_index);
    }

    // Update elapsed time
    {
        int new_time = (int)(esp_timer_get_time() / 1000000ULL);
        if (new_time > old_time)
        {
            elapsed_time += new_time - old_time;
            old_time = new_time;
        }
    }

    // Decide transitions
    if (rh_sensors_high == 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_NIGHT;
    }
    else if (is_day() && rh_sensors_high > 0)
    {
        new_state = STATE_NR_HIGHRHDAY;
    }

    if (new_state != STATE_NR_HIGHRHNIGHT)
    {
        elapsed_time = 0;
        old_time = (int)(esp_timer_get_time() / 1000000ULL);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

// Cooking transitions
void cooking_transitions(void)
{
    int state_index = find_state_index_by_name_ci(STATE_COOKING);
    if (state_index < 0)
    {
        LOGW(TAG, "cooking_transitions: cooking state not configured");
        // fallback to day/night decision
        if (is_day())
            if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
            {
                sm_actuals.state = STATE_NR_DAY;
                xSemaphoreGive(statemachine_mutex);
            }
            else
            {
            }
        else if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.state = STATE_NR_NIGHT;
            xSemaphoreGive(statemachine_mutex);
        }
        return;
    }

    int state_fanspeed = FANSPEED_LOW;
    // int duration_min = 0;

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        state_fanspeed = statemachine_config[state_index].fanspeed;
        // duration_min = statemachine_config[state_index].duration_minutes;
        xSemaphoreGive(state_config_mutex);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_COOKING;
        sm_actuals.fanspeed = state_fanspeed;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    // Update elapsed time
    //{
    //  int new_time = (int)(esp_timer_get_time() / 1000000ULL);
    // if (new_time > old_time)
    // {
    //   elapsed_time += new_time - old_time;
    // old_time = new_time;
    // }
    //}

    // After duration expires, return to day or night depending on time
    if (!cooking_times())
    {
        int next = is_day() ? STATE_NR_DAY : STATE_NR_NIGHT;
        // elapsed_time = 0;
        // old_time = (int)(esp_timer_get_time() / 1000000ULL);
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.state = next;
            xSemaphoreGive(statemachine_mutex);
        }
    }
}

// Valve cycling day transitions (simple wrapper)
void valve_cycle_day_transitions(void)
{
    int idx = find_state_index_by_name_ci(STATE_CYCLINGDAY);
    if (idx < 0)
    {
        LOGW(TAG, "valve_cycle_day_transitions: state not configured");
        return;
    }

    // If valve cycling condition is true, apply the state's valve positions
    if (valve_cycle_times_day())
    {
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.state = STATE_NR_CYCLINGDAY;
            xSemaphoreGive(statemachine_mutex);
        }
        valve_position_statemachine(idx);
    }
    else
    {
        // Not time to cycle — return to day
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.state = STATE_NR_DAY;
            xSemaphoreGive(statemachine_mutex);
        }
    }
}

// Valve cycling night transitions (simple wrapper)
void valve_cycle_night_transitions(void)
{
    int idx = find_state_index_by_name_ci(STATE_CYCLINGNIGHT);
    if (idx < 0)
    {
        LOGW(TAG, "valve_cycle_night_transitions: state not configured");
        return;
    }

    if (valve_cycle_times_night())
    {
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.state = STATE_NR_CYCLINGNIGHT;
            xSemaphoreGive(statemachine_mutex);
        }
        valve_position_statemachine(idx);
    }
    else
    {
        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            sm_actuals.state = STATE_NR_NIGHT;
            xSemaphoreGive(statemachine_mutex);
        }
    }
}

// Manual high speed transitions (manual override)
void manual_high_speed_transitions(void)
{
    int idx = find_state_index_by_name_ci(STATE_MANUALHS);
    if (idx < 0)
    {
        LOGW(TAG, "manual_high_speed_transitions: manual state not configured");
        return;
    }

    // Set fanspeed to high and keep valves as configured for manual state
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_MANUALHS;
        sm_actuals.fanspeed = FANSPEED_HIGH;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(FANSPEED_HIGH);
    valve_position_statemachine(idx);

    // Manual mode remains until external command clears it (handled elsewhere)
}
