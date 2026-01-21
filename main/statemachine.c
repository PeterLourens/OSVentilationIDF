#include "statemachine.h"
#include "statemachine_states.h"

static const char *TAG = "Statemachine";

int old_time = 0;
int elapsed_time = 0;

int co2_sensor_counter = 0;
int rh_sensor_counter = 0;

CO2_Sensors_t co2_sensors[MAX_SENSORS] = {};
RH_Sensors_t rh_sensors[MAX_SENSORS] = {};

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

    // Call the correct state function based on current state
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
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;
        ESP_LOGI(TAG, "State: init, fanspeed: %d.", sm_actuals.fanspeed);
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
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    int state = STATE_NR_DAY;   // Local state variable
    int state_config_index = 0; // Index number for state configuration
    int new_state = 0;          // State after logic in this function
    int state_fanspeed = 0;     // Fanspeed from global config struct
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int rhhighlevel = 0;        // Local variable for RH high level from general statemachine config
    int co2highlevel = 0;       // Local variable for CO2 high level from general statemachine config
    int new_time = 0;
    int co2_sensors_high = 0; // Local variable following the struct value
    int rh_sensors_high = 0;  // Local variable following the struct value

    bool valve_move_locked = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");
    if (xQueuePeek(sensor_avg_queue, &statemachine_avg_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
        return;
    }

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

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;

        for (int i = 0; i < co2_sensor_counter; i++)
        {
            if (co2_sensors[i].co2_reading > co2highlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high CO2.", co2_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highco2day if minimum state time has expired.");
                sm_actuals.co2_sensors_high++;
            }
        }

        for (int i = 0; i < rh_sensor_counter; i++)
        {
            if (rh_sensors[i].rh_reading > rhhighlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high RH.", rh_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highrhday if minimum state time has expired.");
                sm_actuals.rh_sensors_high++;
            }
        }
        co2_sensors_high = sm_actuals.co2_sensors_high;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);
    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }
    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    // Move valves when not locked
    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    // Conditions for changing states
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
    else if (cooking_times() == true)
    {
        ESP_LOGI(TAG, "Transit to cooking.");
        new_state = STATE_NR_COOKING;
    }
    else if (valve_cycle_times_day() == true)
    {
        ESP_LOGI(TAG, "Transit to valve cycling day state.");
        new_state = STATE_NR_CYCLINGDAY;
    }
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

void night_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    int state = STATE_NR_NIGHT; // Local state variable
    int state_config_index = 0; // Index number for state configuration
    int new_state = 0;          // State after logic in this function
    int state_fanspeed = 0;     // Fanspeed from global config struct
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int rhhighlevel = 0;        // Local variable for RH high level from general statemachine config
    int co2highlevel = 0;       // Local variable for CO2 high level from general statemachine config
    int new_time = 0;
    int co2_sensors_high = 0; // Local variable following the struct value
    int rh_sensors_high = 0;  // Local variable following the struct value

    bool valve_move_locked = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");
    if (xQueuePeek(sensor_avg_queue, &statemachine_avg_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
        return;
    }

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_NIGHT, sizeof(state_name));
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

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;

        for (int i = 0; i < co2_sensor_counter; i++)
        {
            if (co2_sensors[i].co2_reading > co2highlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high CO2.", co2_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highco2night if minimum state time has expired.");
                sm_actuals.co2_sensors_high++;
            }
        }

        for (int i = 0; i < rh_sensor_counter; i++)
        {
            if (rh_sensors[i].rh_reading > rhhighlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high RH.", rh_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highrhnight if minimum state time has expired.");
                sm_actuals.rh_sensors_high++;
            }
        }
        co2_sensors_high = sm_actuals.co2_sensors_high;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);
    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }
    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    // Move valves when not locked
    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    // Conditions for changing states
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
    else if (valve_cycle_times_night() == true)
    {
        ESP_LOGI(TAG, "Transit to valve cycling night state.");
        new_state = STATE_NR_CYCLINGNIGHT;
    }
    else
    {
        ESP_LOGI(TAG, "Stay in night state.");
        new_state = STATE_NR_NIGHT;
    }

    if (state != new_state)
    {
        elapsed_time = 0;
        old_time = (esp_timer_get_time()) / 1000000;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void high_co2_day_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    int state = STATE_NR_HIGHCO2DAY; // Local state variable
    int state_config_index = 0;      // Index number for state configuration
    int state_config_index_temp = 0; // Index number for state configuration
    int new_state = 0;               // State after logic in this function
    int state_fanspeed = 0;          // Fanspeed from global config struct
    int valve_nr = 0;
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int co2lowlevel = 0;        // Local variable for CO2 low level from general statemachine config
    int co2highlevel = 0;       // Local variable for CO2 high level from general statemachine config
    int new_time = 0;
    int co2_sensors_high = 0; // Local variable following the struct value

    int state_valve_position[MAX_VALVE_COUNT] = {0}; // Array for valvepositions from statemachine settings

    float reading = 0.2f;

    bool valve_move_locked = 0;
    bool valve_open[MAX_VALVE_COUNT] = {false};

    char state_name[MEDIUM_CONFIG_ITEM] = {};
    char state_name_temp[MEDIUM_CONFIG_ITEM] = {};
    char valve[MEDIUM_CONFIG_ITEM] = {};

    ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");
    if (xQueuePeek(sensor_avg_queue, statemachine_avg_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
        return;
    }

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_HIGHCO2DAY, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    // Copy state_name and call function to find index of the config
    strncpy(state_name_temp, STATE_TEMP, sizeof(state_name_temp));
    state_name_temp[sizeof(state_name_temp) - 1] = '\0';
    state_config_index_temp = find_state_index_by_name_ci(state_name_temp);

    ESP_LOGI(TAG, "Statename: %s, state config index is %d", state_name, state_config_index);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed;             // Assign state fanspeed to variable
        minimum_state_time = statemachine_config[state_config_index].duration_minutes; // Minimum time statemachine remain in this state

        // General statemachine config
        co2lowlevel = statemachine_gen_config.co2_low_level;
        co2highlevel = statemachine_gen_config.co2_high_level;
        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;
        co2_sensors_high = sm_actuals.co2_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "Low CO2 level: %d", co2lowlevel);
    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }
    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    // Temp valve settings for individual valves starting with default settings for this state.
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < 12; i++)
        {
            state_valve_position[i] = statemachine_config[state_config_index].valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }

    // Iterate through CO2 sensors to see which one has high CO2 reading to see if default settings apply when high reading is at fan inlet
    // or only one valve needs to open (when high reading is in a room)
    for (int i = 0; i < co2_sensor_counter; i++)
    {
        if (!co2_sensors[i].valve)
        {
            ESP_LOGE(TAG, "Valve name missing for sensor index %d", i);
            continue;
        }

        strncpy(valve, co2_sensors[i].valve, sizeof(valve) - 1);
        valve[sizeof(valve) - 1] = '\0';
        char *substr = strstr(valve, "valve");
        if (substr)
        {
            memmove(substr, substr + 5, strlen(substr + 5) + 1); // 5 is length of "valve"
        }
        valve_nr = atoi(valve);
        reading = co2_sensors[i].co2_reading;

        if (strcmp(valve, "Fan_inlet") != 0)
        {
            // Hysteresis logic
            if (valve_open[valve_nr])
            {
                // Valve is open → close only if CO₂ drops below low threshold
                if (reading < co2lowlevel)
                {
                    valve_open[valve_nr] = false;
                    state_valve_position[valve_nr] = 4;
                    ESP_LOGI(TAG,
                             "CO₂ %.1f ppm < low threshold %.1f ppm → closing valve %s",
                             reading, co2lowlevel, co2_sensors[i].valve);
                }
                else
                {
                    state_valve_position[valve_nr] = 20;
                    ESP_LOGI(TAG,
                             "CO₂ %.1f ppm ≥ low threshold %.1f ppm → keeping valve %s open",
                             reading, co2lowlevel, co2_sensors[i].valve);
                }
            }
            else
            {
                // Valve is closed → open only if CO₂ rises above high threshold
                if (reading > co2highlevel)
                {
                    valve_open[valve_nr] = true;
                    state_valve_position[valve_nr] = 20;
                    ESP_LOGI(TAG,
                             "CO₂ %.1f ppm > high threshold %.1f ppm → opening valve %s",
                             reading, co2highlevel, co2_sensors[i].valve);
                }
                else
                {
                    state_valve_position[valve_nr] = 4;
                    ESP_LOGI(TAG,
                             "CO₂ %.1f ppm ≤ high threshold %.1f ppm → keeping valve %s closed",
                             reading, co2highlevel, co2_sensors[i].valve);
                }
            }
        }

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
        {
            if (reading > co2lowlevel)
            {
                sm_actuals.co2_sensors_high++;
            }
            co2_sensors_high = sm_actuals.co2_sensors_high;
            xSemaphoreGive(statemachine_mutex);
        }
    }

    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);

    // Copy values to temp settings
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; i++)
        {
            statemachine_config[state_config_index_temp].valve_position[i] = state_valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }

    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index_temp);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    // Conditions for transition. This state always transits back to day first or to highco2night if night time.
    if (co2_sensors_high == 0 && elapsed_time >= minimum_state_time) // Day without high CO2 readings
    {
        ESP_LOGI(TAG, "Transit to day.");
        new_state = STATE_NR_DAY;
    }
    else if (is_day() == false && co2_sensors_high > 0) // Night with high CO2 readings
    {
        ESP_LOGI(TAG, "Transit to highco2night.");
        new_state = STATE_NR_HIGHCO2NIGHT;
    }
    else
    {
        ESP_LOGI(TAG, "Remain in highco2day state.");
        new_state = STATE_NR_HIGHCO2DAY;
    }

    if (state != new_state)
    {
        elapsed_time = 0;
        old_time = (esp_timer_get_time()) / 1000000;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void high_co2_night_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    int state = STATE_NR_HIGHCO2NIGHT; // Local state variable
    int state_config_index = 0;        // Index number for state configuration
    int state_config_index_temp = 0;   // Index number for state configuration
    int new_state = 0;                 // State after logic in this function
    int state_fanspeed = 0;            // Fanspeed from global config struct
    int valve_nr = 0;
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int co2lowlevel = 0;        // Local variable for CO2 low level from general statemachine config
    int co2highlevel = 0;       // Local variable for CO2 high level from general statemachine config
    int new_time = 0;
    int co2_sensors_high = 0; // Local variable following the struct value

    int state_valve_position[MAX_VALVE_COUNT] = {0}; // Array for valvepositions from statemachine settings

    float reading = 0.2f;

    bool valve_move_locked = 0;
    bool valve_open[MAX_VALVE_COUNT] = {false};

    char state_name[MEDIUM_CONFIG_ITEM] = {};
    char state_name_temp[MEDIUM_CONFIG_ITEM] = {};
    char valve[MEDIUM_CONFIG_ITEM] = {};

    ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");
    if (xQueuePeek(sensor_avg_queue, statemachine_avg_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
        return;
    }

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_HIGHCO2NIGHT, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    // Copy state_name and call function to find index of the config
    strncpy(state_name_temp, STATE_TEMP, sizeof(state_name_temp));
    state_name_temp[sizeof(state_name_temp) - 1] = '\0';
    state_config_index_temp = find_state_index_by_name_ci(state_name_temp);

    ESP_LOGI(TAG, "Statename: %s, state config index is %d", state_name, state_config_index);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed;             // Assign state fanspeed to variable
        minimum_state_time = statemachine_config[state_config_index].duration_minutes; // Minimum time statemachine remain in this state

        // General statemachine config
        co2lowlevel = statemachine_gen_config.co2_low_level;
        co2highlevel = statemachine_gen_config.co2_high_level;
        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed; // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock;
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;
        co2_sensors_high = sm_actuals.co2_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "Low CO2 level: %d", co2lowlevel);
    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }

    // Temp valve settings for individual valves starting with default settings for this state. Should read these from file and not hardcode them
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < 12; i++)
        {
            state_valve_position[i] = statemachine_config[state_config_index].valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }
    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    // High CO2 has been detected to come into this state. Iterate through CO2 sensors to see which sensor detects high CO2. Valves with CO2 sensors are default
    // set to 20 for this state. Valves with a CO2 value lower than co2lowlevel will be closed to 4 to direct airflow to the rooms with high CO2 reading.
    // ------------------------------------------------------------
    // CO₂ valve logic for HIGHCO2NIGHT
    // ------------------------------------------------------------

    // 1. Detect if ANY sensor is above the high threshold
    bool any_high = false;
    co2_sensors_high = 0;

    for (int i = 0; i < co2_sensor_counter; i++)
    {
        float reading = co2_sensors[i].co2_reading;

        if (reading > co2highlevel)
            any_high = true;

        if (reading > co2lowlevel)
            co2_sensors_high++;
    }

    // 2. Apply valve logic
    for (int i = 0; i < co2_sensor_counter; i++)
    {
        // Copy valve name locally
        strncpy(valve, co2_sensors[i].valve, sizeof(valve) - 1);
        valve[sizeof(valve) - 1] = '\0';

        // Skip fan inlet
        if (strcmp(valve, "Fan_inlet") == 0)
            continue;

        // Extract valve number
        char *substr = strstr(valve, "valve");
        if (substr)
            memmove(substr, substr + 5, strlen(substr + 5) + 1);

        valve_nr = atoi(valve);
        if (valve_nr < 0 || valve_nr >= MAX_VALVE_COUNT)
        {
            ESP_LOGW(TAG, "Invalid valve number parsed from '%s'", co2_sensors[i].valve);
            continue;
        }

        reading = co2_sensors[i].co2_reading;

        // ------------------------------------------------------------
        // CASE A: No sensor is high → all CO₂ sensor valves open to 20
        // ------------------------------------------------------------
        if (!any_high)
        {
            valve_open[valve_nr] = true;
            state_valve_position[valve_nr] = 20;

            ESP_LOGI(TAG, "No high CO₂ sensors. Opening %s to 20 (%.1f ppm).", co2_sensors[i].valve, reading);
            continue;
        }

        // ------------------------------------------------------------
        // CASE B: At least one sensor is high → hysteresis per valve
        // ------------------------------------------------------------
        if (valve_open[valve_nr])
        {
            // Valve currently open → close only if below low threshold
            if (reading < co2lowlevel)
            {
                valve_open[valve_nr] = false;
                state_valve_position[valve_nr] = 4;

                ESP_LOGI(TAG, "CO₂ dropped below low threshold (%.1f ppm). Closing %s.", reading, co2_sensors[i].valve);
            }
            else
            {
                state_valve_position[valve_nr] = 20;

                ESP_LOGI(TAG, "CO₂ still above low threshold (%.1f ppm). Keeping %s open.", reading, co2_sensors[i].valve);
            }
        }
        else
        {
            // Valve currently closed → open only if above high threshold
            if (reading > co2highlevel)
            {
                valve_open[valve_nr] = true;
                state_valve_position[valve_nr] = 20;

                ESP_LOGI(TAG, "CO₂ rose above high threshold (%.1f ppm). Opening %s.", reading, co2_sensors[i].valve);
            }
            else
            {
                state_valve_position[valve_nr] = 4;

                ESP_LOGI(TAG, "CO₂ still below high threshold (%.1f ppm). Keeping %s closed.", reading, co2_sensors[i].valve);
            }
        }
    }

    // Store the count for the rest of the state machine
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.co2_sensors_high = co2_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    ESP_LOGI(TAG, "Number of sensors have high CO₂: %d.", co2_sensors_high);

    // Copy high CO2 state valve positions to temp settings
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; i++)
        {
            statemachine_config[state_config_index_temp].valve_position[i] = state_valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }

    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index_temp);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    // Conditions for transition. This state always transits back to night first or to highco2day if day time.
    if (co2_sensors_high == 0 && elapsed_time >= minimum_state_time) // Night without high CO2 readings
    {
        ESP_LOGI(TAG, "Transit to night.");
        new_state = STATE_NR_NIGHT;
    }
    else if (is_day() == true && co2_sensors_high > 0) // Day with high CO2 readings, no need to wait for minimum_state_time
    {
        ESP_LOGI(TAG, "Transit to highco2day.");
        new_state = STATE_NR_HIGHCO2DAY;
    }
    else
    {
        ESP_LOGI(TAG, "Remain in highco2night state.");
        new_state = STATE_NR_HIGHCO2NIGHT;
    }

    if (state != new_state)
    {
        elapsed_time = 0;
        old_time = (esp_timer_get_time()) / 1000000;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void high_rh_day_transitions(void)
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    int state = STATE_NR_HIGHRHDAY;  // Local state variable
    int state_config_index = 0;      // Index number for state configuration
    int state_config_index_temp = 0; // Index number for state configuration
    int new_state = 0;               // State after logic in this function
    int state_fanspeed = 0;          // Fanspeed from global config struct
    int valve_nr = 0;
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int rhlowlevel = 0;         // Local variable for CO2 low level from general statemachine config
    int rhhighlevel = 0;        // Local variable for CO2 high level from general statemachine config
    int new_time = 0;
    int rh_sensors_high = 0; // Local variable following the struct value

    int state_valve_position[MAX_VALVE_COUNT] = {0}; // Array for valvepositions from statemachine settings

    float reading = 0.2f;

    bool valve_move_locked = 0;
    bool valve_open[MAX_VALVE_COUNT] = {false};

    char state_name[MEDIUM_CONFIG_ITEM] = {};
    char state_name_temp[MEDIUM_CONFIG_ITEM] = {};
    char valve[MEDIUM_CONFIG_ITEM] = {};

    ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");
    if (xQueuePeek(sensor_avg_queue, statemachine_avg_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
        return;
    }

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_HIGHRHDAY, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    // Copy state_name and call function to find index of the config
    strncpy(state_name_temp, STATE_TEMP, sizeof(state_name_temp));
    state_name_temp[sizeof(state_name_temp) - 1] = '\0';
    state_config_index_temp = find_state_index_by_name_ci(state_name_temp);

    ESP_LOGI(TAG, "Statename: %s, state config index is %d", state_name, state_config_index);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed;             // Assign state fanspeed to variable
        minimum_state_time = statemachine_config[state_config_index].duration_minutes; // Minimum time statemachine remain in this state

        // General statemachine config
        rhlowlevel = statemachine_gen_config.rh_low_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;
        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        sm_actuals.rh_sensors_high = 0;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);
    ESP_LOGI(TAG, "Low RH level: %d", rhlowlevel);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }
    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    // Temp valve settings for individual valves starting with default settings for this state.
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < 12; i++)
        {
            state_valve_position[i] = statemachine_config[state_config_index].valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }

    // Iterate through RH sensors to see which one has high CO2 reading to see if default settings apply when high reading is at fan inlet or only one valve
    // needs to open (when high reading is in a room)
    for (int i = 0; i < rh_sensor_counter; i++)
    {
        strncpy(valve, rh_sensors[i].valve, sizeof(valve) - 1);
        valve[sizeof(valve) - 1] = '\0';

        char *substr = strstr(valve, "valve");

        if (substr)
        {
            memmove(substr, substr + 5, strlen(substr + 5) + 1); // 5 is length of "valve"
        }
        valve_nr = atoi(valve);
        reading = rh_sensors[i].rh_reading;

        if (strcmp(valve, "Fan_inlet") != 0)
        {
            // Hysteresis logic
            if (valve_open[valve_nr])
            {
                // Valve is currently open → only close if RH drops below low threshold
                if (reading < rhlowlevel)
                {
                    valve_open[valve_nr] = false;
                    state_valve_position[valve_nr] = 4;
                    ESP_LOGI(TAG, "RH dropped below low threshold (%.1f %%). Closing valve %s.",
                             reading, rh_sensors[i].valve);
                }
                else
                {
                    state_valve_position[valve_nr] = 20;
                    ESP_LOGI(TAG, "RH still above low threshold (%.1f %%). Keeping valve %s open.", reading, rh_sensors[i].valve);
                }
            }
            else
            {
                // Valve is currently closed → only open if RH rises above high threshold
                if (reading > rhhighlevel)
                {
                    valve_open[valve_nr] = true;
                    state_valve_position[valve_nr] = 20;
                    ESP_LOGI(TAG, "RH rose above high threshold (%.1f %%). Opening valve %s.", reading, rh_sensors[i].valve);
                }
                else
                {
                    state_valve_position[valve_nr] = 4;
                    ESP_LOGI(TAG, "RH still below high threshold (%.1f %%). Keeping valve %s closed.", reading, rh_sensors[i].valve);
                }
            }
        }

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
        {
            if (reading > rhlowlevel)
            {
                sm_actuals.rh_sensors_high++;
            }
            rh_sensors_high = sm_actuals.rh_sensors_high;
            xSemaphoreGive(statemachine_mutex);
        }
    }

    ESP_LOGI(TAG, "Number of sensors have high RH: %d", rh_sensors_high);

    // Copy values to temp settings
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; i++)
        {
            statemachine_config[state_config_index_temp].valve_position[i] = state_valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }

    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index_temp);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    // Conditions for transition. This state always transits back to day first or to highco2night if night time.
    if (rh_sensors_high == 0 && elapsed_time >= minimum_state_time) // Day with no high CO2 readings
    {
        ESP_LOGI(TAG, "Transit to day.");
        new_state = STATE_NR_DAY;
    }
    else if (is_day() == false && rh_sensors_high > 0) // Night with high RH readings
    {
        ESP_LOGI(TAG, "Transit to highrhnight.");
        new_state = STATE_NR_HIGHRHNIGHT;
    }
    else
    {
        ESP_LOGI(TAG, "Remain in highrhday state.");
        new_state = STATE_NR_HIGHRHDAY;
    }

    if (state != new_state)
    {
        elapsed_time = 0;
        old_time = (esp_timer_get_time()) / 1000000;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void high_rh_night_transitions()
{
    float statemachine_avg_sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    int state = STATE_NR_HIGHRHNIGHT; // Local state variable
    int state_config_index = 0;       // Index number for state configuration
    int state_config_index_temp = 0;  // Index number for state configuration
    int new_state = 0;                // State after logic in this function
    int state_fanspeed = 0;           // Fanspeed from global config struct
    int valve_nr = 0;
    int minimum_state_time = 0; // Minimum state time from global state settings struct
    int rhlowlevel = 0;         // Local variable for CO2 low level from general statemachine config
    int rhhighlevel = 0;        // Local variable for CO2 high level from general statemachine config
    int new_time = 0;
    int rh_sensors_high = 0; // Local variable following the struct value

    int state_valve_position[MAX_VALVE_COUNT] = {0}; // Array for valvepositions from statemachine settings

    float reading = 0.2f;

    bool valve_move_locked = 0;
    bool valve_open[MAX_VALVE_COUNT] = {false};

    char state_name[MEDIUM_CONFIG_ITEM] = {};
    char state_name_temp[MEDIUM_CONFIG_ITEM] = {};
    char valve[MEDIUM_CONFIG_ITEM] = {};

    ESP_LOGI(TAG, "Read average sensor data from queue for statemachine.");
    if (xQueuePeek(sensor_avg_queue, statemachine_avg_sensor_data, 0) != pdTRUE)
    {
        ESP_LOGE(TAG, "Failed to read data from average sensor queue.");
        return;
    }

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_HIGHRHNIGHT, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    // Copy state_name and call function to find index of the config
    strncpy(state_name_temp, STATE_TEMP, sizeof(state_name_temp));
    state_name_temp[sizeof(state_name_temp) - 1] = '\0';
    state_config_index_temp = find_state_index_by_name_ci(state_name_temp);

    ESP_LOGI(TAG, "Statename: %s, state config index is %d", state_name, state_config_index);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed;             // Assign state fanspeed to variable
        minimum_state_time = statemachine_config[state_config_index].duration_minutes; // Minimum time statemachine remain in this state

        // General statemachine config
        rhlowlevel = statemachine_gen_config.rh_low_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;
        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        sm_actuals.rh_sensors_high = 0;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);
    ESP_LOGI(TAG, "Low RH level: %d", rhlowlevel);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }
    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    // Temp valve settings for individual valves starting with default settings for this state.
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < 12; i++)
        {
            state_valve_position[i] = statemachine_config[state_config_index].valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }

    /// Iterate through RH sensors to see which one has high RH reading to see if default settings apply when high reading is
    // at fan inlet or only one valve needs to open (when high reading is in a room)
    for (int i = 0; i < rh_sensor_counter; i++)
    {
        strncpy(valve, rh_sensors[i].valve, sizeof(valve) - 1);
        valve[sizeof(valve) - 1] = '\0';

        char *substr = strstr(valve, "valve");

        if (substr)
        {
            memmove(substr, substr + 5, strlen(substr + 5) + 1); // 5 is length of "valve"
        }
        valve_nr = atoi(valve);
        reading = rh_sensors[i].rh_reading;

        if (strcmp(valve, "Fan_inlet") != 0)
        {
            // Hysteresis logic
            if (valve_open[valve_nr])
            {
                // Valve is currently open → only close if RH drops below low threshold
                if (reading < rhlowlevel)
                {
                    valve_open[valve_nr] = false;
                    state_valve_position[valve_nr] = 4;
                    ESP_LOGI(TAG, "RH dropped below low threshold (%.1f %%). Closing valve %s.",
                             reading, rh_sensors[i].valve);
                }
                else
                {
                    state_valve_position[valve_nr] = 20;
                    ESP_LOGI(TAG, "RH still above low threshold (%.1f %%). Keeping valve %s open.", reading, rh_sensors[i].valve);
                }
            }
            else
            {
                // Valve is currently closed → only open if RH rises above high threshold
                if (reading > rhhighlevel)
                {
                    valve_open[valve_nr] = true;
                    state_valve_position[valve_nr] = 20;
                    ESP_LOGI(TAG, "RH rose above high threshold (%.1f %%). Opening valve %s.", reading, rh_sensors[i].valve);
                }
                else
                {
                    state_valve_position[valve_nr] = 4;
                    ESP_LOGI(TAG, "RH still below high threshold (%.1f %%). Keeping valve %s closed.", reading, rh_sensors[i].valve);
                }
            }
        }

        if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
        {
            if (reading > rhlowlevel)
            {
                sm_actuals.rh_sensors_high++;
            }
            rh_sensors_high = sm_actuals.rh_sensors_high;
            xSemaphoreGive(statemachine_mutex);
        }
    }

    ESP_LOGI(TAG, "Number of sensors have high RH: %d", rh_sensors_high);

    // Copy values to temp settings
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; i++)
        {
            statemachine_config[state_config_index_temp].valve_position[i] = state_valve_position[i];
        }
        xSemaphoreGive(state_config_mutex);
    }

    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index_temp);
    }
    else
    {
        ESP_LOGW(TAG, "Valves are locked for moving.");
    }

    // Conditions for transition. This state always transits back to night first or to highrhday if night time.
    if (rh_sensors_high == 0 && elapsed_time >= minimum_state_time) // Day with no high CO2 readings
    {
        ESP_LOGI(TAG, "Transit to night.");
        new_state = STATE_NR_NIGHT;
    }
    else if (is_day() == true && rh_sensors_high > 0) // Night with high RH readings
    {
        ESP_LOGI(TAG, "Transit to highrhday.");
        new_state = STATE_NR_HIGHRHDAY;
    }
    else
    {
        ESP_LOGI(TAG, "Remain in highrhnight state.");
        new_state = STATE_NR_HIGHRHNIGHT;
    }

    if (state != new_state)
    {
        elapsed_time = 0;
        old_time = (esp_timer_get_time()) / 1000000;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}
void cooking_transitions(void)
{
    int state = STATE_NR_COOKING;
    int new_time = 0;
    int state_config_index = 0;
    int state_fanspeed = 0;
    int new_state = 0;

    bool valve_move_locked = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_COOKING, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    ESP_LOGI(TAG, "Statename: %s, state config index is %d", state_name, state_config_index);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed; // Assign state fanspeed to variable
        xSemaphoreGive(state_config_mutex);
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }

    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index);
    }
    else
    {
        ESP_LOGI(TAG, "Valves are locked for moving.");
    }

    // Conditions for transition
    if (cooking_times() == false && is_day() == true)
    {
        ESP_LOGI(TAG, "Transit to day.");
        new_state = STATE_NR_DAY;
    }
    else if (cooking_times() == false && is_day() == false)
    {
        ESP_LOGI(TAG, "Transit to night.");
        new_state = STATE_NR_NIGHT;
    }
    else
    {
        ESP_LOGI(TAG, "Remain in cooking state.");
        new_state = STATE_NR_COOKING;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void valve_cycle_day_transitions(void)
{
    int state = STATE_NR_CYCLINGDAY;
    int new_time = 0;
    int state_config_index = 0;
    int state_fanspeed = 0;
    int new_state = 0;
    int co2highlevel = 0;
    int rhhighlevel = 0;
    int co2_sensors_high = 0;
    int rh_sensors_high = 0;

    bool valve_move_locked = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_CYCLINGDAY, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    ESP_LOGI(TAG, "Statename: %s, state config index is %d", state_name, state_config_index);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed; // Assign state fanspeed to variable

        // General statemachine config
        co2highlevel = statemachine_gen_config.co2_high_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;

        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;

        for (int i = 0; i < co2_sensor_counter; i++)
        {
            if (co2_sensors[i].co2_reading > co2highlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high CO2.", co2_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highco2day if minimum state time has expired.");
                sm_actuals.co2_sensors_high++;
            }
        }

        for (int i = 0; i < rh_sensor_counter; i++)
        {
            if (rh_sensors[i].rh_reading > rhhighlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high RH.", rh_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highrhday if minimum state time has expired.");
                sm_actuals.rh_sensors_high++;
            }
        }
        co2_sensors_high = sm_actuals.co2_sensors_high;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);
    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }

    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index);
    }
    else
    {
        ESP_LOGI(TAG, "Valves are locked for moving.");
    }

    // Conditions for transition
    if (co2_sensors_high > 0)
    {
        new_state = STATE_NR_HIGHCO2DAY;
    }
    else if (rh_sensors_high > 0)
    {
        new_state = STATE_NR_HIGHRHDAY;
    }
    else if (valve_cycle_times_day() == false && is_day() == true)
    {
        ESP_LOGI(TAG, "Transit to day.");
        new_state = STATE_NR_DAY;
    }
    else if (valve_cycle_times_day() == false && is_day() == false)
    {
        ESP_LOGI(TAG, "Transit to night.");
        new_state = STATE_NR_NIGHT;
    }
    else
    {
        ESP_LOGI(TAG, "Remain in valve cycling day state.");
        new_state = STATE_NR_CYCLINGDAY;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void valve_cycle_night_transitions(void)
{
    int state = STATE_NR_CYCLINGNIGHT;
    int new_time = 0;
    int state_config_index = 0;
    int state_fanspeed = 0;
    int new_state = 0;
    int co2highlevel = 0;
    int rhhighlevel = 0;
    int co2_sensors_high = 0; // Local variable following the struct value
    int rh_sensors_high = 0;  // Local variable following the struct value

    bool valve_move_locked = 0;

    char state_name[MEDIUM_CONFIG_ITEM] = {};

    // Copy state_name and call function to find index of the config
    strncpy(state_name, STATE_CYCLINGNIGHT, sizeof(state_name));
    state_name[sizeof(state_name) - 1] = '\0';
    state_config_index = find_state_index_by_name_ci(state_name);

    ESP_LOGI(TAG, "Statename: %s, state config index is %d", state_name, state_config_index);

    // Actions for this state
    if (state_config_mutex && xSemaphoreTake(state_config_mutex, (TickType_t)10) == pdTRUE)
    {
        // State specific config
        state_fanspeed = statemachine_config[state_config_index].fanspeed; // Assign state fanspeed to variable

        // General statemachine config
        co2highlevel = statemachine_gen_config.co2_high_level;
        rhhighlevel = statemachine_gen_config.rh_high_level;

        xSemaphoreGive(state_config_mutex);
    }

    select_sensors();

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_actuals.state = state;
        sm_actuals.fanspeed = state_fanspeed;      // Set fanspeed as defined in state settings
        valve_move_locked = sm_actuals.valve_lock; // Check if valve moving is still locked
        sm_actuals.co2_sensors_high = 0;
        sm_actuals.rh_sensors_high = 0;

        for (int i = 0; i < co2_sensor_counter; i++)
        {
            if (co2_sensors[i].co2_reading > co2highlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high CO2.", co2_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highco2day if minimum state time has expired.");
                sm_actuals.co2_sensors_high++;
            }
        }

        for (int i = 0; i < rh_sensor_counter; i++)
        {
            if (rh_sensors[i].rh_reading > rhhighlevel)
            {
                ESP_LOGI(TAG, "Sensor located at %s has high RH.", rh_sensors[i].valve);
                ESP_LOGI(TAG, "Transit to highrhday if minimum state time has expired.");
                sm_actuals.rh_sensors_high++;
            }
        }
        co2_sensors_high = sm_actuals.co2_sensors_high;
        rh_sensors_high = sm_actuals.rh_sensors_high;
        xSemaphoreGive(statemachine_mutex);
    }

    set_fanspeed(state_fanspeed);

    ESP_LOGI(TAG, "High CO2 level: %d", co2highlevel);
    ESP_LOGI(TAG, "High RH level: %d", rhhighlevel);
    ESP_LOGI(TAG, "Number of sensors have high CO2: %d.", co2_sensors_high);
    ESP_LOGI(TAG, "Number of sensors have high RH: %d.", rh_sensors_high);

    new_time = (esp_timer_get_time()) / 1000000;
    if (new_time > old_time)
    {
        elapsed_time += new_time - old_time;
        old_time = new_time;
    }

    ESP_LOGI(TAG, "State: %d, fanspeed: %d, state_time: %d.", state, state_fanspeed, elapsed_time);

    if (valve_move_locked == 0)
    {
        valve_position_statemachine(state_config_index);
    }
    else
    {
        ESP_LOGI(TAG, "Valves are locked for moving.");
    }

    // Conditions for transition
    if (co2_sensors_high > 0)
    {
        ESP_LOGI(TAG, "Transit to highco2night.");
        new_state = STATE_NR_HIGHCO2NIGHT;
    }
    else if (rh_sensors_high > 0)
    {
        ESP_LOGI(TAG, "Transit to highrhnight.");
        new_state = STATE_NR_HIGHRHNIGHT;
    }
    else if (valve_cycle_times_night() == false && is_day() == true)
    {
        ESP_LOGI(TAG, "Transit to day.");
        new_state = STATE_NR_DAY;
    }
    else if (valve_cycle_times_night() == false && is_day() == false)
    {
        ESP_LOGI(TAG, "Transit to night.");
        new_state = STATE_NR_NIGHT;
    }
    else
    {
        ESP_LOGI(TAG, "Remain in valve cycling night state.");
        new_state = STATE_NR_CYCLINGNIGHT;
    }

    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)20) == pdTRUE)
    {
        sm_actuals.state = new_state;
        xSemaphoreGive(statemachine_mutex);
    }
}

void manual_high_speed_transitions(void) {}
