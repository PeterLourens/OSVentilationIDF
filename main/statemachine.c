#include "statemachine.h"

static const char *TAG = "Statemachine";

float statemachine_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

int old_time = 0;
int elapsed_time = 0;

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

// bool valve_open[MAX_VALVES] = {false};

// Functions for the state machine would go here
void init_statemachine(void)
{
    // In init state set fanspeed to low (1) and state to init in struct sm_actuals with Mutex
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = 1;               // Init
        sm_actuals.fanspeed = FANSPEED_LOW; // Low
        xSemaphoreGive(statemachine_mutex);
    }

    // Code to set fanspeed
    set_fanspeed(FANSPEED_LOW);
}

void run_statemachine(void)
{
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

    set_fanspeed(state_fanspeed);

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

void day_transitions(void)
{
    int state = STATE_NR_DAY;   // Local state variable
    int state_config_index = 0; // Index number for state configuration
    int new_state = 0;          // State after logic in this function
    int state_fanspeed = 0;     // Fanspeed from global config struct
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int rhhighlevel = 0;        // Local variable for RH high level from general statemachine config
    int co2highlevel = 0;       // Local variable for CO2 high level from general statemachine config
    int new_time = 0;

    bool valve_move_locked = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    co2_sensors_high = 0;
    rh_sensors_high = 0;

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_DAY, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed;             // Assign state fanspeed to variable
        minimum_state_time = statemachine_config[state_config_index].duration_minutes; // Minimum time statemachine remain in this state

        // General statemachine config
        co2highlevel = statemachine_gen_config.co2_high_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;

        xSemaphoreGive(state_config_mutex);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        xSemaphoreGive(statemachine_mutex);
    }

    ESP_LOGI(TAG, "State: %s, fanspeed: %s, state_time: %d.", state, state_fanspeed, elapsed_time);

    set_fanspeed(state_fanspeed);
    select_sensors();

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);

    if (valve_move_locked == 0)
    {
        // valve_position_statemachine(statemachine_state);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    for (int i = 0; i < co2_sensor_counter; i++)
    {
        if (co2_sensors[i].co2_reading > co2highlevel)
        {
            ESP_LOGI(TAG, "Sensor located at %s has high CO2.", co2_sensors[i].valve);
            ESP_LOGI(TAG, "Transit to highco2day if minimum state time has expired.");
            co2_sensors_high++;
        }
    }

    for (int i = 0; i < rh_sensor_counter; i++)
    {
        if (rh_sensors[i].rh_reading > rhhighlevel)
        {
            ESP_LOGI(TAG, "Sensor located at %s has high RH.", rh_sensors[i].valve);
            ESP_LOGI(TAG, "Transit to highrhday if minimum state time has expired.");
            rh_sensors_high++;
        }
    }

    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    if (co2_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHCO2DAY;
    }
    else if (rh_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHRHDAY;
    }
    else if (is_day() == false)
    {
        ESP_LOGI(TAG, "Transit to night.");
        new_state = STATE_NR_NIGHT;
    }
    /*else if (cooking_times() == true)
    {
        ESP_LOGI(TAG, "Transit to cooking.");
        new_state = STATE_NR_COOKING;
    }
    else if (valve_cycle_times_day() == true)
    {
        ESP_LOGI(TAG, "Transit to valve cycling day state.");
        new_state = STATE_NR_CYCLINGDAY;
    }*/
    // Manual high speed mode is ignored for now
    else
    {
        ESP_LOGI(TAG, "Stay in day state.");
        new_state = STATE_NR_DAY;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        if (state != new_state)
        {
            elapsed_time = 0;
            old_time = (esp_timer_get_time()) / 1000000;
        }
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void night_transitions(void) {

    int state = STATE_NR_NIGHT;   // Local state variable
    int state_config_index = 0; // Index number for state configuration
    int new_state = 0;          // State after logic in this function
    int state_fanspeed = 0;     // Fanspeed from global config struct
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int rhhighlevel = 0;        // Local variable for RH high level from general statemachine config
    int co2highlevel = 0;       // Local variable for CO2 high level from general statemachine config
    int new_time = 0;

    bool valve_move_locked = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    co2_sensors_high = 0;
    rh_sensors_high = 0;

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_DAY, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed;             // Assign state fanspeed to variable
        minimum_state_time = statemachine_config[state_config_index].duration_minutes; // Minimum time statemachine remain in this state

        // General statemachine config
        co2highlevel = statemachine_gen_config.co2_high_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;

        xSemaphoreGive(state_config_mutex);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        xSemaphoreGive(statemachine_mutex);
    }

    ESP_LOGI(TAG, "State: %s, fanspeed: %s, state_time: %d.", state, state_fanspeed, elapsed_time);

    set_fanspeed(state_fanspeed);
    select_sensors();

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);

    if (valve_move_locked == 0)
    {
        // valve_position_statemachine(statemachine_state);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    for (int i = 0; i < co2_sensor_counter; i++)
    {
        if (co2_sensors[i].co2_reading > co2highlevel)
        {
            ESP_LOGI(TAG, "Sensor located at %s has high CO2.", co2_sensors[i].valve);
            ESP_LOGI(TAG, "Transit to highco2day if minimum state time has expired.");
            co2_sensors_high++;
        }
    }

    for (int i = 0; i < rh_sensor_counter; i++)
    {
        if (rh_sensors[i].rh_reading > rhhighlevel)
        {
            ESP_LOGI(TAG, "Sensor located at %s has high RH.", rh_sensors[i].valve);
            ESP_LOGI(TAG, "Transit to highrhday if minimum state time has expired.");
            rh_sensors_high++;
        }
    }

    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    if (co2_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHCO2NIGHT;
    }
    else if (rh_sensors_high > 0 && elapsed_time >= minimum_state_time)
    {
        new_state = STATE_NR_HIGHRHNIGHT;
    }
    else if (is_day() == true)
    {
        ESP_LOGI(TAG, "Transit to day.");
        new_state = STATE_NR_DAY;
    }
    /*else if (cooking_times() == true)
    {
        ESP_LOGI(TAG, "Transit to cooking.");
        new_state = STATE_NR_COOKING;
    }
    else if (valve_cycle_times_day() == true)
    {
        ESP_LOGI(TAG, "Transit to valve cycling day state.");
        new_state = STATE_NR_CYCLINGDAY;
    }*/
    // Manual high speed mode is ignored for now
    else
    {
        ESP_LOGI(TAG, "Stay in night state.");
        new_state = STATE_NR_NIGHT;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        if (state != new_state)
        {
            elapsed_time = 0;
            old_time = (esp_timer_get_time()) / 1000000;
        }
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void high_co2_day_transitions(void) {}
void high_co2_night_transitions(void) {}
void high_rh_day_transitions(void) {}
void high_rh_night_transitions() {}
void cooking_transitions(void) {}
void valve_cycle_day_transitions(void) {}
void valve_cycle_night_transitions(void) {}
void manual_high_speed_transitions(void) {}
