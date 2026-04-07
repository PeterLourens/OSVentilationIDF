// pushbutton.h
#ifndef PUSHBUTTON_H
#define PUSHBUTTON_H

#include <stdbool.h>
#include <driver/gpio.h>

#include "logging.h"

// Initialize pushbutton module (configures GPIO and ISR).
void pushbutton_init(void);

// Deinitialize pushbutton module (removes ISR and frees resources)
void pushbutton_deinit(void);

// Return last debounced state (true = pressed)
bool pushbutton_get_state(void);

// Return event queue handle (NULL if not available)
QueueHandle_t pushbutton_get_queue(void);
#endif
