/**
 * @file pushbutton.h
 * @brief Public pushbutton GPIO and event queue interface for the OSVentilation firmware.
 */
#ifndef PUSHBUTTON_H
#define PUSHBUTTON_H

#include <stdbool.h>
#include <driver/gpio.h>

#include "logging.h"

/**
 * @brief Initialize pushbutton module and attach the ISR.
 */
void pushbutton_init(void);

/**
 * @brief Deinitialize the pushbutton module and release resources.
 */
void pushbutton_deinit(void);

/**
 * @brief Return the last debounced pushbutton state.
 *
 * @return true if the button is currently pressed, false otherwise.
 */
bool pushbutton_get_state(void);

/**
 * @brief Return the event queue handle used by the pushbutton ISR.
 *
 * @return QueueHandle_t handle, or NULL if unavailable.
 */
QueueHandle_t pushbutton_get_queue(void);
#endif
