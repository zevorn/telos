#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static void usage(FILE *stream)
{
    fputs(
        "usage: telos-build --source DIR --build-dir DIR --staging DIR\n"
        "                   [--pkgconfig DIR] [--sysroot DIR]\n",
        stream
    );
}

static int run(const char *const *arguments, const char *destination)
{
    pid_t child = fork();
    int status;

    if (child < 0) {
        return 125;
    }
    if (child == 0) {
        if (destination != NULL) {
            setenv("DESTDIR", destination, 1);
        }
        execvp(arguments[0], (char *const *)arguments);
        _exit(127);
    }
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status)) {
        return 125;
    }
    return WEXITSTATUS(status);
}

int main(int argc, char **argv)
{
    const char *source = NULL;
    const char *build = NULL;
    const char *staging = NULL;
    const char *pkgconfig = NULL;
    const char *sysroot = NULL;

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        usage(stdout);
        return 0;
    }
    for (int index = 1; index < argc; index += 2) {
        if (index + 1 >= argc) {
            usage(stderr);
            return 2;
        }
        if (strcmp(argv[index], "--source") == 0) {
            source = argv[index + 1];
        } else if (strcmp(argv[index], "--build-dir") == 0) {
            build = argv[index + 1];
        } else if (strcmp(argv[index], "--staging") == 0) {
            staging = argv[index + 1];
        } else if (strcmp(argv[index], "--pkgconfig") == 0) {
            pkgconfig = argv[index + 1];
        } else if (strcmp(argv[index], "--sysroot") == 0) {
            sysroot = argv[index + 1];
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (source == NULL || build == NULL || staging == NULL) {
        usage(stderr);
        return 2;
    }
    if (pkgconfig != NULL) {
        setenv("PKG_CONFIG_PATH", pkgconfig, 1);
    }
    if (sysroot != NULL) {
        setenv("PKG_CONFIG_SYSROOT_DIR", sysroot, 1);
    }
    {
        const char *setup[] = {
            "meson",
            "setup",
            build,
            source,
            "--prefix=/",
            "--libdir=lib",
            "--buildtype=release",
            "--wrap-mode=nodownload",
            NULL,
        };
        const char *compile[] = {"meson", "compile", "-C", build, NULL};
        const char *test[] = {
            "meson",
            "test",
            "-C",
            build,
            "--print-errorlogs",
            NULL,
        };
        const char *install[] = {"meson", "install", "-C", build, NULL};
        int result;

        result = run(setup, NULL);
        if (result == 0) {
            result = run(compile, NULL);
        }
        if (result == 0) {
            result = run(test, NULL);
        }
        if (result == 0) {
            result = run(install, staging);
        }
        return result;
    }
}
