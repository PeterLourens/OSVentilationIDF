#include "valvecontrol.h"
#include "statemachine.h"
#include "esp_err.h"

static const char *TAG = "valve control";

/* Patterns */
static const uint8_t pattern1[4] = {0b00000101, 0b00001001, 0b00001010, 0b00000110};
static const uint8_t pattern2[4] = {0b01010000, 0b10010000, 0b10100000, 0b01100000};

/* ---------------------- Generic helpers ---------------------- */

/* Configure three GPIOs as outputs (idempotent). Returns ESP_OK or error. */
esp_err_t gpio_setup_3pins(int dataPin, int clockPin, int latchPin)
{
    if (dataPin < 0 || clockPin < 0 || latchPin < 0)
        return ESP_ERR_INVALID_ARG;

    gpio_config_t cfg = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << dataPin) | (1ULL << clockPin) | (1ULL << latchPin),
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .intr_type = GPIO_INTR_DISABLE};
    return gpio_config(&cfg);
}

/* short microsecond delay using ROM API */
void short_delay_us(int us)
{
    if (us > 0)
        esp_rom_delay_us(us);
}

/* Shift out one byte MSB-first (equivalent to Arduino shiftOut MSBFIRST) */
void shift_out_msbfist(int dataPin, int clockPin, uint8_t val)
{
    for (int bit = 7; bit >= 0; --bit)
    {
        int bitval = (val >> bit) & 1;
        gpio_set_level(dataPin, bitval);
        short_delay_us(1);
        gpio_set_level(clockPin, 1);
        short_delay_us(1);
        gpio_set_level(clockPin, 0);
        short_delay_us(1);
    }
}

/* Latch pulse: generate a short RCLK rising edge (caller may set latch low beforehand) */
void latch_pulse(int latchPin)
{
    gpio_set_level(latchPin, 1);
    short_delay_us(1);
    gpio_set_level(latchPin, 0);
    short_delay_us(1);
}

/* Shift three zero bytes (nearest first) and latch — mirrors Arduino reference */
void all_outputs_off(int dataPin, int clockPin, int latchPin)
{
    if (gpio_setup_3pins(dataPin, clockPin, latchPin) != ESP_OK)
        return;

    /* follow original order: shift output[2], output[1], output[0] */
    gpio_set_level(latchPin, 0);
    shift_out_msbfist(dataPin, clockPin, 0x00); // nearest chip
    shift_out_msbfist(dataPin, clockPin, 0x00); // middle chip
    shift_out_msbfist(dataPin, clockPin, 0x00); // farthest chip
    latch_pulse(latchPin);
}

void read_valve_pos_in_valvecontrol(void)
{
    int32_t positions[MAX_VALVE_COUNT] = {0};

    nvs_handle_t nvs;
    esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open NVS namespace 'valves': %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "Reading valve positions from NVS");

    for (int i = 0; i < MAX_VALVE_COUNT; i++)
    {
        int32_t pos = -999; // sentinel so we see if it changes
        char key[16];
        snprintf(key, sizeof(key), "valve%d", i);

        esp_err_t r = nvs_get_i32(nvs, key, &pos);

        ESP_LOGD(TAG, "Key %s -> err=%s, value=%ld", key, esp_err_to_name(r), (long)pos);

        if (r != ESP_OK)
        {
            pos = 0;
        }

        if (valve_control_data_mutex &&
            xSemaphoreTake(valve_control_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            valvecontroldata.actual_valve_position[i] = (int)pos;
            xSemaphoreGive(valve_control_data_mutex);
        }
    }

    nvs_close(nvs);

    // Write all values under one lock
    if (valve_control_data_mutex &&
        xSemaphoreTake(valve_control_data_mutex, pdMS_TO_TICKS(10)) == pdTRUE)
    {
        for (int i = 0; i < MAX_VALVE_COUNT; i++)
        {
            valvecontroldata.valve_number[i] = positions[i];
        }
        xSemaphoreGive(valve_control_data_mutex);
    }
}

// Control valves from web interface and from statemachine. Uses NVS namespace "valves" to read/write valve positions.
void control_valves(void)
{
    static const char *TAG = "control_valves";

    // Temporary variables used after selection of I/O
    int clockPin = 0;
    int latchPin = 0;
    int dataPin = 0;
    int valve_number = 0;
    int direction = 0;
    int valve_pos = 0;
    int valve_position_change = 0;
    int new_valve_position_change = 0;
    int new_valve_position = 0;
    int sm_state = 0;

    bool store_valve_position = false;
    bool check_valve_position = false;
    bool valvecontrol_from_post = false;

    // No temporary 'state' pointer used here (statemachine state changes are handled elsewhere)
    if (valve_control_data_mutex && xSemaphoreTake(valve_control_data_mutex, (TickType_t)10) == pdTRUE)
    {
        store_valve_position = valvecontroldata.write_new_position;
        check_valve_position = valvecontroldata.check_position;
        xSemaphoreGive(valve_control_data_mutex);
    }

    ESP_LOGD(TAG, "Store new valve Position: %d, Check valve position: %d", store_valve_position ? 1 : 0, check_valve_position ? 1 : 0);

    /* Ensure statemachine is in stopped mode before allowing manual valve moves */
    if (statemachine_mutex && xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
    {
        sm_state = sm_actuals.state;
        xSemaphoreGive(statemachine_mutex);
    }

    if (valve_control_data_mutex && xSemaphoreTake(valve_control_data_mutex, (TickType_t)10) == pdTRUE)
    {
        valvecontrol_from_post = valvecontroldata.valve_move_request_from_post;
        xSemaphoreGive(valve_control_data_mutex);
    }

    if (valvecontrol_from_post == true && sm_state != STATE_NR_STOPPED)
    {
        ESP_LOGW(TAG, "Statemachine not stopped (state=%d) — manual valve control aborted", sm_state);
        return;
    }

    // Read actual valve positions from NVS (namespace "valves"). If a key is missing default to 0.
    nvs_handle_t nvs;
    esp_err_t err = nvs_open("valves", NVS_READONLY, &nvs);
    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "Failed to open NVS namespace 'valves' for read: %d", err);
    }

    for (int i = 0; i < MAX_VALVE_COUNT; i++)
    {
        if (valve_control_data_mutex && xSemaphoreTake(valve_control_data_mutex, (TickType_t)50) == pdTRUE)
        {
            valve_number = valvecontroldata.valve_number[i];
            valve_position_change = valvecontroldata.position_change[i];
            direction = valvecontroldata.direction[i];
            xSemaphoreGive(valve_control_data_mutex);
        }

        // Read current position (default 0 on error)
        int32_t pos = 0;
        if (err == ESP_OK)
        {
            char key[16];
            snprintf(key, sizeof(key), "valve%d", i);
            esp_err_t r = nvs_get_i32(nvs, key, &pos);
            if (r != ESP_OK)
            {
                ESP_LOGW(TAG, "NVS read for %s failed: %d; assuming position 0", key, r);
                pos = 0;
            }
        }
        valve_pos = (int)pos;

        // Assign the correct IO based on valve number
        if (valve_number < 6)
        {
            latchPin = LATCHPIN1;
            clockPin = CLOCKPIN1;
            dataPin = DATAPIN1;
        }
        else
        {
            latchPin = LATCHPIN2;
            clockPin = CLOCKPIN2;
            dataPin = DATAPIN2;
        }

        bool moved = false;
        esp_err_t vc_ret = ESP_OK;

        if (check_valve_position)
        {
            if (direction == 0)
            { // Close
                new_valve_position_change = (valve_position_change < valve_pos) ? valve_position_change : valve_pos;
                new_valve_position = valve_pos - new_valve_position_change;
            }
            else
            { // Open
                int available = 24 - valve_pos;
                new_valve_position_change = (valve_position_change < available) ? valve_position_change : available;
                new_valve_position = valve_pos + new_valve_position_change;
            }
            ESP_LOGI(TAG, "Valve: %d, req.: %d, cur.: %d, move: %d, dir.: %s", valve_number, valve_position_change, valve_pos, new_valve_position_change, (direction == 0 ? "close" : "open"));
            vc_ret = valvecontrol(direction, new_valve_position_change, valve_number, dataPin, clockPin, latchPin);
            moved = (vc_ret == ESP_OK && new_valve_position_change > 0);
        }
        else if (!check_valve_position)
        {
            vc_ret = valvecontrol(direction, valve_position_change, valve_number, dataPin, clockPin, latchPin);
            moved = (vc_ret == ESP_OK && valve_position_change > 0);
        }
        else
        {
            ESP_LOGE(TAG, "Position change outside valve range of 0 and 24");
        }

        // If requested, write updated positions to NVS immediately after successful move
        if (store_valve_position && check_valve_position && moved)
        {
            // write single key (retry up to 3 times)
            nvs_close(nvs); // close readonly handle
            nvs_handle_t nvsw;
            esp_err_t werr = nvs_open("valves", NVS_READWRITE, &nvsw);
            if (werr != ESP_OK)
            {
                ESP_LOGE(TAG, "Failed to open NVS for write: %d", werr);
            }
            else
            {
                char key[16];
                snprintf(key, sizeof(key), "valve%d", i);
                esp_err_t s = ESP_ERR_INVALID_STATE;
                for (int attempt = 0; attempt < 3; ++attempt)
                {
                    s = nvs_set_i32(nvsw, key, new_valve_position);
                    if (s == ESP_OK)
                        s = nvs_commit(nvsw);
                    if (s == ESP_OK)
                        break;
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                if (s != ESP_OK)
                {
                    ESP_LOGE(TAG, "Failed to write valve position %s after retries: %d", key, s);
                }
                else
                {
                    // Update in-memory actual positions
                    if (valve_control_data_mutex && xSemaphoreTake(valve_control_data_mutex, (TickType_t)10) == pdTRUE)
                    {
                        valvecontroldata.actual_valve_position[i] = new_valve_position;
                        xSemaphoreGive(valve_control_data_mutex);
                    }
                    ESP_LOGD(TAG, "Valve position valve%d stored successfully in NVS: %d", i, new_valve_position);
                }
                nvs_close(nvsw);
                // Re-open readonly handle for further reads
                err = nvs_open("valves", NVS_READONLY, &nvs);
                if (err != ESP_OK)
                {
                    ESP_LOGW(TAG, "Failed to re-open NVS namespace 'valves' for read: %d", err);
                }
            }
        }
    }

    if (err == ESP_OK)
        nvs_close(nvs);

    // No statemachine 'state' changes done here; leave state handling to statemachine task.
}

void valve_position_statemachine(int state_config_index)
{
    static const char *TAG = "valve pos statemachine";

    int desired_positions[MAX_VALVE_COUNT] = {0};
    int sum_move = 0;
    int current = 0;
    int desired = 0;

    if (state_config_index < 0)
    {
        ESP_LOGE(TAG, "valve_position_statemachine: invalid state_config_index=%d", state_config_index);
        return;
    }

    /* Read desired positions from the state config under state_config_mutex */
    if (state_config_mutex == NULL || xSemaphoreTake(state_config_mutex, (TickType_t)10) != pdTRUE)
    {
        ESP_LOGW(TAG, "valve_position_statemachine: could not take state_config_mutex");
        return;
    }

    // Read actual valve data from NVS
    // read_valve_pos_in_valvecontrol();

    for (int i = 0; i < MAX_VALVE_COUNT; i++)
    {
        desired_positions[i] = statemachine_config[state_config_index].valve_position[i];
    }
    xSemaphoreGive(state_config_mutex);

    // Update global valvecontroldata with move requests (compute delta from in-memory actuals).
    // Protect with valve_control_data_mutex.
    if (valve_control_data_mutex == NULL || xSemaphoreTake(valve_control_data_mutex, (TickType_t)50) != pdTRUE)
    {
        ESP_LOGW(TAG, "valve_position_statemachine: could not take valve_control_data_mutex");
        return;
    }

    // Populate valvecontroldata arrays so control_valves() can process them.
    // We set check_position=true to compute movement relative to current actual_valve_position[].
    // Valve numbers are set 0..N-1 to match indices.
    for (int v = 0; v < MAX_VALVE_COUNT; ++v)
    {
        current = valvecontroldata.actual_valve_position[v];
        desired = desired_positions[v];

        ESP_LOGD(TAG, "Valve%d: %d", v, current);

        valvecontroldata.valve_number[v] = v;

        if (desired == current)
        {
            valvecontroldata.position_change[v] = 0;
            valvecontroldata.direction[v] = 0; /* direction irrelevant when no movement */
        }
        else if (desired > current)
        {
            valvecontroldata.position_change[v] = desired - current;
            valvecontroldata.direction[v] = 1; /* open/forward */
            sum_move = sum_move + valvecontroldata.position_change[v];
        }
        else /* desired < current */
        {
            valvecontroldata.position_change[v] = current - desired;
            valvecontroldata.direction[v] = 0; /* close/backward */
            sum_move = sum_move + valvecontroldata.position_change[v];
        }
        ESP_LOGD(TAG, "Valve %d: cur=%d desired=%d change=%d dir=%s", v, current, desired, valvecontroldata.position_change[v], valvecontroldata.direction[v] ? "open" : "close");
    }

    if (sum_move > 0)
    {
        valvecontroldata.check_position = true;
        valvecontroldata.write_new_position = true;
    }
    else
    {
        valvecontroldata.check_position = true;
        valvecontroldata.write_new_position = false;
    }

    /* Clear any previous POST-request flag — movement is requested programmatically now */
    valvecontroldata.valve_move_request_from_post = false;
    xSemaphoreGive(valve_control_data_mutex);

    ESP_LOGD(TAG, "Valve targets from state %d copied to valvecontroldata. Calling control_valves() to execute.", state_config_index);

    /* Invoke control_valves() to perform movements. It will check statemachine state internally. */
    if (sum_move > 0)
    {
        ESP_LOGI(TAG, "sum_move is >0 (%d)", sum_move);
        control_valves();
    }
    else
    {
        ESP_LOGI(TAG, "Sum move is 0, no need to call control_valves function");
    }
}

// direction: 1 = open (forward), 0 = close (backward)
// position_change: 0..24 (1 => 24 cycles as in original)
// valve_number: 0..11 (values >=6 are remapped as original)
// dataPin, clockPin, latchPin: GPIO pins for the chain

esp_err_t valvecontrol(int direction, int position_change, int valve_number, int dataPin, int clockPin, int latchPin)
{
    static const char *TAG = "Valvecontrol";

    int output[3][4] = {{0}};
    int i, j, k;
    const int cycles = 24; // cycles per full rotation (same as original)

    /* Validate direction and position_change like original */
    if (!(direction == 1 || direction == 0))
    {
        ESP_LOGE(TAG, "Invalid direction: %d", direction);
        return ESP_ERR_INVALID_ARG;
    }
    if (position_change < 0 || position_change > 24)
    {
        ESP_LOGE(TAG, "Error in valvecontrol. Requested position change is: %d. Position change should be between 0 and 24", position_change);
        return ESP_ERR_INVALID_ARG;
    }

    /* Take lock_valve_move mutex and set flag like original (if available) */
    if (statemachine_mutex)
    {
        if (xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
        {
            sm_actuals.valve_lock = true;
            xSemaphoreGive(statemachine_mutex);
        }
    }

    /* Normalize valve number same as original */
    if (valve_number >= 6)
        valve_number -= 6;

    /* Build outputs exactly like Arduino switch-case */
    switch (valve_number)
    {
    case 0:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = pattern1[i];
            output[1][i] = 0;
            output[2][i] = 0;
        }
        break;
    case 1:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = pattern2[i];
            output[1][i] = 0;
            output[2][i] = 0;
        }
        break;
    case 2:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = pattern1[i];
            output[2][i] = 0;
        }
        break;
    case 3:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = pattern2[i];
            output[2][i] = 0;
        }
        break;
    case 4:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = 0;
            output[2][i] = pattern1[i];
        }
        break;
    case 5:
        for (i = 0; i < 4; i++)
        {
            output[0][i] = 0;
            output[1][i] = 0;
            output[2][i] = pattern2[i];
        }
        break;
    default:
        ESP_LOGE(TAG, "Invalid valve_number: %d", valve_number);
        /* Clear lock flag if set then return */
        if (statemachine_mutex)
        {
            if (xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
            {
                sm_actuals.valve_lock = false;
                xSemaphoreGive(statemachine_mutex);
            }
        }
        return ESP_ERR_INVALID_ARG;
    }

    /* Configure GPIO pins */
    if (gpio_setup_3pins(dataPin, clockPin, latchPin) != ESP_OK)
    {
        ESP_LOGE(TAG, "GPIO config failed in valvecontrol");
        if (statemachine_mutex)
        {
            if (xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
            {
                sm_actuals.valve_lock = false;
                xSemaphoreGive(statemachine_mutex);
            }
        }
        return ESP_FAIL;
    }

    /* Ensure pins start low */
    gpio_set_level(dataPin, 0);
    gpio_set_level(clockPin, 0);
    gpio_set_level(latchPin, 0);

    /* Forward / open */
    if (direction == 1)
    {
        for (j = 0; j < (cycles * position_change); j++)
        {
            for (k = 0; k < 4; k++)
            {
                gpio_set_level(latchPin, 0);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[2][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[1][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[0][k]);
                gpio_set_level(latchPin, 1);
                /* match original delay */
                vTaskDelay(pdMS_TO_TICKS(10));
                gpio_set_level(latchPin, 0);
            }
        }
        all_outputs_off(dataPin, clockPin, latchPin);
    }
    /* Backward / close */
    else
    {
        for (j = 0; j < (cycles * position_change); j++)
        {
            for (k = 3; k >= 0; k--)
            {
                gpio_set_level(latchPin, 0);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[2][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[1][k]);
                shift_out_msbfist(dataPin, clockPin, (uint8_t)output[0][k]);
                gpio_set_level(latchPin, 1);
                vTaskDelay(pdMS_TO_TICKS(10));
                gpio_set_level(latchPin, 0);
            }
        }
        all_outputs_off(dataPin, clockPin, latchPin);
    }

    /* Release lock flag */
    if (statemachine_mutex)
    {
        if (xSemaphoreTake(statemachine_mutex, (TickType_t)10) == pdTRUE)
        {
            sm_actuals.valve_lock = false;
            xSemaphoreGive(statemachine_mutex);
        }
    }
    return ESP_OK;
}