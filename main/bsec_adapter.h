#ifndef BSEC_ADAPTER_H
#define BSEC_ADAPTER_H

#include <stdint.h>
#include <stddef.h>

/* Initialize BSEC adapter (call after I2C and device selection are ready).
   Returns 0 on success, negative on error. */
int bsec_adapter_init(void);

/* Accessor: pointer to the BSEC memory block (opaque). */
void *bsec_adapter_get_mem_block(void);

/* Accessor: size of the BSEC memory block in bytes. */
size_t bsec_adapter_get_mem_block_size(void);

/* Accessor: get pointer to bsecInstance for sensor index (returns NULL if unavailable). */
void *bsec_adapter_get_instance(size_t sens_no);

#endif /* BSEC_ADAPTER_H */
