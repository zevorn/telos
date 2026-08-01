# OpenAI Responses Provider

`dev.zevorn.openai-responses` adapts Telos' provider-neutral request and Event
interfaces to the OpenAI Responses protocol. HTTPS transport and secret
resolution are injected, so the Plugin never owns credentials.

Applications that use the built-in form include:

```c
#include <telos/plugins/openai_responses.h>
```

The default build links this official Plugin into the Telos distribution and,
when `shared_plugins` is enabled, also builds an in-process Plugin module.
Credentialed network verification remains opt-in; see
[`docs/testing.md`](../../../docs/testing.md).
