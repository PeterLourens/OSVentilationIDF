/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "statemachine_helper.h"

static const char *TAG = "sm_helpers";

// Find state index by name (case-insensitive). Returns -1 if not found.
int find_state_index_by_name_ci(const char *name)
{
    if (!name)
        return -1;

    for (int i = 0; i < MAX_NR_STATES; ++i)
    {
        const char *s = statemachine_config[i].state_name;
        if (!s)
            continue;
        if (strcasecmp(s, name) == 0)
            return i;
    }
    return -1;
}

// Return true if current time falls into configured "day" window.
bool is_day(void)
{
    int wday_d_h = 0, wday_d_m = 0, wday_n_h = 0, wday_n_m = 0;
    int wkend_d_h = 0, wkend_d_m = 0, wkend_n_h = 0, wkend_n_m = 0;

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        wday_d_h = statemachine_gen_config.wday_d_h_start;
        wday_d_m = statemachine_gen_config.wday_d_m_start;
        wday_n_h = statemachine_gen_config.wday_n_h_start;
        wday_n_m = statemachine_gen_config.wday_n_m_start;
        wkend_d_h = statemachine_gen_config.wkend_d_h_start;
        wkend_d_m = statemachine_gen_config.wkend_d_m_start;
        wkend_n_h = statemachine_gen_config.wkend_n_h_start;
        wkend_n_m = statemachine_gen_config.wkend_n_m_start;
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "is_day: state_config_mutex unavailable, using defaults");
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int current_minutes = tm_now.tm_hour * 60 + tm_now.tm_min;

    if (is_weekend())
    {
        int day_start = wkend_d_h * 60 + wkend_d_m;
        int night_start = wkend_n_h * 60 + wkend_n_m;
        // If start == stop, treat as full-day
        if (day_start == night_start)
            return true;
        if (day_start < night_start)
            return (current_minutes >= day_start && current_minutes < night_start);
        // wrapping window
        return (current_minutes >= day_start || current_minutes < night_start);
    }
    else
    {
        int day_start = wday_d_h * 60 + wday_d_m;
        int night_start = wday_n_h * 60 + wday_n_m;
        if (day_start == night_start)
            return true;
        if (day_start < night_start)
            return (current_minutes >= day_start && current_minutes < night_start);
        return (current_minutes >= day_start || current_minutes < night_start);
    }
}

// Return true if today matches configured weekend days
bool is_weekend(void)
{
    char wk1[SMALL_CONFIG_ITEM] = {0};
    char wk2[SMALL_CONFIG_ITEM] = {0};
    char today[SMALL_CONFIG_ITEM] = {0};

    formatted_day(today, sizeof(today)); // e.g., "Mon", "Tue", or localized string

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        strncpy(wk1, statemachine_gen_config.wkend_d1, sizeof(wk1) - 1);
        strncpy(wk2, statemachine_gen_config.wkend_d2, sizeof(wk2) - 1);
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "is_weekend: state_config_mutex unavailable, assuming not weekend");
        return false;
    }

    // Compare strings exactly (config expected to match formatted_day output)
    if (wk1[0] != '\0' && strcmp(today, wk1) == 0)
        return true;
    if (wk2[0] != '\0' && strcmp(today, wk2) == 0)
        return true;
    return false;
}

// Return true if current time is within the cooking state window
bool cooking_times(void)
{
    int idx = find_state_index_by_name_ci(STATE_COOKING);
    if (idx < 0)
    {
        LOGD(TAG, "cooking_times: state not configured");
        return false;
    }

    int start_h = 0, start_m = 0, stop_h = 0, stop_m = 0;
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        start_h = statemachine_config[idx].state_start_hour;
        start_m = statemachine_config[idx].state_start_minute;
        stop_h = statemachine_config[idx].state_end_hour;
        stop_m = statemachine_config[idx].state_end_minute;
        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGW(TAG, "cooking_times: state_config_mutex unavailable");
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);

    int cur_h = tm_now.tm_hour;
    int cur_m = tm_now.tm_min;

    // If start == stop -> full day
    if (start_h == stop_h && start_m == stop_m)
        return true;

    if (cur_h == start_h && cur_m > start_m)
        return true;
    if (cur_h == stop_h && cur_m >= stop_m)
        return false;

    return false;
}

// Helper: compute overlap minutes between two windows [a_start,a_stop) and [b_start,b_stop)
static int minutes_overlap(int a_start, int a_stop, int b_start, int b_stop)
{
    int total = 0;

    int a_s[2], a_e[2], a_cnt = 0;
    if (a_start == a_stop)
    {
        a_s[0] = 0;
        a_e[0] = 1440;
        a_cnt = 1;
    }
    else if (a_start < a_stop)
    {
        a_s[0] = a_start;
        a_e[0] = a_stop;
        a_cnt = 1;
    }
    else
    {
        a_s[0] = a_start;
        a_e[0] = 1440;
        a_s[1] = 0;
        a_e[1] = a_stop;
        a_cnt = 2;
    }

    int b_s[2], b_e[2], b_cnt = 0;
    if (b_start == b_stop)
    {
        b_s[0] = 0;
        b_e[0] = 1440;
        b_cnt = 1;
    }
    else if (b_start < b_stop)
    {
        b_s[0] = b_start;
        b_e[0] = b_stop;
        b_cnt = 1;
    }
    else
    {
        b_s[0] = b_start;
        b_e[0] = 1440;
        b_s[1] = 0;
        b_e[1] = b_stop;
        b_cnt = 2;
    }

    for (int i = 0; i < a_cnt; ++i)
    {
        for (int j = 0; j < b_cnt; ++j)
        {
            int ov_s = (a_s[i] > b_s[j]) ? a_s[i] : b_s[j];
            int ov_e = (a_e[i] < b_e[j]) ? a_e[i] : b_e[j];
            if (ov_e > ov_s)
                total += ov_e - ov_s;
        }
    }

    return total;
}

// Determine whether cycling should run during the day window
bool valve_cycle_times_day(void)
{
    int idx = find_state_index_by_name_ci(STATE_CYCLINGDAY);
    if (idx < 0)
    {
        LOGE(TAG, "valve_cycle_times_day: state not configured");
        return false;
    }

    int start_h, start_m, stop_h, stop_m, interval_min, duration_min;
    int wday_d_h, wday_d_m, wday_n_h, wday_n_m;
    int wkend_d_h, wkend_d_m, wkend_n_h, wkend_n_m;

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        start_h = statemachine_config[idx].state_start_hour;
        start_m = statemachine_config[idx].state_start_minute;
        stop_h = statemachine_config[idx].state_end_hour;
        stop_m = statemachine_config[idx].state_end_minute;
        interval_min = statemachine_config[idx].cycling_interval_min;
        duration_min = statemachine_config[idx].duration_minutes;

        wday_d_h = statemachine_gen_config.wday_d_h_start;
        wday_d_m = statemachine_gen_config.wday_d_m_start;
        wday_n_h = statemachine_gen_config.wday_n_h_start;
        wday_n_m = statemachine_gen_config.wday_n_m_start;

        wkend_d_h = statemachine_gen_config.wkend_d_h_start;
        wkend_d_m = statemachine_gen_config.wkend_d_m_start;
        wkend_n_h = statemachine_gen_config.wkend_n_h_start;
        wkend_n_m = statemachine_gen_config.wkend_n_m_start;

        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGE(TAG, "valve_cycle_times_day: could not take state_config_mutex");
        return false;
    }

    if (interval_min < 30 || duration_min <= 5)
    {
        LOGD(TAG, "valve_cycle_times_day: invalid interval/duration interval=%d duration=%d", interval_min, duration_min);
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int current = tm_now.tm_hour * 60 + tm_now.tm_min;
    int start = start_h * 60 + start_m;
    int stop = stop_h * 60 + stop_m;

    // Check if current is inside the configured cycling window
    bool in_window;
    if (start == stop)
        in_window = true;
    else if (start < stop)
        in_window = (current >= start && current < stop);
    else
        in_window = (current >= start || current < stop);

    if (!in_window)
        return false;

    int day_start, day_stop;
    if (is_weekend())
    {
        day_start = wkend_d_h * 60 + wkend_d_m;
        day_stop = wkend_n_h * 60 + wkend_n_m;
    }
    else
    {
        day_start = wday_d_h * 60 + wday_d_m;
        day_stop = wday_n_h * 60 + wday_n_m;
    }

    int overlap = minutes_overlap(start, stop, day_start, day_stop);
    if (overlap <= 0)
        return false;

    int max_cycles = ((overlap - 1) / interval_min) + 1;
    if (max_cycles <= 0)
        return false;

    int elapsed = (current >= start) ? (current - start) : (current + 1440 - start);
    int cycle_index = elapsed / interval_min;
    if (cycle_index >= max_cycles)
        return false;

    int elapsed_mod = elapsed % interval_min;
    return (elapsed_mod < duration_min);
}

// Determine whether cycling should run during the night window
bool valve_cycle_times_night(void)
{
    int idx = find_state_index_by_name_ci(STATE_CYCLINGNIGHT);
    if (idx < 0)
    {
        LOGE(TAG, "valve_cycle_times_night: state not configured");
        return false;
    }

    int start_h, start_m, stop_h, stop_m, interval_min, duration_min;
    int wday_d_h, wday_d_m, wday_n_h, wday_n_m;
    int wkend_d_h, wkend_d_m, wkend_n_h, wkend_n_m;

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        start_h = statemachine_config[idx].state_start_hour;
        start_m = statemachine_config[idx].state_start_minute;
        stop_h = statemachine_config[idx].state_end_hour;
        stop_m = statemachine_config[idx].state_end_minute;
        interval_min = statemachine_config[idx].cycling_interval_min;
        duration_min = statemachine_config[idx].duration_minutes;

        wday_d_h = statemachine_gen_config.wday_d_h_start;
        wday_d_m = statemachine_gen_config.wday_d_m_start;
        wday_n_h = statemachine_gen_config.wday_n_h_start;
        wday_n_m = statemachine_gen_config.wday_n_m_start;

        wkend_d_h = statemachine_gen_config.wkend_d_h_start;
        wkend_d_m = statemachine_gen_config.wkend_d_m_start;
        wkend_n_h = statemachine_gen_config.wkend_n_h_start;
        wkend_n_m = statemachine_gen_config.wkend_n_m_start;

        xSemaphoreGive(state_config_mutex);
    }
    else
    {
        LOGE(TAG, "valve_cycle_times_night: could not take state_config_mutex");
        return false;
    }

    if (interval_min < 30 || duration_min <= 5)
    {
        LOGD(TAG, "valve_cycle_times_night: invalid interval/duration interval=%d duration=%d", interval_min, duration_min);
        return false;
    }

    time_t now = time(NULL);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    int current = tm_now.tm_hour * 60 + tm_now.tm_min;
    int start = start_h * 60 + start_m;
    int stop = stop_h * 60 + stop_m;

    bool in_window;
    if (start == stop)
        in_window = true;
    else if (start < stop)
        in_window = (current >= start && current < stop);
    else
        in_window = (current >= start || current < stop);

    if (!in_window)
        return false;

    int night_start, night_stop;
    if (is_weekend())
    {
        night_start = wkend_n_h * 60 + wkend_n_m;
        night_stop = wkend_d_h * 60 + wkend_d_m;
    }
    else
    {
        night_start = wday_n_h * 60 + wday_n_m;
        night_stop = wday_d_h * 60 + wday_d_m;
    }

    int overlap = minutes_overlap(start, stop, night_start, night_stop);
    if (overlap <= 0)
        return false;

    int max_cycles = ((overlap - 1) / interval_min) + 1;
    if (max_cycles <= 0)
        return false;

    int elapsed = (current >= start) ? (current - start) : (current + 1440 - start);
    int cycle_index = elapsed / interval_min;
    if (cycle_index >= max_cycles)
        return false;

    int elapsed_mod = elapsed % interval_min;
    return (elapsed_mod < duration_min);
}

// Safe string helper
static inline const char *safe_str(const char *s)
{
    return (s && s[0] != '\0') ? s : "(null)";
}

// Select sensors for CO2 and RH lists using averaged data
void select_sensors(void)
{
    int j = 0; // CO2 counter
    int k = 0; // RH counter

    co2_sensor_counter = 0;
    rh_sensor_counter = 0;

    // Ensure queue is ready and item size matches
    if (!sensors_queues_ready || sensor_avg_queue == NULL)
    {
        LOGW(TAG, "select_sensors: sensor_avg_queue not ready");
        return;
    }
    if (sensor_avg_item_size != sizeof(float[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]))
    {
        LOGW(TAG, "select_sensors: sensor_avg_item_size mismatch");
        return;
    }

    float sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];
    memset(sensor_data, 0, sizeof(sensor_data));

    if (xQueuePeek(sensor_avg_queue, sensor_data, pdMS_TO_TICKS(50)) != pdTRUE)
    {
        LOGW(TAG, "select_sensors: failed to peek sensor_avg_queue");
        return;
    }

    for (int i = 0; i < CHANNELS_PER_BUS; ++i)
    {
        const bus_sensor_t *s0 = NULL;
        const bus_sensor_t *s1 = NULL;

        if (sensor_cfg_mutex && xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
        {
            s0 = &bus0_sensor_settings[i];
            s1 = &bus1_sensor_settings[i];
            xSemaphoreGive(sensor_cfg_mutex);
        }
        else
        {
            LOGD(TAG, "select_sensors: sensor_cfg_mutex unavailable; using defaults");
        }

        // Bus 0 CO2
        if (s0 && s0->co2_enabled && j < MAX_SENSORS)
        {
            float reading = sensor_data[0][i][IDX_CO2];
            if (!isfinite(reading))
                reading = 0.0f;
            co2_sensors[j].valve = (char *)safe_str(s0->valve);
            co2_sensors[j].co2_reading = reading;
            LOGI(TAG, "CO2 sensor: %s = %.2f", safe_str(s0->valve), reading);
            ++j;
        }

        // Bus 1 CO2
        if (s1 && s1->co2_enabled && j < MAX_SENSORS)
        {
            float reading = sensor_data[1][i][IDX_CO2];
            if (!isfinite(reading))
                reading = 0.0f;
            co2_sensors[j].valve = (char *)safe_str(s1->valve);
            co2_sensors[j].co2_reading = reading;
            LOGI(TAG, "CO2 sensor: %s = %.2f", safe_str(s1->valve), reading);
            ++j;
        }

        // Bus 0 RH
        if (s0 && s0->rh_enabled && k < MAX_SENSORS)
        {
            float reading = sensor_data[0][i][IDX_HUMIDITY];
            if (!isfinite(reading))
                reading = 0.0f;
            rh_sensors[k].valve = (char *)safe_str(s0->valve);
            rh_sensors[k].rh_reading = reading;
            LOGI(TAG, "RH sensor: %s = %.2f", safe_str(s0->valve), reading);
            ++k;
        }

        // Bus 1 RH
        if (s1 && s1->rh_enabled && k < MAX_SENSORS)
        {
            float reading = sensor_data[1][i][IDX_HUMIDITY];
            if (!isfinite(reading))
                reading = 0.0f;
            rh_sensors[k].valve = (char *)safe_str(s1->valve);
            rh_sensors[k].rh_reading = reading;
            LOGI(TAG, "RH sensor: %s = %.2f", safe_str(s1->valve), reading);
            ++k;
        }
    }

    co2_sensor_counter = j;
    rh_sensor_counter = k;
}

// Consume and clear a POST-requested valve move flag (thread-safe)
bool statemachine_consume_valve_move_request(void)
{
    bool rv = false;
    if (valve_control_data_mutex && xSemaphoreTake(valve_control_data_mutex, pdMS_TO_TICKS(50)) == pdTRUE)
    {
        rv = valvecontroldata.valve_move_request_from_post;
        valvecontroldata.valve_move_request_from_post = false;
        xSemaphoreGive(valve_control_data_mutex);
    }
    else
    {
        LOGW(TAG, "statemachine_consume_valve_move_request: could not take mutex");
        rv = false;
    }
    return rv;
}
