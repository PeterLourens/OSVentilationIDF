/* bsec_adapter.c */
#include <string.h>
#include "bsec_adapter.h"

/* Include Bosch vendor header(s) only in this file */
#include "bsec_integration.h" /* adjust path as needed */

/* The vendor header defines the real symbols (e.g.):
   uint8_t bsec_mem_block[NUM_OF_SENS][BSEC_INSTANCE_SIZE];
   uint8_t *bsecInstance[NUM_OF_SENS];
   If the vendor header already defines them, DO NOT redefine them here.
   If it does not, define them here using the vendor macros. */

/* If vendor header defines the arrays, we can reference them directly.
   If the vendor header does NOT define them, uncomment and define them here:
*/
/* Example (only if vendor header does not define them):
uint8_t bsec_mem_block[NUM_OF_SENS][BSEC_INSTANCE_SIZE];
uint8_t *bsecInstance[NUM_OF_SENS];
*/

int bsec_adapter_init(void)
{
   /* Implement vendor init sequence here, using the exact prototypes
      from bsec_integration.h (bme68x_init, bsec_iot_init, etc.). */

   /* Example skeleton (adapt to your vendor API): */
   return_values_init ret; /* use or remove to avoid unused warning */
   /* ... initialize bme68x device, call bsec_iot_init, subscribe, etc. ... */

   /* If you don't use 'ret' for now, remove the variable or use it in logging. */
   (void)ret;
   return 0;
}

void *bsec_adapter_get_mem_block(void)
{
   /* Return pointer to the first byte of vendor memory block.
      If vendor defines bsec_mem_block as 2D array, return pointer to it. */
#ifdef bsec_mem_block
   return (void *)bsec_mem_block;
#else
   return NULL;
#endif
}

size_t bsec_adapter_get_mem_block_size(void)
{
#ifdef BSEC_INSTANCE_SIZE
#ifdef NUM_OF_SENS
   return (size_t)(NUM_OF_SENS * BSEC_INSTANCE_SIZE);
#else
   return (size_t)BSEC_INSTANCE_SIZE;
#endif
#else
   return 0;
#endif
}

void *bsec_adapter_get_instance(size_t sens_no)
{
#ifdef bsecInstance
   if (sens_no >= (size_t)NUM_OF_SENS)
      return NULL;
   return (void *)bsecInstance[sens_no];
#else
   return NULL;
#endif
}
