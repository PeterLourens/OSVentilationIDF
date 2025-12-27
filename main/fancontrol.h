#ifndef FANCONTROL_H
#define FANCONTROL_H

#ifdef __cplusplus
extern "C"
{
#endif

#include "globals.h"
#include "general.h"

    /* Control fans via configured mechanism. Pass fanspeed as an integer:
     * 0 = error, 1 = low, 2 = medium, 3 = high
     */
    void set_fanspeed(int fanspeed);

#ifdef __cplusplus
}
#endif

#endif /* FANCONTROL_H */