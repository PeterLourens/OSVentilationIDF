#ifndef FANCONTROL_H
#define FANCONTROL_H

#include "globals.h"
#include "general.h"

#include "driver/i2c_master.h"

/* Control fans via configured mechanism. Pass fanspeed as an integer:
 * 0 = error, 1 = low, 2 = medium, 3 = high
 */
void set_fanspeed(int fanspeed);

#endif /* FANCONTROL_H */