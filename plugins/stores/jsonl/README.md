# JSONL Event Store Plugin

`dev.zevorn.jsonl-store` persists the Telos event envelope as one JSON object
per line. It keeps a bounded in-memory index for ordered reads and reopens the
same file on the next process, which makes it suitable for session recovery
and debugging without coupling file policy to Core.

Configure the Plugin with an object containing a writable `path`:

```json
{"path":"/var/lib/telos/events.jsonl"}
```

Records must have strictly increasing event sequences. Files are limited to
16 MiB and each record to 1 MiB so a damaged or untrusted file cannot grow
without bound. A partial or malformed record is rejected during open.
