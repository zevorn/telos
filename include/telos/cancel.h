#ifndef TELOS_CANCEL_H
#define TELOS_CANCEL_H

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct telos_cancel;

struct telos_cancel *telos_cancel_create(void);

struct telos_cancel *telos_cancel_retain(const struct telos_cancel *cancel);

void telos_cancel_release(const struct telos_cancel *cancel);

bool telos_cancel_request(struct telos_cancel *cancel);

bool telos_cancel_requested(const struct telos_cancel *cancel);

#ifdef __cplusplus
}
#endif

#endif
