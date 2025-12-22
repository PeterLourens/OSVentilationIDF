#include "statemachine_helper.h"

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
