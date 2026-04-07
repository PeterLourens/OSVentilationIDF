// task_pushbutton.c
#include "task_pushbutton.h"

static const char *TAG = "task_pushbutton";

// Static task allocation (pattern matches existing code style) */
static StaticTask_t pushbutton_tcb;
static StackType_t pushbutton_stack[4096];
static const size_t pushbutton_stack_size = 4096; // words (adjust if needed)

/* Debounce and long-press thresholds */
#define DEBOUNCE_MS 50
#define POLL_MS 20
#define LONG_PRESS_MS 1000

// Helper: resolve GPIO used for button (same logic as pushbutton.c)
static gpio_num_t get_pushbutton_gpio_local(void)
{
    if (hw.switch1_pin >= 0)
        return (gpio_num_t)hw.switch1_pin;
    return GPIO_NUM_6;
}

// Toggle backlight safely via i2c_lcd API
static void toggle_backlight(void)
{
    bool cur = i2c_lcd_backlight_get();
    esp_err_t r = i2c_lcd_backlight_set(!cur);
    if (r == ESP_OK)
        LOGI(TAG, "Backlight toggled -> %d", !cur);
    else
        LOGW(TAG, "Backlight toggle failed: %s", esp_err_to_name(r));
}

// Task code: uses ISR queue when available, falls back to polling for robustness
void task_pushbutton_code(void *pvParameters)
{
    (void)pvParameters;
    pushbutton_init();

    gpio_num_t pin = get_pushbutton_gpio_local();
    QueueHandle_t q = pushbutton_get_queue();

    TickType_t last_press_tick = 0;
    bool pressed_state = (gpio_get_level(pin) == 0); // active low

    for (;;)
    {
        // Prefer event-driven path if queue exists
        if (q)
        {
            uint32_t evt;
            if (xQueueReceive(q, &evt, pdMS_TO_TICKS(POLL_MS)) == pdTRUE)
            {
                // Debounce: wait and re-read
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
                int level = gpio_get_level(pin);
                bool pressed = (level == 0);
                if (pressed && !pressed_state)
                {
                    // pressed edge
                    last_press_tick = xTaskGetTickCount();
                    pressed_state = true;
                }
                else if (!pressed && pressed_state)
                {
                    // release edge
                    if (last_press_tick != 0)
                    {
                        TickType_t now = xTaskGetTickCount();
                        uint32_t ms = (uint32_t)((now - last_press_tick) * portTICK_PERIOD_MS);
                        if (ms < LONG_PRESS_MS)
                        {
                            toggle_backlight();
                        }
                        else
                        {
                            LOGI(TAG, "Button long press %lums (no action)", (unsigned long)ms);
                        }
                        last_press_tick = 0;
                    }
                    pressed_state = false;
                }
            }
            else
            {
                // timeout: allow low-power polling; continue loop
            }
        }
        else
        {
            // Fallback polling if queue not available
            vTaskDelay(pdMS_TO_TICKS(POLL_MS));
            int level = gpio_get_level(pin);
            bool pressed = (level == 0);
            if (pressed != pressed_state)
            {
                // potential edge: debounce
                vTaskDelay(pdMS_TO_TICKS(DEBOUNCE_MS));
                level = gpio_get_level(pin);
                bool stable = (level == 0);
                if (stable == pressed)
                {
                    if (pressed)
                    {
                        last_press_tick = xTaskGetTickCount();
                    }
                    else
                    {
                        if (last_press_tick != 0)
                        {
                            TickType_t now = xTaskGetTickCount();
                            uint32_t ms = (uint32_t)((now - last_press_tick) * portTICK_PERIOD_MS);
                            if (ms < LONG_PRESS_MS)
                                toggle_backlight();
                            else
                                LOGI(TAG, "Button long press %lums (no action)", (unsigned long)ms);
                            last_press_tick = 0;
                        }
                    }
                    pressed_state = pressed;
                }
            }
        }
    }
}

// Start/stop helpers
void start_task_pushbutton(void)
{
    if (task_pushbutton != NULL)
    {
        LOGW(TAG, "pushbutton task already running");
        return;
    }

    task_pushbutton = xTaskCreateStatic(task_pushbutton_code, "pushbtn", pushbutton_stack_size, NULL, 5, pushbutton_stack, &pushbutton_tcb);
    if (!task_pushbutton)
        LOGE(TAG, "Failed to create pushbutton task");
    else
        LOGI(TAG, "Pushbutton task started");
}

void stop_task_pushbutton(void)
{
    if (task_pushbutton == NULL)
    {
        LOGI(TAG, "Pushbutton task not running");
        return;
    }

    pushbutton_deinit();
    vTaskDelete(task_pushbutton);
    task_pushbutton = NULL;
    LOGI(TAG, "Pushbutton task stopped");
}
