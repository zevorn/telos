#include <stdio.h>

#include <telos/cancel.h>

int main(void)
{
    struct telos_cancel *source = telos_cancel_create();
    struct telos_cancel *observer;

    if (source == NULL) {
        fputs("failed to create cancellation state\n", stderr);
        return 1;
    }

    observer = telos_cancel_retain(source);
    if (observer == NULL || telos_cancel_requested(observer) ||
        !telos_cancel_request(source) || !telos_cancel_requested(observer) ||
        telos_cancel_request(source)) {
        fputs("cancellation was not shared and idempotent\n", stderr);
        telos_cancel_release(observer);
        telos_cancel_release(source);
        return 1;
    }

    telos_cancel_release(source);
    if (!telos_cancel_requested(observer)) {
        fputs("observer lost cancellation after source release\n", stderr);
        telos_cancel_release(observer);
        return 1;
    }

    telos_cancel_release(observer);
    return 0;
}
