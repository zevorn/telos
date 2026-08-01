#ifndef TELOS_CONTAINER_OF_H
#define TELOS_CONTAINER_OF_H

#include <telos/types.h>

#define TELOS_CONTAINER_OF(pointer, type, member)                              \
    ((type *)((char *)(pointer) - offsetof(type, member)))

#define TELOS_CONTAINER_OF_CONST(pointer, type, member)                        \
    ((const type *)((const char *)(pointer) - offsetof(type, member)))

#endif
