# Memory Event Store

`dev.zevorn.memory-store` keeps an append-only Event sequence in process
memory. It is the default lightweight Store for tests and short-lived
Sessions, and requires no capabilities.

The registry factory accepts no configuration or an empty object. Built-in
callers may use `telos_memory_store_create()` from
`<telos/plugins/memory_store.h>`.
