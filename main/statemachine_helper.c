#include "statemachine_helper.h"

static float sensor_fifo[2][8][3][MAX_FIFO_SIZE];
static float sum[48] = {0.0};
static float avg[48] = {0.0};
static int fifoSize[48] = {0}; // Current number of elements in the FIFO.
static int fifoHead[48] = {0}; // Index of the oldest element in the FIFO.

void sensor_data_average(void)
{
    static const char *TAG = "Sensor avg";

    int pos;
    float sensor_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};
    float sensor_avg_data[MAX_I2C_BUSES][CHANNELS_PER_BUS][DATA_FIELDS] = {0};

    if (xQueuePeek(sensor_queue, &sensor_data, 0) == pdTRUE)
    {
        pos = 0;
        for (int bus = 0; bus < 2; bus++)
        {
            for (int slot = 0; slot < 8; slot++)
            {
                for (int measurement = 0; measurement < 3; measurement++)
                {
                    fifoPush(bus, slot, measurement, sensor_data[bus][slot][measurement], pos);
                    sensor_avg_data[bus][slot][measurement] = avg[pos];
                    pos++; // counter for fifoSize and fifoHead
                }
            }
        }
        if (sensor_avg_queue != NULL)
        {
            xQueueOverwrite(sensor_avg_queue, &sensor_avg_data);
        }
        else
        {
            ESP_LOGE(TAG, "Average sensor data queue handle is NULL.");
        }
    }
}

void fifoPush(int bus, int slot, int measurement, float value, int pos)
{
    if (fifoSize[pos] < MAX_FIFO_SIZE)
    {
        // Add the value to the next available position.
        sensor_fifo[bus][slot][measurement][(fifoHead[pos] + fifoSize[pos]) % MAX_FIFO_SIZE] = value;
        sum[pos] += value;
        fifoSize[pos] = fifoSize[pos] + 1;
    }
    else
    {
        // Overwrite the oldest value and move the head forward.
        sum[pos] -= sensor_fifo[bus][slot][measurement][fifoHead[pos]];
        sensor_fifo[bus][slot][measurement][fifoHead[pos]] = value;
        sum[pos] += value;
        fifoHead[pos] = (fifoHead[pos] + 1) % MAX_FIFO_SIZE;
    }
    avg[pos] = sum[pos] / fifoSize[pos];
}

/*int fanspeed_char_to_int(char fanspeed_char)
{
    switch (fanspeed_char)
    {
    case 'Low': // Low
    case 'low':
        return 1;
    case 'Medium': // Medium
    case 'medium':
        return 2;
    case 'High': // High
    case 'high':
        return 3;
    default: // Error or unknown
        return 0;
    }
}*/

// The configuration from each statemachine state is stored in struct statemachine_config[MAX_NR_STATES];
// The name of the state is stored in the char statemachine_config[i].state_name
// The index i should then be used to indicate the actual state of the statemachine so that a direct link can be made with state_number and state_name
