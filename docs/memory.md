# Memory and resource model

Telos targets both Linux hosts and constrained Zephyr systems. The runtime is
therefore designed around bounded work, explicit ownership, and caller-owned
storage. Heap allocation is permitted where an object's lifetime is genuinely
dynamic, but it is not the default implementation technique for new Core
mechanisms.

## Design rules

Core and Plugin code should follow these rules in order:

1. Prefer compile-time storage for fixed platform limits.
2. Prefer caller-owned buffers and intrusive containers for bounded runtime
   state.
3. If runtime sizing is required, allocate once during initialization rather
   than in the Reactor's steady-state path.
4. Put unbounded or host-specific behavior in a Plugin and keep it out of
   minimal Zephyr profiles.
5. Treat every size calculation as fallible. Check addition, multiplication,
   alignment, and conversion before reading, writing, or allocating.
6. Document ownership at every API boundary. Every retained object and heap
   allocation must have one visible release path, including partial-failure
   and cancellation paths.
7. Never infer string bounds. Carry a byte length, reserve space for the final
   NUL when a C string is required, and leave the destination unchanged when a
   bounded append fails.

Dynamic allocation must not be added to interrupt handlers, fixed-latency
callbacks, intrusive container operations, or the steady-state Reactor loop.
Allocation in constructors must return an explicit error and be covered by
failure-injection tests.

## Portable types

Every public header obtains C scalar types through `<telos/types.h>`. The
header enforces the C17 and 8-bit-byte baseline and provides Telos names for
fixed-width integers, object sizes, signed byte counts, pointer-sized values,
stable 64-bit offsets, maximum alignment, and raw bytes.

`telos_ssize` and `telos_offset` are the portable API types used where POSIX
would expose `ssize_t` or `off_t`. Public interfaces must not expose
`pthread_t`, `pid_t`, `off_t`, native file descriptors, or Zephyr kernel
objects. Platform implementations translate those native types at the Linux
or Zephyr boundary instead. The types header deliberately does not include
`pthread.h`, `unistd.h`, allocation functions, or string functions; pulling
those facilities into every translation unit would obscure platform
dependencies and increase firmware coupling.

## Zero-allocation utility headers

The public utility layer consists only of macros and `static inline`
functions. It has no library state and performs no allocation:

| Header | Purpose |
| --- | --- |
| `<telos/array.h>` | Compile-time array element counts. |
| `<telos/align.h>` | Checked power-of-two alignment. |
| `<telos/bitops.h>` | Fixed-width bitmap declaration and bounded bit operations. |
| `<telos/bitfield.h>` | Checked extraction, preparation, and replacement of contiguous fields. |
| `<telos/checked_math.h>` | Overflow-checked `size_t` addition and multiplication. |
| `<telos/container_of.h>` | Intrusive object recovery from an embedded member. |
| `<telos/list.h>` | Circular intrusive doubly linked lists. |
| `<telos/static_buffer.h>` | Failure-atomic string assembly in caller-owned storage. |

The APIs use Telos-prefixed names to avoid collisions with Zephyr, libc,
Linux headers, and Plugin dependencies. They use portable C17 rather than GNU
statement expressions or `typeof`, so the same headers compile in host and
firmware builds.

## Profile guidance

The Ring Store is the preferred Event Store shape for constrained systems
because its retained Event count is bounded. The unbounded Memory Store and
filesystem-backed Markdown Store are host-oriented Plugins and should not be
selected by a minimal Zephyr image.

Some v0.1 objects still allocate during construction or retain immutable
reference-counted Values and Events. That is an explicit migration boundary,
not a claim that the current runtime is heap-free. New APIs should provide a
caller-owned initialization path before their heap-backed convenience
constructor is considered complete.

## Review checklist

Before merging code that handles buffers or ownership, verify that:

- capacities and byte counts cannot wrap;
- all input strings have a known bound or a validated terminating NUL;
- writes reserve space for a terminator when one is required;
- the output remains valid after every failure;
- each success and failure path releases exactly what it owns;
- Reactor work has an explicit upper bound;
- Linux-only facilities are behind a Plugin or platform adapter;
- allocation-failure and boundary-value tests cover the new path.
