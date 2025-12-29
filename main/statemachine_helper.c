#include "statemachine_helper.h"
#include "statemachine.h"
#include "globals.h"

static const char *TAG = "StatemachineHelper";

// Definitions: allocate storage for pointers shared across translation units. These must be non-static so statemachine.c and other files can use them.
float (*statemachine_sensor_data)[CHANNELS_PER_BUS][DATA_FIELDS] = NULL;
float (*statemachine_avg_sensor_data)[CHANNELS_PER_BUS][DATA_FIELDS] = NULL;

static float sensor_fifo[2][8][3][MAX_FIFO_SIZE];
static float sum[48] = {0.0};
static float avg[48] = {0.0};
static int fifoSize[48] = {0}; // Current number of elements in the FIFO.
static int fifoHead[48] = {0}; // Index of the oldest element in the FIFO.

void sensor_data_average(void)
{
    static const char *TAG = "Sensor avg";

    int pos;
    float sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    float sensor_avg_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    if (xQueuePeek(sensor_queue, &sensor_data, 0) == pdTRUE)
    {
        pos = 0;
        for (int bus = 0; bus < 2; bus++)
        {
            for (int slot = 0; slot < 8; slot++)
            {
                for (int measurement = 0; measurement < 3; measurement++)
                {
                    fifoPush(bus, slot, measurement, sensor_data[bus][slot][measurement], pos);
                    sensor_avg_data[bus][slot][measurement] = avg[pos];
                    pos++; // counter for fifoSize and fifoHead
                }
            }
        }
        if (sensor_avg_queue != NULL)
        {
            xQueueOverwrite(sensor_avg_queue, &sensor_avg_data);
        }
        else
        {
            ESP_LOGE(TAG, "Average sensor data queue handle is NULL.");
        }
    }
}

// FIFO push function to add a new value and update sum and average required for sensor moving average calculation
void fifoPush(int bus, int slot, int measurement, float value, int pos)
{
    if (fifoSize[pos] < MAX_FIFO_SIZE)
    {
        // Add the value to the next available position.
        sensor_fifo[bus][slot][measurement][(fifoHead[pos] + fifoSize[pos]) % MAX_FIFO_SIZE] = value;
        sum[pos] += value;
        fifoSize[pos] = fifoSize[pos] + 1;
    }
    else
    {
        // Overwrite the oldest value and move the head forward.
        sum[pos] -= sensor_fifo[bus][slot][measurement][fifoHead[pos]];
        sensor_fifo[bus][slot][measurement][fifoHead[pos]] = value;
        sum[pos] += value;
        fifoHead[pos] = (fifoHead[pos] + 1) % MAX_FIFO_SIZE;
    }
    avg[pos] = sum[pos] / fifoSize[pos];
}

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

    if (is_weekend())
    {
        int day_start = weekend_day_hour_start * 60 + weekend_day_minute_start;
        int night_start = weekend_night_hour_start * 60 + weekend_night_minute_start;
        return current_minutes >= day_start && current_minutes < night_start;
    }
    else
    {
        int day_start = weekday_day_hour_start * 60 + weekday_day_minute_start;
        int night_start = weekday_night_hour_start * 60 + weekday_night_minute_start;
        return current_minutes >= day_start && current_minutes < night_start;
    }
}

bool is_weekend(void)
{
    char day_buffer[10];
    formatted_day(day_buffer, sizeof(day_buffer));

    if (strcmp(day_buffer, "Saturday") == 0 || strcmp(day_buffer, "Sunday") == 0)
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

    struct tm temp_time;
    time_t now = 0;

    time(&now);
    localtime_r(&now, &temp_time);

    // Get state config setting when cooking is required
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)100) == pdTRUE)
        {
            start_hour = statemachine_config[2].state_start_hour;
            start_min = statemachine_config[2].state_start_minute;
            stop_hour = statemachine_config[2].state_end_hour;
            stop_min = statemachine_config[2].state_end_minute;
            xSemaphoreGive(state_config_mutex);
        }
    }

    if (temp_time.tm_hour == start_hour && temp_time.tm_min > start_min)
        return true;
    else if (temp_time.tm_hour == stop_hour && temp_time.tm_min >= stop_min)
        return false;
    else
        return false;
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
    if (state_config_index < 0)
        return false;

    // Read config values and general day/night config under mutex
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)100) == pdTRUE)
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
            return false;
        }
    }
    else
    {
        // No mutex available — cannot safely read config
        return false;
    }

    // Enforce minimum interval of 60 minutes
    if (cycling_interval_min < 60)
        cycling_interval_min = 60;

    // Validate duration
    if (duration_min <= 0)
        return false;

    int start = start_hour * 60 + start_min;
    int stop = stop_hour * 60 + stop_min;
    int current = temp_time.tm_hour * 60 + temp_time.tm_min;

    // If not in cycling window return false
    bool in_window = false;
    if (start == stop)
    {
        in_window = true;
    }
    else if (start < stop)
    {
        in_window = (current >= start && current < stop);
    }
    else
    {
        in_window = (current >= start || current < stop);
    }
    if (!in_window)
        return false;

    // Determine day window (weekday or weekend)
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

    int overlap_minutes = minutes_overlap(start, stop, day_start, day_stop);
    if (overlap_minutes <= 0)
        return false;

    // Compute maximum allowed cycles within overlap (ceil-like for first cycle at start)
    int max_cycles = (overlap_minutes <= 0) ? 0 : ((overlap_minutes - 1) / cycling_interval_min) + 1;
    if (max_cycles <= 0)
        return false;

    // Compute elapsed minutes since cycling start (wrap-aware)
    int elapsed = (current >= start) ? (current - start) : (current + 1440 - start);

    // Determine which cycle index we're in
    int cycle_index = elapsed / cycling_interval_min;
    if (cycle_index >= max_cycles)
        return false;

    int elapsed_mod = elapsed % cycling_interval_min;
    if (elapsed_mod < duration_min)
        return true;

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
        return false;

    // Read config values and general day/night config under mutex
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)100) == pdTRUE)
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
            return false;
        }
    }
    else
    {
        return false;
    }

    // Enforce minimum interval of 60 minutes
    if (cycling_interval_min < 60)
        cycling_interval_min = 60;

    // Validate duration
    if (duration_min <= 0)
        return false;

    int start = start_hour * 60 + start_min;
    int stop = stop_hour * 60 + stop_min;
    int current = temp_time.tm_hour * 60 + temp_time.tm_min;

    // If not in cycling window return false
    bool in_window = false;
    if (start == stop)
    {
        in_window = true;
    }
    else if (start < stop)
    {
        in_window = (current >= start && current < stop);
    }
    else
    {
        in_window = (current >= start || current < stop);
    }
    if (!in_window)
        return false;

    // Determine night window (weekday or weekend). Night window is [night_start, next day start)
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

    int overlap_minutes = minutes_overlap(start, stop, night_start, night_stop);
    if (overlap_minutes <= 0)
        return false;

    // Compute maximum allowed cycles within overlap
    int max_cycles = (overlap_minutes <= 0) ? 0 : ((overlap_minutes - 1) / cycling_interval_min) + 1;
    if (max_cycles <= 0)
        return false;

    // Compute elapsed minutes since cycling start (wrap-aware)
    int elapsed = (current >= start) ? (current - start) : (current + 1440 - start);

    int cycle_index = elapsed / cycling_interval_min;
    if (cycle_index >= max_cycles)
        return false;

    int elapsed_mod = elapsed % cycling_interval_min;
    if (elapsed_mod < duration_min)
        return true;

    return false;
}

void select_sensors(void)
{
    int j = 0; // counter for struct
    int k = 0; // counter for struct

    static const char *TAG = "select_sensors";

    co2_sensor_counter = 0;
    rh_sensor_counter = 0;

    float sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS];

    // Copy sensor readings from global
    if (xQueuePeek(sensor_avg_queue, &sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read from sensor_avg_queue");
        return;
    }

    for (int i = 0; i < CHANNELS_PER_BUS; i++)
    {
        const bus_sensor_t *s0 = NULL;
        const bus_sensor_t *s1 = NULL;

        if (sensor_cfg_mutex && xSemaphoreTake(sensor_cfg_mutex, (TickType_t)10) == pdTRUE)
        {
            s0 = &bus0_sensor_settings[i];
            s1 = &bus1_sensor_settings[i];
            xSemaphoreGive(sensor_cfg_mutex);
        }

        if (s0 && s0->co2_enabled)
        {
            co2_sensors[j].valve = (char *)s0->valve;
            co2_sensors[j].co2_reading = sensor_data[0][i][IDX_CO2];
            ESP_LOGI(TAG, "%s, CO2: %.2f", co2_sensors[j].valve, co2_sensors[j].co2_reading);
            j++;
        }

        if (s1 && s1->co2_enabled)
        {
            co2_sensors[j].valve = (char *)s1->valve;
            co2_sensors[j].co2_reading = sensor_data[1][i][IDX_CO2];
            ESP_LOGI(TAG, "%s, CO2: %.2f", co2_sensors[j].valve, co2_sensors[j].co2_reading);
            j++;
        }

        if (s0 && s0->rh_enabled)
        {
            rh_sensors[k].valve = (char *)s0->valve;
            rh_sensors[k].rh_reading = sensor_data[0][i][IDX_HUMIDITY];
            ESP_LOGI(TAG, "%s, RH: %.2f", rh_sensors[k].valve, rh_sensors[k].rh_reading);
            k++;
        }

        if (s1 && s1->rh_enabled)
        {
            rh_sensors[k].valve = (char *)s1->valve;
            rh_sensors[k].rh_reading = sensor_data[1][i][IDX_HUMIDITY];
            ESP_LOGI(TAG, "%s, RH: %.2f", rh_sensors[k].valve, rh_sensors[k].rh_reading);
            k++;
        }
    }

    co2_sensor_counter = j;
    rh_sensor_counter = k;
}

// Helper functions to have data available in other tasks
int parse_co2_sensors_high(void)
{
    return co2_sensors_high;
}

int parse_rh_sensors_high(void)
{
    return rh_sensors_high;
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

/* Allocate the two big buffers. Prefer PSRAM, fall back to internal heap.
   Call early in startup (e.g. from main init) before run_statemachine runs. */
esp_err_t statemachine_alloc_buffers(void)
{
    size_t elems = (size_t)MAX_I2C_BUSES;
    size_t bytes = elems * sizeof(*statemachine_sensor_data);

    void *p = NULL;
    if (esp_psram_get_size() > 0) {
        p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) {
            ESP_LOGI(TAG, "Allocated %u bytes for statemachine_sensor_data in PSRAM", (unsigned)bytes);
        }
    }
    if (p == NULL) {
        p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (p) {
            ESP_LOGW(TAG, "Allocated %u bytes for statemachine_sensor_data in internal RAM", (unsigned)bytes);
        }
    }
    if (p == NULL) {
        ESP_LOGE(TAG, "Failed to allocate %u bytes for statemachine_sensor_data", (unsigned)bytes);
        return ESP_ERR_NO_MEM;
    }
    statemachine_sensor_data = (float (*)[CHANNELS_PER_BUS][DATA_FIELDS])p;
    memset(statemachine_sensor_data, 0, bytes);

    /* avg buffer */
    p = NULL;
    if (esp_psram_get_size() > 0) {
        p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) {
            ESP_LOGI(TAG, "Allocated %u bytes for statemachine_avg_sensor_data in PSRAM", (unsigned)bytes);
        }
    }
    if (p == NULL) {
        p = heap_caps_malloc(bytes, MALLOC_CAP_8BIT);
        if (p) {
            ESP_LOGW(TAG, "Allocated %u bytes for statemachine_avg_sensor_data in internal RAM", (unsigned)bytes);
        }
    }
    if (p == NULL) {
        /* cleanup first allocation */
        heap_caps_free(statemachine_sensor_data);
        statemachine_sensor_data = NULL;
        ESP_LOGE(TAG, "Failed to allocate %u bytes for statemachine_avg_sensor_data", (unsigned)bytes);
        return ESP_ERR_NO_MEM;
    }
    statemachine_avg_sensor_data = (float (*)[CHANNELS_PER_BUS][DATA_FIELDS])p;
    memset(statemachine_avg_sensor_data, 0, bytes);

    ESP_LOGI(TAG, "Statemachine buffers ready (max buses=%d, channels=%d, fields=%d).",
             MAX_I2C_BUSES, CHANNELS_PER_BUS, DATA_FIELDS);
    return ESP_OK;
}

/* Optional cleanup (call if freeing is needed) */
void statemachine_free_buffers(void)
{
    if (statemachine_sensor_data) {
        heap_caps_free(statemachine_sensor_data);
        statemachine_sensor_data = NULL;
    }
    if (statemachine_avg_sensor_data) {
        heap_caps_free(statemachine_avg_sensor_data);
        statemachine_avg_sensor_data = NULL;
    }
}