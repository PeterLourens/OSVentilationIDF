/*
    This file is part of the OSVentilationIDF project.

    Copyright (C) 2023-2026 Peter Lourens
*/

/**
 * @file task_uart_sender.c
 * @brief UART sender task implementation and buffer pool management.
 */

#include <esp_attr.h>
#include "task_uart_sender.h"

static const char *TAG = "uart_sender";

EXT_RAM_BSS_ATTR static StackType_t uart_sender_stack[UART_SENDER_STACK_WORDS];
static StaticTask_t uart_sender_tcb;

static QueueHandle_t uart_tx_queue = NULL;

// Pool storage (allocated in SPIRAM if available)
static uint8_t *uart_pool_buf = NULL;

// Pool management
static int uart_pool_free_idx[UART_POOL_COUNT]; // stack of free indices
static int uart_pool_free_top = 0;              // number of free entries
static SemaphoreHandle_t uart_pool_lock = NULL; // protect pool operations

// Telemetry counters
static size_t uart_sender_dropped_msgs = 0;
static size_t uart_sender_pool_exhaust = 0;

static bool uart_driver_installed_flag = false;

// Forward declarations
static esp_err_t uart_sender_hw_init(void);
static void uart_tx_task(void *pvParameters);
static void uart_pool_init(void);
static char *uart_pool_alloc(void);
static void uart_pool_free(char *buf);

// Diagnostics accessors
size_t uart_sender_get_dropped_count(void)
{
    return uart_sender_dropped_msgs;
}

size_t uart_sender_get_pool_exhaust_count(void)
{
    return uart_sender_pool_exhaust;
}

// Initialize pool free list; must be called before using pool
static void uart_pool_init(void)
{
    if (uart_pool_lock == NULL)
    {
        uart_pool_lock = xSemaphoreCreateMutex();
        if (uart_pool_lock == NULL)
        {
            ESP_LOGE(TAG, "Failed to create uart_pool_lock");
            return;
        }
    }

    if (uart_pool_buf == NULL)
    {
        size_t total_size = (size_t)UART_POOL_COUNT * UART_POOL_BUF_SIZE;
        uart_pool_buf = heap_caps_malloc(total_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (uart_pool_buf == NULL)
        {
            ESP_LOGW(TAG, "uart_pool_init: SPIRAM allocation failed, falling back to internal RAM");
            uart_pool_buf = malloc(total_size);
            if (uart_pool_buf == NULL)
            {
                ESP_LOGE(TAG, "uart_pool_init: failed to allocate uart_pool_buf");
                return;
            }
        }
    }

    xSemaphoreTake(uart_pool_lock, portMAX_DELAY);
    uart_pool_free_top = 0;
    for (int i = 0; i < UART_POOL_COUNT; ++i)
    {
        uart_pool_free_idx[uart_pool_free_top++] = i;
    }
    xSemaphoreGive(uart_pool_lock);
}

// Non-blocking allocate: returns pointer to buffer or NULL if none available
static char *uart_pool_alloc(void)
{
    char *ret = NULL;
    if (uart_pool_lock == NULL || uart_pool_buf == NULL)
        return NULL;

    if (xSemaphoreTake(uart_pool_lock, 0) == pdTRUE)
    {
        if (uart_pool_free_top > 0)
        {
            int idx = uart_pool_free_idx[--uart_pool_free_top];
            ret = (char *)(uart_pool_buf + ((size_t)idx * UART_POOL_BUF_SIZE));
        }
        xSemaphoreGive(uart_pool_lock);
    }
    return ret;
}

// Free buffer previously allocated from pool
static void uart_pool_free(char *buf)
{
    if (buf == NULL || uart_pool_lock == NULL)
        return;

    // compute index
    ptrdiff_t offset = buf - (char *)uart_pool_buf;
    if (offset < 0)
        return;
    int idx = offset / UART_POOL_BUF_SIZE;
    if (idx < 0 || idx >= UART_POOL_COUNT)
        return;

    if (xSemaphoreTake(uart_pool_lock, portMAX_DELAY) == pdTRUE)
    {
        uart_pool_free_idx[uart_pool_free_top++] = idx;
        xSemaphoreGive(uart_pool_lock);
    }
}

int uart_log_vprintf(const char *fmt, va_list args)
{
    if (fmt == NULL)
        return 0;

    char local_buf[UART_POOL_BUF_SIZE];
    int n = vsnprintf(local_buf, UART_POOL_BUF_SIZE - 3, fmt, args);
    if (n < 0)
        return 0;

    if (n > UART_POOL_BUF_SIZE - 3)
    {
        n = UART_POOL_BUF_SIZE - 3;
        local_buf[n] = '\0';
    }
    else
    {
        local_buf[n] = '\0';
    }

    // Keep the original log output on the JTAG/serial monitor / console
    if (n > 0)
    {
        fwrite(local_buf, 1, n, stdout);
        fflush(stdout);
    }

    if (uart_tx_queue == NULL)
    {
        return n;
    }

    // Allocate buffer from pool (non-blocking)
    char *buf = uart_pool_alloc();
    if (!buf)
        return n;

    memcpy(buf, local_buf, n + 1);

    // Parse ESP-IDF log format and extract tag:message without timestamp
    char *parsed_msg = buf;
    int parsed_len = n;

    // Skip ANSI escape codes at the beginning
    char *start = buf;
    while (start < buf + n && *start == '\033')
    {
        char *m = strchr(start, 'm');
        if (m)
        {
            start = m + 1;
        }
        else
        {
            break;
        }
    }

    // ESP-IDF format: "L (timestamp) tag: message"
    if (start < buf + n && *start >= 'A' && *start <= 'Z' && start[1] == ' ' && start[2] == '(')
    {
        // Find the closing ) after timestamp
        char *close_paren = strchr(start + 2, ')');
        if (close_paren && close_paren[1] == ' ')
        {
            // Skip "L (timestamp) " and keep "tag: message"
            parsed_msg = close_paren + 2;
            parsed_len = n - (parsed_msg - buf);
        }
    }
    else
    {
        // Not a standard ESP-IDF log, use the message after skipping ANSI codes
        parsed_msg = start;
        parsed_len = n - (start - buf);
    }

    // Skip if message is empty or just whitespace/newlines
    if (parsed_len <= 1 || (parsed_len == 2 && parsed_msg[0] == '\r' && parsed_msg[1] == '\n'))
    {
        uart_pool_free(buf);
        return n; // Return original length for ESP-IDF
    }

    // Trim trailing whitespace/newlines
    while (parsed_len > 0 && (parsed_msg[parsed_len - 1] == '\n' || parsed_msg[parsed_len - 1] == '\r' || parsed_msg[parsed_len - 1] == ' '))
    {
        parsed_len--;
    }

    if (parsed_len <= 0)
    {
        uart_pool_free(buf);
        return n;
    }

    // Copy parsed message back to buffer
    memmove(buf, parsed_msg, parsed_len);
    buf[parsed_len] = '\0';

    // Add CRLF for UART
    if (parsed_len + 2 < UART_POOL_BUF_SIZE)
    {
        buf[parsed_len++] = '\r';
        buf[parsed_len++] = '\n';
        buf[parsed_len] = '\0';
    }

    // Enqueue pointer; non-blocking
    BaseType_t ok = xQueueSend(uart_tx_queue, &buf, 0);
    if (ok != pdTRUE)
    {
        uart_pool_free(buf);
        return 0;
    }

    return n;
}

// -------------------- Hardware init -------------------- //
static esp_err_t uart_sender_hw_init(void)
{
    ESP_LOGI(TAG, "hw_init: start");

    if (hw.uart0_tx_gpio < 0 || hw.uart0_rx_gpio < 0)
    {
        ESP_LOGE(TAG, "hw_init: invalid pins tx=%d rx=%d", hw.uart0_tx_gpio, hw.uart0_rx_gpio);
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

    ESP_LOGI(TAG, "uart_sender_hw_init: configuring UART%d (TX=%d RX=%d) @%d", SENDER_UART_NUM, hw.uart0_tx_gpio, hw.uart0_rx_gpio, SENDER_UART_BAUD);

    // Configure UART parameters first (safe to call before driver install)
    esp_err_t err = uart_param_config(SENDER_UART_NUM, &uart_config);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(err));
        return err;
    }

    // Set pins (use GPIO numbers, not physical pad indices)
    err = uart_set_pin(SENDER_UART_NUM, hw.uart0_tx_gpio, hw.uart0_rx_gpio, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    // Small delay to let pin matrix settle before driver install
    vTaskDelay(pdMS_TO_TICKS(10));

    // Install driver. Try default flags first; if it fails, try IRAM flag (if available).
    ESP_LOGI(TAG, "uart_sender_hw_init: installing driver rx=%d tx=%d", SENDER_UART_RX_BUF_SIZE, SENDER_TX_BUF_SIZE);

    err = uart_driver_install(SENDER_UART_NUM, SENDER_UART_RX_BUF_SIZE, SENDER_TX_BUF_SIZE, 0, NULL, 0);

    if (err != ESP_OK)
    {
        ESP_LOGW(TAG, "uart_driver_install(default) failed: %s", esp_err_to_name(err));
#ifdef ESP_INTR_FLAG_IRAM
        ESP_LOGI(TAG, "uart_sender_hw_init: retrying uart_driver_install with ESP_INTR_FLAG_IRAM");
        err = uart_driver_install(SENDER_UART_NUM, SENDER_UART_RX_BUF_SIZE, SENDER_TX_BUF_SIZE, 0, NULL, ESP_INTR_FLAG_IRAM);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "uart_driver_install(IRAM) failed: %s", esp_err_to_name(err));
            return err;
        }
#else
        return err;
#endif
    }

    // Mark driver installed for runtime checks
    uart_driver_installed_flag = true;

    // Small delay to allow driver/interrupts to settle
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG, "UART initialized UART%d TX=%d RX=%d @%d", SENDER_UART_NUM, hw.uart0_tx_gpio, hw.uart0_rx_gpio, SENDER_UART_BAUD);

    return ESP_OK;
}

// -------------------- Public send API (pool-backed) -------------------- //
void uart_sender_sendf(const char *fmt, ...)
{
    if (fmt == NULL)
        return;

    // If queue not created yet, drop message
    if (uart_tx_queue == NULL)
    {
        uart_sender_dropped_msgs++;
        return;
    }

    // Allocate buffer from pool (non-blocking)
    char *buf = uart_pool_alloc();
    if (!buf)
    {
        uart_sender_pool_exhaust++;
        uart_sender_dropped_msgs++;
        return;
    }

    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, UART_POOL_BUF_SIZE - 3, fmt, ap);
    va_end(ap);

    if (n < 0)
    {
        // return buffer to pool
        uart_pool_free(buf);
        return;
    }

    if (n > UART_POOL_BUF_SIZE - 3)
    {
        n = UART_POOL_BUF_SIZE - 3;
        buf[n] = '\0';
    }

    // Append CRLF
    buf[n++] = '\r';
    buf[n++] = '\n';
    buf[n] = '\0';

    // Enqueue pointer; non-blocking
    BaseType_t ok = xQueueSend(uart_tx_queue, &buf, 0);
    if (ok != pdTRUE)
    {
        // queue full: return buffer to pool and count drop
        uart_pool_free(buf);
        uart_sender_dropped_msgs++;
    }
}

// -------------------- TX worker (diagnostic, defensive) -------------------- //
static void uart_tx_task(void *pvParameters)
{
    (void)pvParameters;

    if (uart_tx_queue == NULL)
    {
        ESP_LOGE(TAG, "uart_tx_task: uart_tx_queue is NULL; exiting");
        vTaskDelete(NULL);
        return;
    }

    char *line = NULL;
    TickType_t last_report = xTaskGetTickCount();

    for (;;)
    {
        // Periodic diagnostics every 5s
        if ((xTaskGetTickCount() - last_report) > pdMS_TO_TICKS(5000))
        {
            UBaseType_t high = uxTaskGetStackHighWaterMark(NULL);
            ESP_LOGD(TAG, "TX task HWM=%u free_heap=%u pool_free=%d dropped=%u pool_exhaust=%u",
                     (unsigned)high,
                     (unsigned)esp_get_free_heap_size(),
                     uart_pool_free_top,
                     (unsigned)uart_sender_dropped_msgs,
                     (unsigned)uart_sender_pool_exhaust);
            last_report = xTaskGetTickCount();
        }

        if (xQueueReceive(uart_tx_queue, &line, portMAX_DELAY) == pdTRUE)
        {
            if (!line)
            {
                ESP_LOGW(TAG, "uart_tx_task: received NULL pointer");
                continue;
            }

            size_t len = strlen(line);
            int written = uart_write_bytes(SENDER_UART_NUM, line, len);
            if (written < 0)
            {
                ESP_LOGW(TAG, "uart_tx_task: uart_write_bytes error %d", written);
            }
            else if ((size_t)written != len)
            {
                ESP_LOGW(TAG, "uart_tx_task: partial write %d/%u", written, (unsigned)len);
            }

            // Wait for TX to finish at driver level (timeout 2s)
            esp_err_t wait_err = uart_wait_tx_done(SENDER_UART_NUM, pdMS_TO_TICKS(2000));
            if (wait_err != ESP_OK)
            {
                ESP_LOGW(TAG, "uart_tx_task: uart_wait_tx_done %s", esp_err_to_name(wait_err));
            }

            // Return buffer to pool
            uart_pool_free(line);
            line = NULL;

            // Yield briefly so interrupts and watchdog can run
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }

    vTaskDelete(NULL);
}

// -------------------- User-visible sender task --------------------
void task_uart_sender_code(void *pvParameters)
{
    (void)pvParameters;

    // Give system and TX worker time to initialize
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Keep task alive but idle - logs are redirected by main.c
    for (;;)
    {
        vTaskDelay(pdMS_TO_TICKS(10000)); // Check every 10 seconds
    }
}

// -------------------- Start function --------------------
void start_task_uart_sender(void)
{
    // Initialize pool first
    uart_pool_init();

    // Initialize hardware first
    if (uart_sender_hw_init() != ESP_OK)
    {
        ESP_LOGE(TAG, "UART hardware init failed; not starting sender tasks");
        return;
    }

    // Create queue for pointers to pool buffers
    if (uart_tx_queue == NULL)
    {
        uart_tx_queue = xQueueCreate(UART_SENDER_QUEUE_LEN, sizeof(char *));
        if (uart_tx_queue == NULL)
        {
            ESP_LOGE(TAG, "Failed to create uart_tx_queue");
            uart_driver_delete(SENDER_UART_NUM);
            return;
        }
    }

    // Create TX worker task (pinned to core 0, low priority)
    if (task_uart_tx_sender == NULL)
    {
        BaseType_t r = xTaskCreatePinnedToCore(uart_tx_task, "uart_tx_task", 8192, NULL, tskIDLE_PRIORITY + 1, &task_uart_tx_sender, 1);
        if (r != pdPASS)
        {
            ESP_LOGE(TAG, "Failed to create uart_tx_task");
            task_uart_tx_sender = NULL;
            if (uart_tx_queue)
            {
                vQueueDelete(uart_tx_queue);
                uart_tx_queue = NULL;
            }
            uart_driver_delete(SENDER_UART_NUM);
            return;
        }
    }

    // Create the user-visible sender task as a static task (one-time)
    if (task_uart_sender == NULL)
    {
        task_uart_sender = xTaskCreateStatic(task_uart_sender_code, "uart_sender", UART_SENDER_STACK_WORDS, NULL, tskIDLE_PRIORITY + 1, uart_sender_stack, &uart_sender_tcb);

        if (task_uart_sender == NULL)
        {
            ESP_LOGE(TAG, "UART sender task creation failed");
            if (task_uart_tx_sender)
            {
                vTaskDelete(task_uart_tx_sender);
                task_uart_tx_sender = NULL;
            }
            if (uart_tx_queue)
            {
                vQueueDelete(uart_tx_queue);
                uart_tx_queue = NULL;
            }
            uart_driver_delete(SENDER_UART_NUM);
            return;
        }
    }

    ESP_LOGI(TAG, "UART sender started (tx_handle=%p sender_handle=%p pool_count=%d buf_size=%d)", (void *)task_uart_tx_sender, (void *)task_uart_sender, UART_POOL_COUNT, UART_POOL_BUF_SIZE);
}

void stop_task_uart_sender(void)
{
    ESP_LOGI(TAG, "Stopping UART sender tasks");

    if (task_uart_sender != NULL)
    {
        vTaskDelete(task_uart_sender);
        task_uart_sender = NULL;
    }

    if (task_uart_tx_sender != NULL)
    {
        vTaskDelete(task_uart_tx_sender);
        task_uart_tx_sender = NULL;
    }

    if (uart_tx_queue != NULL)
    {
        vQueueDelete(uart_tx_queue);
        uart_tx_queue = NULL;
    }

    if (uart_driver_installed_flag)
    {
        uart_driver_delete(SENDER_UART_NUM);
        uart_driver_installed_flag = false;
    }

    ESP_LOGI(TAG, "UART sender stopped");
}
