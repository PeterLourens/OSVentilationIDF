#ifndef STATEMACHINE_HELPER_H
#define STATEMACHINE_HELPER_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "globals.h"
#include "general.h"

    /* Sensor averaging helpers */
    void sensor_data_average(void);

    /* Push sensor value into FIFO for moving average calculation */
    void fifoPush(int bus, int slot, int measurement, float value, int pos);

    /* Return index of a state by case-insensitive name, or -1 if not found */
    int find_state_index_by_name_ci(const char *name);

    /* Time and mode helpers */
    bool is_day(void);
    bool is_weekend(void);
    bool cooking_times(void);

    /* Cycling valve helpers: return true when valve should be active */
    bool valve_cycle_times_day(void);
    bool valve_cycle_times_night(void);

    /* Select sensors which have high readings */
    void select_sensors(void);

#ifdef __cplusplus
}
#endif

#endif /* STATEMACHINE_HELPER_H */