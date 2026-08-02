#include <assert.h>
#include <errno.h>
#include <string.h>

#include <telos/command.h>

struct command_fixture {
    size_t calls;
    char arguments[64];
};

static bool emit_notice(telos_frontend_emit_fn emit, void *context,
                        const char *text, struct telos_error **error)
{
    const struct telos_frontend_event event = {
        .kind = TELOS_FRONTEND_NOTICE,
        .text = text,
    };

    return emit(&event, context, error);
}

static bool run_model(const char *arguments, const struct telos_cancel *cancel,
                      telos_frontend_emit_fn emit, void *emit_context,
                      void *context, struct telos_error **error)
{
    struct command_fixture *fixture = context;

    (void)cancel;
    fixture->calls += 1;
    assert(arguments != NULL);
    assert(strlen(arguments) < sizeof(fixture->arguments));
    strcpy(fixture->arguments, arguments);
    return emit_notice(emit, emit_context, "model selected", error);
}

static bool run_quit(const char *arguments, const struct telos_cancel *cancel,
                     telos_frontend_emit_fn emit, void *emit_context,
                     void *context, struct telos_error **error)
{
    bool *called = context;

    (void)arguments;
    (void)cancel;
    (void)emit;
    (void)emit_context;
    (void)error;
    *called = true;
    return true;
}

static bool collect_event(const struct telos_frontend_event *event,
                          void *context, struct telos_error **error)
{
    const char **text = context;

    (void)error;
    *text = event->text;
    return true;
}

int main(void)
{
    struct telos_command_registry registry;
    struct telos_command command = {
        .name = "model",
        .help = "select a model",
        .run = run_model,
    };
    struct command_fixture fixture = {0};
    bool quit_called = false;
    const struct telos_command quit = {
        .name = "quit",
        .help = "quit",
        .run = run_quit,
        .context = &quit_called,
    };
    struct telos_error *error = NULL;
    const char *notice = NULL;
    bool handled;
    bool exit_requested;

    telos_command_registry_initialize(&registry);
    command.context = &fixture;
    assert(telos_command_registry_add(&registry, &command, &error));
    assert(error == NULL);
    assert(telos_command_registry_add(&registry, &quit, &error));
    assert(error == NULL);

    handled = false;
    exit_requested = false;
    assert(telos_command_registry_dispatch(
        &registry, "/model  openai/gpt-5", NULL, collect_event, &notice,
        &handled, &exit_requested, &error));
    assert(error == NULL);
    assert(handled);
    assert(!exit_requested);
    assert(fixture.calls == 1);
    assert(strcmp(fixture.arguments, "openai/gpt-5") == 0);
    assert(strcmp(notice, "model selected") == 0);

    handled = true;
    exit_requested = true;
    assert(telos_command_registry_dispatch(
        &registry, "hello", NULL, collect_event, &notice, &handled,
        &exit_requested, &error));
    assert(error == NULL);
    assert(!handled);
    assert(!exit_requested);
    assert(fixture.calls == 1);

    handled = false;
    exit_requested = false;
    assert(telos_command_registry_dispatch(
        &registry, "/quit", NULL, collect_event, &notice, &handled,
        &exit_requested, &error));
    assert(error == NULL);
    assert(handled);
    assert(exit_requested);
    assert(quit_called);

    handled = false;
    exit_requested = false;
    assert(!telos_command_registry_dispatch(
        &registry, "/missing", NULL, collect_event, &notice, &handled,
        &exit_requested, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
    assert(telos_error_code(error) == ENOENT);
    telos_error_release(error);
    return 0;
}
