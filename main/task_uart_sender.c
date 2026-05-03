/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

#include "task_uart_sender.h"

static const char *TAG = "uart_sender";

static StackType_t uart_sender_stack[UART_SENDER_STACK_WORDS];
static StaticTask_t uart_sender_tcb;

static QueueHandle_t uart_tx_queue = NULL;

// Initialize UART0 using pins from hw
static esp_err_t uart_sender_hw_init(void)
{
    // Abort if HAL does not provide UART0 pins
    if (hw.uart0_tx_gpio < 0 || hw.uart0_rx_gpio < 0)
    {
        ESP_LOGE(TAG, "UART0 pins not available in hardware_caps (tx=%d rx=%d)", hw.uart0_tx_gpio, hw.uart0_rx_gpio);
        return ESP_ERR_INVALID_ARG;
    }

    uart_config_t uart_config = {
        .baud_rate = SENDER_UART_BAUD,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };

    // Install driver: rx buffer 0 (we don't read), tx buffer SENDER_TX_BUF_SIZE
    // esp_err_t err = uart_driver_install(SENDER_UART_NUM, 0, SENDER_TX_BUF_SIZE, 0, NULL, 0);
    esp_err_t err = uart_driver_install(SENDER_UART_NUM, SENDER_UART_RX_BUF_SIZE, SENDER_TX_BUF_SIZE, 0, NULL, 0);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    err = uart_param_config(SENDER_UART_NUM, &uart_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    int tx_pin = hw.uart0_tx_gpio;
    int rx_pin = hw.uart0_rx_gpio;

    err = uart_set_pin(SENDER_UART_NUM, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "UART initialized UART%d TX=%d RX=%d @%d", SENDER_UART_NUM, tx_pin, rx_pin, SENDER_UART_BAUD);
    return ESP_OK;
}

// Non-blocking formatted send:
// - formats into a heap buffer
// - attempts to enqueue pointer to queue with zero wait
// - if queue full, drops message and frees buffer
// Caller never blocks (unless heap allocation blocks briefly).
void uart_sender_sendf(const char *fmt, ...)
{
    if (fmt == NULL)
        return;

    // Allocate buffer on heap to avoid large stack usage in caller
    char *buf = (char *)malloc(UART_SENDER_MAX_LINE);
    if (!buf)
    {
        ESP_LOGW(TAG, "malloc failed for uart_sender_sendf");
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, UART_SENDER_MAX_LINE - 3, fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        free(buf);
        return;
    }

    // Truncate if necessary
    if (n > UART_SENDER_MAX_LINE - 3)
    {
        n = UART_SENDER_MAX_LINE - 3;
        buf[n] = '\0';
    }

    // Append CRLF
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = '\0';

    // Try to enqueue without blocking
    BaseType_t ok = xQueueSend(uart_tx_queue, &buf, 0);
    if (ok != pdTRUE)
    {
        // Queue full: drop message
        ESP_LOGW(TAG, "uart_tx_queue full, dropping log");
        free(buf);
    }
}

// TX task: dequeues heap buffers and writes them to UART (blocking write)
static void uart_tx_task(void *pvParameters)
{
    (void)pvParameters;
    char *line = NULL;

    for (;;)
    {
        if (xQueueReceive(uart_tx_queue, &line, portMAX_DELAY) == pdTRUE)
        {
            if (line)
            {
                size_t len = strlen(line);
                // Blocking write until all bytes are written
                int written = uart_write_bytes(SENDER_UART_NUM, line, len);
                if (written < 0)
                {
                    ESP_LOGW(TAG, "uart_write_bytes error: %d", written);
                }
                // free the heap buffer
                free(line);
                line = NULL;
            }
        }
    }
}

// User-visible periodic sender task (keeps original structure)
void task_uart_sender_code(void *pvParameters)
{
    (void)pvParameters;

    // Example: this task can produce logs by calling uart_sender_sendf()
    uint32_t counter = 0;

    for (;;)
    {
        uart_sender_sendf("sender: uptime_ms=%u counter=%u", (unsigned)esp_log_timestamp(), counter++);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// Start function: initialize UART, create queue and tasks.
void start_task_uart_sender(void)
{
    // Initialize hardware first
    if (uart_sender_hw_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "UART hardware init failed; not starting sender tasks");
        return;
    }

    // Create queue for pointers to heap buffers
    if (uart_tx_queue == NULL)
    {
        uart_tx_queue = xQueueCreate(UART_SENDER_QUEUE_LEN, sizeof(char *));
        if (uart_tx_queue == NULL)
        {
            ESP_LOGE(TAG, "Failed to create uart_tx_queue");
            return;
        }
    }

    // Create TX task (dynamic allocation is fine for the TX worker)
    if (task_uart_sender == NULL)
    {
        BaseType_t r = xTaskCreate(uart_tx_task, "uart_tx_task", 2048, NULL, tskIDLE_PRIORITY + 6, &task_uart_sender);
        if (r != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create uart_tx_task");
            task_uart_sender = NULL;
            vQueueDelete(uart_tx_queue);
            uart_tx_queue = NULL;
            return;
        }
    }

    // Create the user-visible sender task as a static task to match your structure
    if (task_uart_sender == NULL)
    {
        task_uart_sender = xTaskCreateStatic(task_uart_sender_code, "uart_sender", UART_SENDER_STACK_WORDS, NULL, tskIDLE_PRIORITY + 5, uart_sender_stack, &uart_sender_tcb);

        if (task_uart_sender == NULL)
        {
            ESP_LOGE(TAG, "UART sender task creation failed");
            /* cleanup if needed */
            if (task_uart_sender)
            {
                vTaskDelete(task_uart_sender);
                task_uart_sender = NULL;
            }
            if (uart_tx_queue)
            {
                vQueueDelete(uart_tx_queue);
                uart_tx_queue = NULL;
            }
            return;
        }
    }

    ESP_LOGI(TAG, "UART sender started");
}