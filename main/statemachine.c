#include "statemachine.h"

static const char *TAG = "Statemachine";

float statemachine_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

int co2_sensor_counter = 0;
int rh_sensor_counter = 0;
int co2_sensors_high = 0;
int rh_sensors_high = 0;
int old_time = 0;
int elapsed_time = 0;

const char *STATE_INIT = "init";
const char *STATE_STOPPED = "stopped";
const char *STATE_DAY = "day";
const char *STATE_NIGHT = "night";
const char *STATE_HIGHCO2DAY = "highco2day";
const char *STATE_HIGHCO2NIGHT = "highco2night";
const char *STATE_HIGHRHDAY = "highrhday";
const char *STATE_HIGHRHNIGHT = "highrhnight";
const char *STATE_COOKING = "cooking";
const char *STATE_CYCLINGDAY = "cyclingday";
const char *STATE_CYCLINGNIGHT = "cyclingnight";
const char *STATE_MANUAL_HIGH_SPEED = "manual_high_speed";

// bool valve_open[MAX_VALVES] = {false};

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

CO2_Sensors_t co2_sensors[MAX_SENSORS] = {};
RH_Sensors_t rh_sensors[MAX_SENSORS] = {};

// Functions for the state machine would go here
void init_statemachine(void)
{
    // In init state set fanspeed to low (1) and state to init in struct sm_actuals with Mutex
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.sm_state = 1;    // Init
        sm_actuals.sm_fanspeed = 1; // Low
        xSemaphoreGive(statemachine_mutex);
    }

    // Code to set fanspeed
    // set_fanspeed(sm_actuals.sm_fanspeed);
}

void run_statemachine(void)
{
    // char *temp_state = NULL;
    char *temp_state = "test";

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        // temp_state = state;
        xSemaphoreGive(statemachine_mutex);
    }

    ESP_LOGI(TAG, "Read sensor data from queue for statemachine.");

    if (xQueuePeek(sensor_queue, &statemachine_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from sensor queue.");
        return;
    }

    ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");

    if (xQueuePeek(sensor_avg_queue, &statemachine_avg_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
        return;
    }

    if (strcmp(temp_state, STATE_INIT) == 0)
    {
        init_transitions();
    }
    else if (strcmp(temp_state, STATE_STOPPED) == 0)
    {
        stopped_transitions();
    }
    else if (strcmp(temp_state, STATE_DAY) == 0)
    {
        day_transitions();
    }
    else if (strcmp(temp_state, STATE_NIGHT) == 0)
    {
        night_transitions();
    }
    else if (strcmp(temp_state, STATE_HIGHCO2DAY) == 0)
    {
        high_co2_day_transitions();
    }
    else if (strcmp(temp_state, STATE_HIGHCO2NIGHT) == 0)
    {
        high_co2_night_transitions();
    }
    else if (strcmp(temp_state, STATE_HIGHRHDAY) == 0)
    {
        high_rh_day_transitions();
    }
    else if (strcmp(temp_state, STATE_HIGHRHNIGHT) == 0)
    {
        high_rh_night_transitions();
    }
    else if (strcmp(temp_state, STATE_COOKING) == 0)
    {
        cooking_transitions();
    }
    else if (strcmp(temp_state, STATE_CYCLINGDAY) == 0)
    {
        valve_cycle_day_transitions();
    }
    else if (strcmp(temp_state, STATE_CYCLINGNIGHT) == 0)
    {
        valve_cycle_night_transitions();
    }
    else if (strcmp(temp_state, STATE_MANUAL_HIGH_SPEED) == 0)
    {
        manual_high_speed_transitions();
    }
    else
    {
        // This state should normally never be entered. Back to init of statemachine to keep it running
        ESP_LOGE(TAG, "Error in state number, back to init state.");
        init_transitions();
    }
}

void init_transitions(void) {}
void stopped_transitions(void) {}
void day_transitions(void) {}
void night_transitions(void) {}
void high_co2_day_transitions(void) {}
void high_co2_night_transitions(void) {}
void high_rh_day_transitions(void) {}
void high_rh_night_transitions() {}
void cooking_transitions(void) {}
void valve_cycle_day_transitions(void) {}
void valve_cycle_night_transitions(void) {}
void manual_high_speed_transitions(void) {}
