#ifndef TELOS_LINUX_H
#define TELOS_LINUX_H

#include <telos/types.h>

enum telos_linux_architecture {
    TELOS_LINUX_ARCHITECTURE_UNKNOWN,
    TELOS_LINUX_ARCHITECTURE_X86_64,
    TELOS_LINUX_ARCHITECTURE_AARCH64,
    TELOS_LINUX_ARCHITECTURE_RISCV64,
};

struct telos_linux_platform {
    enum telos_linux_architecture architecture;
    const char *target;
    telos_size page_size;
    telos_size online_processor_count;
};

const char *telos_linux_target(void);

int telos_linux_platform_query(struct telos_linux_platform *platform);

#endif
