# OpenAI Codex Authentication Plugin

`dev.zevorn.openai-codex-auth` implements OpenAI device-code login behind
Telos' provider-neutral authentication interface. It is an official host-only
Plugin for Linux and macOS; Core and Zephyr builds do not contain the OAuth
protocol.

The Plugin:

- starts and polls the OpenAI Codex device authorization flow;
- exchanges and refreshes OAuth tokens;
- extracts the ChatGPT account identifier required by the Codex endpoint;
- resolves an access token only for `provider.openai`; and
- atomically maintains a current-user-only credential cache.

Only `https://auth.openai.com` is accepted as a production authentication
service. HTTP or alternate HTTPS endpoints are accepted solely when their host
is `localhost`, `127.0.0.1`, or `[::1]`, which keeps functional tests local.

The implementation uses bounded buffers for protocol responses, tokens, forms,
paths, and account identifiers. One authentication instance is allocated when
the Plugin is created. Secrets are erased before temporary storage or the
instance is released.

Applications normally use the terminal commands documented in
[`docs/openai-login.md`](../../../docs/openai-login.md). Embedders can include:

```c
#include <telos/plugins/openai_codex_auth.h>
```

The Plugin requires filesystem read/write and HTTPS network capabilities. Its
cache is `openai-codex-auth.json` below the caller-provided state directory and
must remain private to the current user.
