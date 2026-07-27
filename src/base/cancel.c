#include <stdatomic.h>
#include <stdlib.h>

#include <telos/cancel.h>

struct telos_cancel {
    atomic_uint references;
    atomic_bool requested;
};

struct telos_cancel *telos_cancel_create(void)
{
    struct telos_cancel *cancel = malloc(sizeof(*cancel));

    if (cancel == NULL) {
        return NULL;
    }

    atomic_init(&cancel->references, 1);
    atomic_init(&cancel->requested, false);
    return cancel;
}

struct telos_cancel *telos_cancel_retain(const struct telos_cancel *cancel)
{
    struct telos_cancel *mutable_cancel = (struct telos_cancel *)cancel;

    if (mutable_cancel != NULL) {
        atomic_fetch_add_explicit(
            &mutable_cancel->references,
            1,
            memory_order_relaxed
        );
    }

    return mutable_cancel;
}

void telos_cancel_release(const struct telos_cancel *cancel)
{
    struct telos_cancel *mutable_cancel = (struct telos_cancel *)cancel;

    if (mutable_cancel == NULL) {
        return;
    }

    if (
        atomic_fetch_sub_explicit(
            &mutable_cancel->references,
            1,
            memory_order_acq_rel
        ) == 1
    ) {
        free(mutable_cancel);
    }
}

bool telos_cancel_request(struct telos_cancel *cancel)
{
    bool expected = false;

    if (cancel == NULL) {
        return false;
    }

    return atomic_compare_exchange_strong_explicit(
        &cancel->requested,
        &expected,
        true,
        memory_order_release,
        memory_order_relaxed
    );
}

bool telos_cancel_requested(const struct telos_cancel *cancel)
{
    if (cancel == NULL) {
        return false;
    }

    return atomic_load_explicit(&cancel->requested, memory_order_acquire);
}
