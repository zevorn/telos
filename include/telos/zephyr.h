#ifndef TELOS_ZEPHYR_H
#define TELOS_ZEPHYR_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

int telos_zephyr_initialize(void);

const char *telos_zephyr_status(void);

bool telos_zephyr_run_static_scenario(void);

const char *telos_zephyr_trace(void);

#ifdef __cplusplus
}
#endif

#endif
