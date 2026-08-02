# Provider API-key Authentication Plugin

`dev.zevorn.api-key-auth` supplies the login lifecycle for providers that use
an API key rather than an OAuth device flow. It exposes DeepSeek, Z.AI, and
Anthropic authentication definitions while keeping their keys in separate
0600 files below the Telos state directory.

The terminal `/login` command reads the provider's environment variable and
persists it without printing the secret:

| Provider | Environment variable |
| --- | --- |
| DeepSeek | `DEEPSEEK_API_KEY` |
| Z.AI | `ZAI_API_KEY` |
| Anthropic | `ANTHROPIC_API_KEY` |

Set the variable before starting Telos, then use `/login`. `/logout` removes
the provider file and wipes the in-memory copy. A missing environment variable
does not overwrite an existing login.
