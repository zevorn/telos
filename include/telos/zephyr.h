#ifndef TELOS_ZEPHYR_H
#define TELOS_ZEPHYR_H

#include <telos/types.h>

int telos_zephyr_initialize(void);

const char *telos_zephyr_status(void);

bool telos_zephyr_run_static_scenario(void);

const char *telos_zephyr_trace(void);

bool telos_zephyr_network_wait(telos_u32 timeout_milliseconds,
                               char *address,
                               telos_size address_size);

#endif
