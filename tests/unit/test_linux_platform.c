#include <assert.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <telos/linux.h>

enum query_mode {
    QUERY_SUCCESS,
    QUERY_PAGE_ERROR,
    QUERY_PAGE_ZERO,
    QUERY_PROCESSOR_ERROR,
    QUERY_PROCESSOR_ZERO,
};

static enum query_mode query_mode;

long __wrap_sysconf(int name)
{
    if (name == _SC_PAGE_SIZE) {
        if (query_mode == QUERY_PAGE_ERROR) {
            errno = ENOSYS;
            return -1;
        }
        if (query_mode == QUERY_PAGE_ZERO) {
            errno = 0;
            return 0;
        }
        return 4096;
    }
    assert(name == _SC_NPROCESSORS_ONLN);
    if (query_mode == QUERY_PROCESSOR_ERROR) {
        errno = EAGAIN;
        return -1;
    }
    if (query_mode == QUERY_PROCESSOR_ZERO) {
        errno = 0;
        return 0;
    }
    return 4;
}

int main(int argc, char **argv)
{
    struct telos_linux_platform platform;

    assert(argc == 2);
    assert(telos_linux_platform_query(NULL) == -EINVAL);
    query_mode = QUERY_PAGE_ERROR;
    assert(telos_linux_platform_query(&platform) == -ENOSYS);
    query_mode = QUERY_PAGE_ZERO;
    assert(telos_linux_platform_query(&platform) == -EIO);
    query_mode = QUERY_PROCESSOR_ERROR;
    assert(telos_linux_platform_query(&platform) == -EAGAIN);
    query_mode = QUERY_PROCESSOR_ZERO;
    assert(telos_linux_platform_query(&platform) == -EIO);
    query_mode = QUERY_SUCCESS;
    assert(telos_linux_platform_query(&platform) == 0);
    assert(platform.page_size == 4096);
    assert(platform.online_processor_count == 4);
    assert(platform.target == telos_linux_target());
    assert(strncmp(platform.target, "linux-", 6) == 0);
    assert(platform.architecture == TELOS_TEST_EXPECTED_ARCHITECTURE);
    assert(strcmp(platform.target, argv[1]) == 0);
    return 0;
}
