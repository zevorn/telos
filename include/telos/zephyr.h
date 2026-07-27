#ifndef TELOS_ZEPHYR_H
#define TELOS_ZEPHYR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

int telos_zephyr_initialize(void);

const char *telos_zephyr_status(void);

bool telos_zephyr_run_static_scenario(void);

const char *telos_zephyr_trace(void);

bool telos_zephyr_network_wait(
    uint32_t timeout_milliseconds,
    char *address,
    size_t address_size
);

#ifdef __cplusplus
}
#endif

#endif
