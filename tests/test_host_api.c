#include <assert.h>
#include <string.h>

#include <telos/plugin.h>

static void log_message(void *context, int level, const char *message)
{
    unsigned int *calls = context;

    (void)level;
    assert(strcmp(message, "ready") == 0);
    *calls += 1;
}

int main(void)
{
    unsigned int calls = 0;
    struct telos_host_api_v1 host;
    struct telos_value *value;
    char json[32];
    int64_t now;

    assert(telos_host_api_v1_initialize(
        &host,
        &calls,
        log_message,
        NULL
    ));
    assert(host.abi_version == TELOS_PLUGIN_ABI_VERSION);
    assert(host.struct_size == sizeof(host));
    assert(host.allocator != NULL);
    assert(host.value != NULL);
    assert(host.event != NULL);
    host.log(host.context, 1, "ready");
    assert(calls == 1);
    value = host.value->parse_json("{\"ok\":true}", 11, NULL);
    assert(value != NULL);
    assert(host.value->write_json(
        value,
        json,
        sizeof(json),
        NULL,
        NULL
    ));
    assert(strcmp(json, "{\"ok\":true}") == 0);
    host.value->release(value);
    assert(telos_clock_now_milliseconds(&host.clock, &now, NULL));
    assert(now > 0);
    return 0;
}
