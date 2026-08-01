# Telos Linux platform

This directory is the Linux host boundary for Telos. Meson builds it only
when the host system is Linux and exposes it as `telos_linux_platform_dep`.
The regular `telos_dep` includes that dependency on Linux.

The first platform API reports the canonical Plugin target, page size, and
online processor count through caller-provided storage. It does not allocate
memory. These values let a host choose explicit resource budgets without
putting Linux discovery logic into Core or into individual Plugins.

```c
#include <telos/linux.h>

struct telos_linux_platform platform;

if (telos_linux_platform_query(&platform) < 0) {
    /* Handle an unavailable host capability. */
}
```

The query returns zero on success or a negative `errno` value on failure.

Linux-specific thread, process, dynamic-loader, and I/O backends should move
behind this platform boundary as their Core interfaces are separated. Core
state-machine, Reactor, and Plugin lifecycle contracts remain platform
neutral; this directory must not contain providers, stores, or other
replaceable features.
