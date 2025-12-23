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

const char *STATE_ERROR = "error";                          // state_nr = 0
const char *STATE_INIT = "init";                            // state_nr = 1
const char *STATE_DAY = "day";                              // state_nr = 2
const char *STATE_NIGHT = "night";                          // state_nr = 3
const char *STATE_HIGHCO2DAY = "highco2day";                // state_nr = 4
const char *STATE_HIGHCO2NIGHT = "highco2night";            // state_nr = 5
const char *STATE_HIGHRHDAY = "highrhday";                  // state_nr = 6
const char *STATE_HIGHRHNIGHT = "highrhnight";              // state_nr = 7
const char *STATE_COOKING = "cooking";                      // state_nr = 8
const char *STATE_CYCLINGDAY = "cyclingday";                // state_nr = 9
const char *STATE_CYCLINGNIGHT = "cyclingnight";            // state_nr = 10
const char *STATE_MANUALHS = "manual_high_speed";           // state_nr = 11
const char *STATE_STOPPED = "stopped";                      // state_nr = 12

const int STATE_NR_ERROR = 0;
const int STATE_NR_INIT = 1;
const int STATE_NR_DAY = 2;
const int STATE_NR_NIGHT = 3;
const int STATE_NR_HIGHCO2DAY = 4;
const int STATE_NR_HIGHCO2NIGHT = 5;
const int STATE_NR_HIGHRHDAY = 6;
const int STATE_NR_HIGHRHNIGHT = 7;
const int STATE_NR_COOKING = 8;
const int STATE_NR_CYCLINGDAY = 9;
const int STATE_NR_CYCLINGNIGHT = 10;
const int STATE_NR_MANUALHS = 11;
const int STATE_NR_STOPPED = 12;

const int FANSPEED_ERROR = 0;
const int FANSPEED_LOW = 1;
const int FANSPEED_MEDIUM = 2;
const int FANSPEED_HIGH = 3;

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
        sm_actuals.state = 1;    // Init
        sm_actuals.fanspeed = 1; // Low
        xSemaphoreGive(statemachine_mutex);
    }

    // Code to set fanspeed
    // set_fanspeed(sm_actuals.sm_fanspeed);
}

void run_statemachine(void)
{
    // char *temp_state = NULL;
    int temp_state = 0;

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        temp_state = sm_actuals.state;
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

    if (temp_state == STATE_NR_INIT)
    {
        init_transitions();
    }
    else if (temp_state == STATE_NR_STOPPED)
    {
        stopped_transitions();
    }
    else if (temp_state == STATE_NR_DAY)
    {
        day_transitions();
    }
    else if (temp_state == STATE_NR_NIGHT)
    {
        night_transitions();
    }
    else if (temp_state == STATE_NR_HIGHCO2DAY)
    {
        high_co2_day_transitions();
    }
    else if (temp_state == STATE_NR_HIGHCO2NIGHT)
    {
        high_co2_night_transitions();
    }
    else if (temp_state == STATE_NR_HIGHRHDAY)
    {
        high_rh_day_transitions();
    }
    else if (temp_state == STATE_NR_HIGHRHNIGHT)
    {
        high_rh_night_transitions();
    }
    else if (temp_state == STATE_NR_COOKING)
    {
        cooking_transitions();
    }
    else if (temp_state == STATE_NR_CYCLINGDAY)
    {
        valve_cycle_day_transitions();
    }
    else if (temp_state == STATE_NR_CYCLINGNIGHT)
    {
        valve_cycle_night_transitions();
    }
    else if (temp_state == STATE_NR_MANUALHS)
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

void stopped_transitions(void) 
{
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = STATE_NR_STOPPED;
        xSemaphoreGive(statemachine_mutex);
    }

    // No further logic required because when start statemachine button is pushed the statemachine will go back to init state
    ESP_LOGI(TAG, "State is stopped. Push start statemachine button on the Valve Control web page to continue.");
}

void init_transitions(void)
{
    int state_fanspeed = FANSPEED_LOW;
    int state = STATE_NR_INIT;
    int new_state = 0;

    // Actions for this state
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state_fanspeed;
        sm_actuals.fanspeed = state;
        ESP_LOGI(TAG, "State: init, fanspeed: %s.", sm_actuals.fanspeed);
        xSemaphoreGive(statemachine_mutex);
    }

    //set_fanspeed(state_fanspeed);

    if (is_day() == true)
    {
        ESP_LOGI(TAG, "Transit to day.");
        new_state = STATE_NR_DAY;
    }
    else
    {
        ESP_LOGI(TAG, "Transit to night.");
        new_state = STATE_NR_NIGHT;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

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
