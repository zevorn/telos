#include <errno.h>
#include <unistd.h>

#include <telos/linux.h>

const char *telos_linux_target(void)
{
    return TELOS_LINUX_TARGET;
}

int telos_linux_platform_query(struct telos_linux_platform *platform)
{
    long page_size;
    long processor_count;

    if (platform == NULL) {
        return -EINVAL;
    }

    errno = 0;
    page_size = sysconf(_SC_PAGE_SIZE);
    if (page_size <= 0) {
        return errno == 0 ? -EIO : -errno;
    }
    errno = 0;
    processor_count = sysconf(_SC_NPROCESSORS_ONLN);
    if (processor_count <= 0) {
        return errno == 0 ? -EIO : -errno;
    }

    *platform = (struct telos_linux_platform){
        .architecture = TELOS_LINUX_ARCHITECTURE,
        .target = TELOS_LINUX_TARGET,
        .page_size = (telos_size)page_size,
        .online_processor_count = (telos_size)processor_count,
    };
    return 0;
}
