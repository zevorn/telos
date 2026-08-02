# Sign in to OpenAI from the terminal

The terminal Agent can authenticate with an OpenAI ChatGPT account through the
OpenAI Codex Authentication Plugin. The runtime Core only defines the generic
authentication interface; the device-code protocol, token refresh, and local
credential cache stay inside the host-only Plugin.

## Build and start

Build Telos with the curl Transport Plugin:

```sh
meson setup build -Dcurl_transport=enabled
meson compile -C build
build/tools/telos
```

The TUI can start before a model is configured, so authentication commands are
available immediately. Type either `login` or `/login`:

```text
You   › login
Telos › Open https://auth.openai.com/codex/device and enter code XXXX-XXXX
```

Open the displayed URL in a browser, sign in to OpenAI, and enter the code.
Telos waits for authorization and reports when login completes. OpenAI may
require device-code authorization to be enabled for the account or workspace;
see the [OpenAI authentication documentation][openai-auth].

Use these commands to inspect or remove the session:

```text
/login status
/logout
```

Set a Responses-compatible model before sending the first prompt:

```sh
export TELOS_AGENT_MODEL='your-responses-model'
build/tools/telos
```

After OAuth login, the Provider sends requests to OpenAI's ChatGPT Codex
Responses service. OAuth credentials are never added to prompts, Events, or
normal Plugin RPC payloads. The Secret Broker supplies a temporary access token
only at the Provider boundary, and the curl Transport accepts it only as the
request bearer credential.

## Credential storage

The Authentication Plugin stores the refreshable session in:

```text
~/.telos/openai-codex-auth.json
```

The file is created atomically with mode `0600`, is accepted only when owned by
the current user, and is deleted by `/logout`. It contains bearer credentials,
so do not copy, publish, or commit it. The Plugin clears in-memory token buffers
when they are replaced or destroyed.

API-key authentication remains available as an alternative. Set
`OPENAI_API_KEY` or `TELOS_AGENT_API_KEY`; do not put either value in
`telos.toml`, command arguments, prompts, or logs.

The OAuth Plugin is built for Linux and macOS. Zephyr builds retain the generic
authentication seam but do not link the POSIX filesystem, device login, or curl
implementation.

[openai-auth]: https://learn.chatgpt.com/docs/auth
