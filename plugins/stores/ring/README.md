# Ring Event Store

`dev.zevorn.ring-store` retains a fixed number of Events and evicts the oldest
Event when full. It is suitable for bounded-memory targets.

The registry factory requires `{"capacity": N}` with a positive integer
capacity. Built-in callers may use `telos_ring_store_create()` from
`<telos/plugins/ring_store.h>`.

Firmware can avoid Store allocation entirely by declaring a
`union telos_ring_store_storage` and a fixed array of Event pointer slots,
then passing both to `telos_ring_store_initialize()`. The generic
`telos_event_store_destroy()` releases retained Events but does not free the
caller-owned storage.
