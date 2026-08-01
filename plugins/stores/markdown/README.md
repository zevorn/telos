# Markdown Event Store

`dev.zevorn.markdown-store` persists append-only Event records as readable
Markdown with structured JSON payloads. It recovers complete records after a
restart and ignores a partial trailing record without damaging prior data.

The registry factory requires `{"path": "/absolute/events.md"}`. Built-in
callers may use `telos_markdown_store_create()` from
`<telos/plugins/markdown_store.h>`.
