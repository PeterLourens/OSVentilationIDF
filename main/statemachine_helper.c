#include "statemachine_helper.h"

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

bool valve_cycle_times_day(void)
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

    // TODO:
    // This function is based on fixed times but state config define and and start hour and interval
    // Calculate based on config settings what times cyclng should start and how many times
    // Array is probably easiest to temp store this, e.g. cycling_times[index, hour, min] and iterate through it by index

    // Get state config setting when cycling is required
    strncpy(state_name, STATE_CYCLINGDAY, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    // Get state config setting when cycling is required
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)100) == pdTRUE)
        {
            start_hour = statemachine_config[state_config_index].state_start_hour;
            start_min = statemachine_config[state_config_index].state_start_minute;
            stop_hour = statemachine_config[state_config_index].state_end_hour;
            stop_min = statemachine_config[state_config_index].state_end_minute;
            xSemaphoreGive(state_config_mutex);
        }
    }

    // **** TODO: Logic to be updated *****
    if (temp_time.tm_hour == 10 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 10 && temp_time.tm_min >= 30)
        return false;
    else if (temp_time.tm_hour == 12 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 12 && temp_time.tm_min >= 30)
        return false;
    else if (temp_time.tm_hour == 14 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 14 && temp_time.tm_min >= 30)
        return false;
    else if (temp_time.tm_hour == 16 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 16 && temp_time.tm_min >= 30)
        return false;
    else if (temp_time.tm_hour == 18 && temp_time.tm_min > 30)
        return true;
    else if (temp_time.tm_hour == 18 && temp_time.tm_min >= 58)
        return false;
    else if (temp_time.tm_hour == 20 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 20 && temp_time.tm_min >= 30)
        return false;
    else
        return false;
}

bool valve_cycle_times_night(void)
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

    // Get state config setting when cycling is required
    strncpy(state_name, STATE_CYCLINGNIGHT, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);
    // state_index = find_state_index(char *state_name, size_t state_name);

    // Get state config setting when cycling is required
    if (state_config_mutex != NULL)
    {
        if (xSemaphoreTake(state_config_mutex, (TickType_t)100) == pdTRUE)
        {
            start_hour = statemachine_config[state_config_index].state_start_hour;  // **** TODO: Find config struct index *****
            start_min = statemachine_config[state_config_index].state_start_minute; // **** TODO: Find config struct index *****
            stop_hour = statemachine_config[state_config_index].state_end_hour;     // **** TODO: Find config struct index *****
            stop_min = statemachine_config[state_config_index].state_end_minute;    // **** TODO: Find config struct index *****
            xSemaphoreGive(state_config_mutex);
        }
    }

    // **** TODO: Logic to be updated *****
    if (temp_time.tm_hour == 23 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 23 && temp_time.tm_min >= 30)
        return false;
    else if (temp_time.tm_hour == 1 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 1 && temp_time.tm_min >= 30)
        return false;
    else if (temp_time.tm_hour == 3 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 3 && temp_time.tm_min >= 30)
        return false;
    else if (temp_time.tm_hour == 5 && temp_time.tm_min < 30)
        return true;
    else if (temp_time.tm_hour == 5 && temp_time.tm_min >= 30)
        return false;
    else
        return false;
}

/*int fanspeed_char_to_int(char fanspeed_char)
{
    switch (fanspeed_char)
    {
    case 'Low': // Low
    case 'low':
        return 1;
    case 'Medium': // Medium
    case 'medium':
        return 2;
    case 'High': // High
    case 'high':
        return 3;
    default: // Error or unknown
        return 0;
    }
}*/

// The configuration from each statemachine state is stored in struct statemachine_config[MAX_NR_STATES];
// The name of the state is stored in the char statemachine_config[i].state_name
// The index i should then be used to indicate the actual state of the statemachine so that a direct link can be made with state_number and state_name
