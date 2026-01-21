#include "statemachine_helper.h"
#include "statemachine.h"
#include "globals.h"

static const char *TAG = "StatemachineHelper";

int find_state_index_by_name_ci(const char *name)
{
    if (!name)
        return -1;

    for (int i = 0; i < MAX_NR_STATES; i++)
    {
        if (strcasecmp(statemachine_config[i].state_name, name) == 0)
            return i;
    }
    return -1;
}

bool is_day(void)
{
    int weekday_day_hour_start = 0;
    int weekday_day_minute_start = 0;
    int weekday_night_hour_start = 0;
    int weekday_night_minute_start = 0;
    int weekend_day_hour_start = 0;
    int weekend_day_minute_start = 0;
    int weekend_night_hour_start = 0;
    int weekend_night_minute_start = 0;
    int current_minutes = 0;

    struct tm temp_time;
    time_t now = 0;

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10))
    {
        weekday_day_hour_start = statemachine_gen_config.wday_d_h_start;
        weekday_day_minute_start = statemachine_gen_config.wday_d_m_start;
        weekday_night_hour_start = statemachine_gen_config.wday_n_h_start;
        weekday_night_minute_start = statemachine_gen_config.wday_n_m_start;
        weekend_day_hour_start = statemachine_gen_config.wkend_d_h_start;
        weekend_day_minute_start = statemachine_gen_config.wkend_d_m_start;
        weekend_night_hour_start = statemachine_gen_config.wkend_n_h_start;
        weekend_night_minute_start = statemachine_gen_config.wkend_n_m_start;
        xSemaphoreGive(state_config_mutex);
    }

    time(&now);
    localtime_r(&now, &temp_time);

    current_minutes = temp_time.tm_hour * 60 + temp_time.tm_min;

    // Debug
    // ESP_LOGI(TAG, "Weekday: day h start is %d and day start min is %d", weekday_day_hour_start, weekday_day_minute_start);
    // ESP_LOGI(TAG, "Weekend: day h start is %d and day start min is %d", weekend_day_hour_start, weekend_day_minute_start);
    // ESP_LOGI(TAG, "Actual time: %d - %d", temp_time.tm_hour, temp_time.tm_min);

    if (is_weekend())
    {
        int day_start = weekend_day_hour_start * 60 + weekend_day_minute_start;
        int night_start = weekend_night_hour_start * 60 + weekend_night_minute_start;
        // ESP_LOGI(TAG, "Current minutes is: %d, day start: %d, night_start: %d", current_minutes, day_start, night_start);
        return current_minutes >= day_start && current_minutes < night_start;
    }
    else
    {
        int day_start = weekday_day_hour_start * 60 + weekday_day_minute_start;
        int night_start = weekday_night_hour_start * 60 + weekday_night_minute_start;
        // ESP_LOGI(TAG, "Current minutes is: %d, day start: %d, night_start: %d", current_minutes, day_start, night_start);
        return current_minutes >= day_start && current_minutes < night_start;
    }
}

bool is_weekend(void)
{
    char day_buffer1[SMALL_CONFIG_ITEM] = {};
    char day_buffer2[SMALL_CONFIG_ITEM] = {};
    char day_now[SMALL_CONFIG_ITEM] = {};

    formatted_day(day_now, sizeof(day_now));

    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10))
    {
        strncpy(day_buffer1, statemachine_gen_config.wkend_d1, sizeof(day_buffer1));
        strncpy(day_buffer2, statemachine_gen_config.wkend_d2, sizeof(day_buffer2));
        xSemaphoreGive(state_config_mutex);
    }

    // Debug
    // ESP_LOGI(TAG, "Weekend days from settings are %s and %s", day_buffer1, day_buffer2);
    // ESP_LOGI(TAG, "Today is: %s", day_now);

    if (strcmp(day_now, day_buffer1) == 0 || strcmp(day_now, day_buffer2) == 0)
    {
        return true;
    }
    else
    {
        return false;
    }
}

bool cooking_times(void)
{
    int start_hour = 0;
    int start_min = 0;
    int stop_hour = 0;
    int stop_min = 0;
    int state_config_index = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    struct tm temp_time;
    time_t now = 0;

    time(&now);
    localtime_r(&now, &temp_time);

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_COOKING, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);
    ESP_LOGI(TAG, "State name is: %s, State index: %d", state_name, state_config_index);

    if (state_config_index < 0)
    {
        ESP_LOGI(TAG, "Invalid state index (<0). Returning false.");
        return false;
    }

    // Get state config setting when cooking is required
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)50) == pdTRUE)
        {
            start_hour = statemachine_config[state_config_index].state_start_hour;
            start_min = statemachine_config[state_config_index].state_start_minute;
            stop_hour = statemachine_config[state_config_index].state_end_hour;
            stop_min = statemachine_config[state_config_index].state_end_minute;
            xSemaphoreGive(state_config_mutex);
        }
    }

    ESP_LOGI(TAG, "Config for '%s': start=%02d:%02d stop=%02d:%02d", state_name, start_hour, start_min, stop_hour, stop_min);

    if (temp_time.tm_hour == start_hour && temp_time.tm_min > start_min)
    {
        ESP_LOGI(TAG, "Condition met: hour==start_hour && min>start_min -> cooking = true");
        return true;
    }
    else if (temp_time.tm_hour == stop_hour && temp_time.tm_min >= stop_min)
    {
        ESP_LOGI(TAG, "Condition met: hour==stop_hour && min>=stop_min -> cooking = false");
        return false;
    }
    else
    {
        ESP_LOGI(TAG, "No conditions met -> cooking = false");
        return false;
    }
}

// Helper: compute overlap minutes between two windows [a_start,a_stop) and [b_start,b_stop)
static int minutes_overlap(int a_start, int a_stop, int b_start, int b_stop)
{
    int total = 0;

    // Expand a into 1 or 2 ranges
    int a1s, a1e, a2s, a2e, a_cnt = 0;
    if (a_start == a_stop)
    {
        a1s = 0;
        a1e = 1440;
        a_cnt = 1;
    }
    else if (a_start < a_stop)
    {
        a1s = a_start;
        a1e = a_stop;
        a_cnt = 1;
    }
    else
    {
        a1s = a_start;
        a1e = 1440;
        a2s = 0;
        a2e = a_stop;
        a_cnt = 2;
    }

    // Expand b into 1 or 2 ranges
    int b1s, b1e, b2s, b2e, b_cnt = 0;
    if (b_start == b_stop)
    {
        b1s = 0;
        b1e = 1440;
        b_cnt = 1;
    }
    else if (b_start < b_stop)
    {
        b1s = b_start;
        b1e = b_stop;
        b_cnt = 1;
    }
    else
    {
        b1s = b_start;
        b1e = 1440;
        b2s = 0;
        b2e = b_stop;
        b_cnt = 2;
    }

    // Compare all range pairs
    int a_s[2], a_e[2], b_s[2], b_e[2];
    if (a_cnt == 1)
    {
        a_s[0] = a1s;
        a_e[0] = a1e;
    }
    else
    {
        a_s[0] = a1s;
        a_e[0] = a1e;
        a_s[1] = a2s;
        a_e[1] = a2e;
    }
    if (b_cnt == 1)
    {
        b_s[0] = b1s;
        b_e[0] = b1e;
    }
    else
    {
        b_s[0] = b1s;
        b_e[0] = b1e;
        b_s[1] = b2s;
        b_e[1] = b2e;
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

bool valve_cycle_times_day(void)
{
    int start_hour = 0;
    int start_min = 0;
    int stop_hour = 0;
    int stop_min = 0;
    int state_config_index = 0;
    int cycling_interval_min = 0;
    int duration_min = 0;

    // Day window config
    int wday_d_h = 0, wday_d_m = 0, wday_n_h = 0, wday_n_m = 0;
    int wkend_d_h = 0, wkend_d_m = 0, wkend_n_h = 0, wkend_n_m = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    struct tm temp_time;
    time_t now = 0;

    time(&now);
    localtime_r(&now, &temp_time);

    // Get state config index for cyclingday
    strncpy(state_name, STATE_CYCLINGDAY, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);
    ESP_LOGD(TAG, "State name is: %s, State index: %d", state_name, state_config_index);

    if (state_config_index < 0)
    {
        ESP_LOGD(TAG, "Invalid state index (<0). Returning false.");
        return false;
    }

    // Read config values and general day/night config under mutex
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)50) == pdTRUE)
        {
            start_hour = statemachine_config[state_config_index].state_start_hour;
            start_min = statemachine_config[state_config_index].state_start_minute;
            stop_hour = statemachine_config[state_config_index].state_end_hour;
            stop_min = statemachine_config[state_config_index].state_end_minute;
            cycling_interval_min = statemachine_config[state_config_index].cycling_interval_min;
            duration_min = statemachine_config[state_config_index].duration_minutes;

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
            // Could not take mutex — be conservative and do not trigger cycling
            ESP_LOGE(TAG, "Could not take state_config_mutex");
            return false;
        }
    }

    else
    {
        // No mutex available — cannot safely read config
        ESP_LOGE(TAG, "state_config_mutex == NULL");
        return false;
    }

    ESP_LOGD(TAG, "Config '%s': start=%02d:%02d stop=%02d:%02d, interval=%d min, duration=%d min", state_name, start_hour, start_min, stop_hour, stop_min, cycling_interval_min, duration_min);
    ESP_LOGD(TAG, "General day/night config: WD day=%02d:%02d WD night=%02d:%02d | WE day=%02d:%02d WE night=%02d:%02d", wday_d_h, wday_d_m, wday_n_h, wday_n_m, wkend_d_h, wkend_d_m, wkend_n_h, wkend_n_m);

    // Enforce minimum interval of 30 minutes
    if (cycling_interval_min < 30)
    {
        ESP_LOGD(TAG, "Invalid cycling interval (%d) <= 30. Returning false.", cycling_interval_min);
        return false;
        ;
    }

    // Validate duration
    if (duration_min <= 5)
    {
        ESP_LOGD(TAG, "Invalid duration (%d) <= 5. Returning false.", duration_min);
        return false;
    }

    int start = start_hour * 60 + start_min;
    int stop = stop_hour * 60 + stop_min;
    int current = temp_time.tm_hour * 60 + temp_time.tm_min;
    ESP_LOGD(TAG, "Window minutes: start=%d stop=%d current=%d", start, stop, current);

    // If not in cycling window return false
    bool in_window = false;
    if (start == stop)
    {
        ESP_LOGD(TAG, "Start == stop -> window spans full day. in_window=true");
        in_window = true;
    }
    else if (start < stop)
    {

        ESP_LOGD(TAG, "Non-wrapping window [%d, %d): in_window=%s", start, stop, in_window ? "true" : "false");
        in_window = (current >= start && current < stop);
    }
    else
    {
        ESP_LOGD(TAG, "Wrapping window [%d, 1440) U [0, %d): in_window=%s", start, stop, in_window ? "true" : "false");
        in_window = (current >= start || current < stop);
    }
    if (!in_window)
    {
        ESP_LOGD(TAG, "Current time not in cycling window. Returning false.");
        return false;
    }

    // Determine day window (weekday or weekend)
    int day_start, day_stop;
    if (is_weekend())
    {
        day_start = wkend_d_h * 60 + wkend_d_m;
        day_stop = wkend_n_h * 60 + wkend_n_m;
        ESP_LOGD(TAG, "Weekend: day window [%d, %d)", day_start, day_stop);
    }
    else
    {
        day_start = wday_d_h * 60 + wday_d_m;
        day_stop = wday_n_h * 60 + wday_n_m;
        ESP_LOGD(TAG, "Weekday: day window [%d, %d)", day_start, day_stop);
    }

    int overlap_minutes = minutes_overlap(start, stop, day_start, day_stop);
    ESP_LOGD(TAG, "Overlap between cycle window and day window: %d minutes", overlap_minutes);

    if (overlap_minutes <= 0)
    {
        ESP_LOGD(TAG, "No overlap with day window. Returning false.");
        return false;
    }

    // Compute maximum allowed cycles within overlap (ceil-like for first cycle at start)
    int max_cycles = (overlap_minutes <= 0) ? 0 : ((overlap_minutes - 1) / cycling_interval_min) + 1;
    ESP_LOGD(TAG, "Max cycles within overlap: %d (interval=%d, overlap=%d)", max_cycles, cycling_interval_min, overlap_minutes);

    if (max_cycles <= 0)
    {
        ESP_LOGD(TAG, "max_cycles <= 0. Returning false.");
        return false;
    }

    // Compute elapsed minutes since cycling start (wrap-aware)
    int elapsed = (current >= start) ? (current - start) : (current + 1440 - start);
    ESP_LOGD(TAG, "Elapsed since start: %d minutes", elapsed);

    // Determine which cycle index we're in
    int cycle_index = elapsed / cycling_interval_min;
    ESP_LOGD(TAG, "cycle_index=%d (elapsed=%d, interval=%d)", cycle_index, elapsed, cycling_interval_min);

    if (cycle_index >= max_cycles)
    {
        ESP_LOGD(TAG, "cycle_index >= max_cycles. Returning false.");
        return false;
    }

    int elapsed_mod = elapsed % cycling_interval_min;
    if (elapsed_mod < duration_min)
    {
        ESP_LOGD(TAG, "elapsed_mod=%d, duration=%d", elapsed_mod, duration_min);
        return true;
    }

    return false;
}

bool valve_cycle_times_night(void)
{
    int start_hour = 0;
    int start_min = 0;
    int stop_hour = 0;
    int stop_min = 0;
    int state_config_index = 0;
    int cycling_interval_min = 0;
    int duration_min = 0;

    // Night window config
    int wday_d_h = 0, wday_d_m = 0, wday_n_h = 0, wday_n_m = 0;
    int wkend_d_h = 0, wkend_d_m = 0, wkend_n_h = 0, wkend_n_m = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    struct tm temp_time;
    time_t now = 0;

    time(&now);
    localtime_r(&now, &temp_time);

    // Get state config index for cyclingnight
    strncpy(state_name, STATE_CYCLINGNIGHT, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    if (state_config_index < 0)
    {
        ESP_LOGD(TAG, "Invalid state index (<0). Returning false.");
        return false;
    }

    // Read config values and general day/night config under mutex
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)50) == pdTRUE)
        {
            start_hour = statemachine_config[state_config_index].state_start_hour;
            start_min = statemachine_config[state_config_index].state_start_minute;
            stop_hour = statemachine_config[state_config_index].state_end_hour;
            stop_min = statemachine_config[state_config_index].state_end_minute;
            cycling_interval_min = statemachine_config[state_config_index].cycling_interval_min;
            duration_min = statemachine_config[state_config_index].duration_minutes;

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
            // Could not take mutex — be conservative and do not trigger cycling
            ESP_LOGE(TAG, "Could not take state_config_mutex");
            return false;
        }
    }
    else
    {
        // No mutex available — cannot safely read config
        ESP_LOGE(TAG, "state_config_mutex == NULL");
        return false;
    }

    ESP_LOGD(TAG, "Config '%s': start=%02d:%02d stop=%02d:%02d, interval=%d min, duration=%d min", state_name, start_hour, start_min, stop_hour, stop_min, cycling_interval_min, duration_min);
    ESP_LOGD(TAG, "General day/night config: WD day=%02d:%02d WD night=%02d:%02d | WE day=%02d:%02d WE night=%02d:%02d", wday_d_h, wday_d_m, wday_n_h, wday_n_m, wkend_d_h, wkend_d_m, wkend_n_h, wkend_n_m);

    // Enforce minimum interval of 30 minutes
    if (cycling_interval_min < 30)
    {
        ESP_LOGD(TAG, "Invalid cycling interval (%d) <= 30. Returning false.", cycling_interval_min);
        return false;
    }

    // Validate duration
    if (duration_min <= 5)
    {
        ESP_LOGD(TAG, "Invalid duration (%d) <= 5. Returning false.", duration_min);
        return false;
    }

    int start = start_hour * 60 + start_min;
    int stop = stop_hour * 60 + stop_min;
    int current = temp_time.tm_hour * 60 + temp_time.tm_min;
    ESP_LOGD(TAG, "Window minutes: start=%d stop=%d current=%d", start, stop, current);

    // If not in cycling window return false
    bool in_window = false;
    if (start == stop)
    {
        ESP_LOGD(TAG, "Start == stop -> window spans full day. in_window=true");
        in_window = true;
    }
    else if (start < stop)
    {
        in_window = (current >= start && current < stop);
        ESP_LOGD(TAG, "Non-wrapping window [%d, %d): in_window=%s", start, stop, in_window ? "true" : "false");
    }
    else
    {
        in_window = (current >= start || current < stop);
        ESP_LOGD(TAG, "Wrapping window [%d, 1440) U [0, %d): in_window=%s", start, stop, in_window ? "true" : "false");
    }
    if (!in_window)
    {
        ESP_LOGD(TAG, "Current time not in cycling window. Returning false.");
        return false;
    }

    // Determine night window (weekday or weekend). Night window is [night_start, next day start)
    int night_start, night_stop;
    if (is_weekend())
    {
        night_start = wkend_n_h * 60 + wkend_n_m;
        night_stop = wkend_d_h * 60 + wkend_d_m;
        ESP_LOGD(TAG, "Weekend: night window [%d, %d)", night_start, night_stop);
    }
    else
    {
        night_start = wday_n_h * 60 + wday_n_m;
        night_stop = wday_d_h * 60 + wday_d_m;
        ESP_LOGD(TAG, "Weekday: night window [%d, %d)", night_start, night_stop);
    }

    int overlap_minutes = minutes_overlap(start, stop, night_start, night_stop);

    if (overlap_minutes <= 0)
        return false;

    // Compute maximum allowed cycles within overlap
    int max_cycles = (overlap_minutes <= 0) ? 0 : ((overlap_minutes - 1) / cycling_interval_min) + 1;
    ESP_LOGD(TAG, "Max cycles within overlap: %d (interval=%d, overlap=%d)", max_cycles, cycling_interval_min, overlap_minutes);

    if (max_cycles <= 0)
    {
        ESP_LOGD(TAG, "max_cycles <= 0. Returning false.");
        return false;
    }

    // Compute elapsed minutes since cycling start (wrap-aware)
    int elapsed = (current >= start) ? (current - start) : (current + 1440 - start);
    ESP_LOGD(TAG, "Elapsed since start: %d minutes", elapsed);

    int cycle_index = elapsed / cycling_interval_min;
    if (cycle_index >= max_cycles)
    {
        ESP_LOGD(TAG, "cycle_index >= max_cycles. Returning false.");
        return false;
    }

    int elapsed_mod = elapsed % cycling_interval_min;
    if (elapsed_mod < duration_min)
    {
        ESP_LOGD(TAG, "elapsed_mod=%d, duration=%d", elapsed_mod, duration_min);
        return true;
    }

    return false;
}

static inline const char *safe_str(const char *s)
{
    return (s && s[0] != '\0') ? s : "(null)";
}

void select_sensors(void)
{
    int j = 0; // CO2 sensor counter
    int k = 0; // RH sensor counter

    static const char *TAG = "select_sensors";

    co2_sensor_counter = 0;
    rh_sensor_counter = 0;

    float sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];

    if (xQueuePeek(sensor_avg_queue, &sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read from sensor_avg_queue");
        return;
    }

    for (int i = 0; i < CHANNELS_PER_BUS; i++)
    {
        const bus_sensor_t *s0 = NULL;
        const bus_sensor_t *s1 = NULL;

        if (sensor_cfg_mutex &&
            xSemaphoreTake(sensor_cfg_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            s0 = &bus0_sensor_settings[i];
            s1 = &bus1_sensor_settings[i];
            xSemaphoreGive(sensor_cfg_mutex);
        }

        // -----------------------------
        // CO₂ sensors (bus 0)
        // -----------------------------
        if (s0 && s0->co2_enabled && j < MAX_SENSORS)
        {
            const char *valve = safe_str(s0->valve);
            float reading = sensor_data[0][i][IDX_CO2];

            if (!isfinite(reading))
                reading = 0.0f;

            co2_sensors[j].valve = (char *)valve;
            co2_sensors[j].co2_reading = reading;

            ESP_LOGI(TAG, "%s, CO2: %.2f", valve, reading);
            j++;
        }

        // -----------------------------
        // CO₂ sensors (bus 1)
        // -----------------------------
        if (s1 && s1->co2_enabled && j < MAX_SENSORS)
        {
            const char *valve = safe_str(s1->valve);
            float reading = sensor_data[1][i][IDX_CO2];

            if (!isfinite(reading))
                reading = 0.0f;

            co2_sensors[j].valve = (char *)valve;
            co2_sensors[j].co2_reading = reading;

            ESP_LOGI(TAG, "%s, CO2: %.2f", valve, reading);
            j++;
        }

        // -----------------------------
        // RH sensors (bus 0)
        // -----------------------------
        if (s0 && s0->rh_enabled && k < MAX_SENSORS)
        {
            const char *valve = safe_str(s0->valve);
            float reading = sensor_data[0][i][IDX_HUMIDITY];

            if (!isfinite(reading))
                reading = 0.0f;

            rh_sensors[k].valve = (char *)valve;
            rh_sensors[k].rh_reading = reading;

            ESP_LOGI(TAG, "%s, RH: %.2f", valve, reading);
            k++;
        }

        // -----------------------------
        // RH sensors (bus 1)
        // -----------------------------
        if (s1 && s1->rh_enabled && k < MAX_SENSORS)
        {
            const char *valve = safe_str(s1->valve);
            float reading = sensor_data[1][i][IDX_HUMIDITY];

            if (!isfinite(reading))
                reading = 0.0f;

            rh_sensors[k].valve = (char *)valve;
            rh_sensors[k].rh_reading = reading;

            ESP_LOGI(TAG, "%s, RH: %.2f", valve, reading);
            k++;
        }
    }

    co2_sensor_counter = j;
    rh_sensor_counter = k;
}

/* Consume (check-and-clear) request set by POST handler. Returns true if move was requested. */
bool statemachine_consume_valve_move_request(void)
{
    bool rv = false;
    if (valve_control_data_mutex && xSemaphoreTake(valve_control_data_mutex, (TickType_t)10) == pdTRUE)
    {
        rv = valvecontroldata.valve_move_request_from_post;
        valvecontroldata.valve_move_request_from_post = false;
        xSemaphoreGive(valve_control_data_mutex);
    }
    else
    {
        ESP_LOGW(TAG, "Could not take valve_control_data_mutex to consume POST move request.");
        /* conservative: avoid triggering move if mutex cannot be taken */
        rv = false;
    }
    return rv;
}
