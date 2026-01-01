#include "task_statemachine.h"

// static const char *TAG = "Statemachine";

// Functions for the task statemachine would go here
void start_task_statemachine(void)
{
    // Create statemachine task
    xTaskCreate(task_statemachine_code, "statemach", 15000, NULL, 6, &task_statemachine);
}

void task_statemachine_code(void *pvParameters)
{

    // size_t snap_count = (size_t)MAX_I2C_BUSES * (size_t)CHANNELS_PER_BUS * (size_t)DATA_FIELDS;
    // size_t snapshot_bytes = sizeof(double) * snap_count;
    // sensor_avg_queue = xQueueCreate(1, snapshot_bytes);

    // sensor_avg_queue = xQueueCreate(1, sizeof(double[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS]));

    // Create queue for average sensor data
    // sensor_avg_queue = xQueueCreate(1, sizeof(float) * MAX_I2C_BUSES * CHANNELS_PER_BUS * DATA_FIELDS);

    init_statemachine();

    for (;;)
    {
        // Run the state machine
        run_statemachine();
        vTaskDelay(60000);
    }
}