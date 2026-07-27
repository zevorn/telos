#include <dlfcn.h>
#include <stdio.h>

int main(int argc, char **argv)
{
    void *module;
    void *entry;

    if (argc != 2) {
        fputs("usage: telos-abi-check PLUGIN.so\n", stderr);
        return 2;
    }
    module = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (module == NULL) {
        fprintf(stderr, "telos-abi-check: %s\n", dlerror());
        return 1;
    }
    entry = dlsym(module, "telos_plugin_init_v1");
    if (entry == NULL) {
        fputs("telos-abi-check: missing telos_plugin_init_v1\n", stderr);
        dlclose(module);
        return 1;
    }
    dlclose(module);
    return 0;
}
