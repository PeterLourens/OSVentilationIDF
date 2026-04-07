// pushbutton.c
#include "pushbutton.h"

static const char *TAG = "pushbutton";

/* Determine which GPIO to use:
   Prefer hardware definition hw.switch1_pin if valid (>=0), otherwise fallback to GPIO6. */
static gpio_num_t get_pushbutton_gpio(void)
{
    if (hw.switch1_pin >= 0)
        return (gpio_num_t)hw.switch1_pin;
    return GPIO_NUM_6;
}

/* Queue used by ISR to notify the pushbutton task of edges */
static QueueHandle_t s_pushbutton_evt_queue = NULL;

/* Last debounced state (true = pressed) */
static volatile bool s_pushbutton_state = false;

/* ISR: enqueue a simple event (timestamp not needed) */
static void IRAM_ATTR pushbutton_isr_handler(void *arg)
{
    uint32_t one = 1;
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    if (s_pushbutton_evt_queue)
        xQueueSendFromISR(s_pushbutton_evt_queue, &one, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken)
        portYIELD_FROM_ISR();
}

void pushbutton_init(void)
{
    gpio_num_t pin = get_pushbutton_gpio();

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE};
    gpio_config(&io_conf);

    if (s_pushbutton_evt_queue == NULL)
    {
        s_pushbutton_evt_queue = xQueueCreate(8, sizeof(uint32_t));
        if (s_pushbutton_evt_queue == NULL)
            ESP_LOGW(TAG, "Failed to create pushbutton queue");
    }

    // Do not re-install ISR service if app_main already did it.
    // If you keep this call, tolerate ESP_ERR_INVALID_STATE.
    esp_err_t r = gpio_install_isr_service(0);
    if (r != ESP_OK && r != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGW(TAG, "gpio_install_isr_service failed: %s", esp_err_to_name(r));
    }

    gpio_isr_handler_add(pin, pushbutton_isr_handler, NULL);

    s_pushbutton_state = (gpio_get_level(pin) == 0);
    ESP_LOGI(TAG, "pushbutton_init: gpio=%d initial=%d", pin, s_pushbutton_state);
}

void pushbutton_deinit(void)
{
    gpio_num_t pin = get_pushbutton_gpio();
    gpio_isr_handler_remove(pin);

    // Do not uninstall the global ISR service here if app_main installed it.
    // Only delete the queue if it exists and no other users rely on it.
    if (s_pushbutton_evt_queue)
    {
        vQueueDelete(s_pushbutton_evt_queue);
        s_pushbutton_evt_queue = NULL;
    }

    ESP_LOGI(TAG, "pushbutton_deinit");
}

bool pushbutton_get_state(void)
{
    return s_pushbutton_state;
}

/* Internal accessor for the task to receive the queue handle */
QueueHandle_t pushbutton_get_queue(void)
{
    return s_pushbutton_evt_queue;
}
