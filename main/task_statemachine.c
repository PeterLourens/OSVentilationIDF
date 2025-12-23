#include "task_statemachine.h"

//static const char *TAG = "Statemachine";

// Functions for the task statemachine would go here
void start_task_statemachine(void)
{
    // Create LCD task
    xTaskCreate(task_statemachine_code, "statemach", 4096, NULL, 6, &task_statemachine);
}

void task_statemachine_code(void *pvParameters)
{
    // init_statemachine();

    for (;;)
    {
        // Update average sensor data queue
        sensor_data_average();
        run_statemachine();

        vTaskDelay(60000);
        // if (ap_active == 0) {
        //
        //}
    }
}