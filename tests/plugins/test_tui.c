#define _XOPEN_SOURCE 700

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <telos/command.h>
#include <telos/plugins/tui_frontend.h>

struct turn_fixture {
    size_t turns;
};

static size_t session_completion_count(const char *input, void *context)
{
    (void)context;
    return strcmp(input, "/resume") == 0 ? 2 : 0;
}

static bool session_completion_at(
    const char *input, size_t ordinal,
    struct telos_frontend_completion_item *item, void *context)
{
    static const char *const values[] = {
        "/resume first-session",
        "/resume second-session",
    };
    static const char *const labels[] = {
        "first session",
        "second session",
    };
    static const char *const details[] = {
        "first saved conversation",
        "second saved conversation",
    };

    (void)context;
    if (strcmp(input, "/resume") != 0 || item == NULL || ordinal >= 2) {
        return false;
    }
    memcpy(item->value, values[ordinal], strlen(values[ordinal]) + 1);
    memcpy(item->label, labels[ordinal], strlen(labels[ordinal]) + 1);
    memcpy(item->detail, details[ordinal], strlen(details[ordinal]) + 1);
    return true;
}

static bool emit_event(telos_frontend_emit_fn emit, void *context,
                       enum telos_frontend_event_kind kind,
                       const char *text, const char *name,
                       struct telos_error **error)
{
    const struct telos_frontend_event event = {
        .kind = kind,
        .text = text,
        .name = name,
    };

    return emit(&event, context, error);
}

static bool run_named_command(const char *arguments,
                              const struct telos_cancel *cancel,
                              telos_frontend_emit_fn emit, void *emit_context,
                              void *context, struct telos_error **error)
{
    (void)arguments;
    (void)cancel;
    return emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE,
                      (const char *)context, NULL, error);
}

static bool run_model_command(const char *arguments,
                              const struct telos_cancel *cancel,
                              telos_frontend_emit_fn emit, void *emit_context,
                              void *context, struct telos_error **error)
{
    (void)cancel;
    (void)context;
    assert(strcmp(arguments, "fixture/second") == 0);
    return emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE,
                      "selected second", NULL, error);
}

static bool run_short_turn(const char *input,
                           const struct telos_cancel *cancel,
                           telos_frontend_emit_fn emit,
                           void *emit_context,
                           const struct telos_frontend_steer *steer,
                           void *turn_context,
                           struct telos_error **error)
{
    (void)steer;
    (void)turn_context;
    assert(strcmp(input, "short") == 0);
    assert(!telos_cancel_requested(cancel));
    if (!emit_event(emit, emit_context, TELOS_FRONTEND_RESPONSE_STARTED, NULL,
                    NULL, error) ||
        !emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                    "first\nsecond", NULL, error)) {
        return false;
    }
    for (size_t index = 0; index < 256; ++index) {
        if (!emit_event(emit, emit_context,
                        (enum telos_frontend_event_kind)999, NULL, NULL,
                        error)) {
            return false;
        }
    }
    return emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_STARTED, NULL,
                      "short-tool", error) &&
           emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_COMPLETED, NULL,
                      "short-tool", error) &&
           emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_FAILED, NULL,
                      "short-tool", error) &&
           emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE,
                      "short complete", NULL, error);
}

static bool run_turn(const char *input, const struct telos_cancel *cancel,
                     telos_frontend_emit_fn emit, void *emit_context,
                     const struct telos_frontend_steer *steer,
                     void *turn_context, struct telos_error **error)
{
    struct turn_fixture *fixture = turn_context;
    size_t turn = fixture->turns++;

    (void)steer;
    if (strcmp(input, "cancel") == 0) {
        const struct timespec delay = {
            .tv_nsec = 1000000,
        };

        if (!emit_event(emit, emit_context, TELOS_FRONTEND_RESPONSE_STARTED,
                        NULL, NULL, error) ||
            !emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                        "waiting for cancel", NULL, error)) {
            return false;
        }
        while (!telos_cancel_requested(cancel)) {
            nanosleep(&delay, NULL);
        }
        *error = telos_error_create(TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                                    "turn cancelled", NULL);
        return false;
    }
    if (strcmp(input, "silent-fail") == 0) {
        return false;
    }
    if (strcmp(input, "invalid-event") == 0) {
        return emit(NULL, emit_context, error);
    }
    if (strcmp(input, "invalid-context") == 0) {
        const struct telos_frontend_event event = {
            .kind = TELOS_FRONTEND_NOTICE,
        };

        return emit(&event, NULL, error);
    }
    if (strcmp(input, "markdown") == 0) {
        static const char markdown[] =
            "# Heading\n"
            "- **bold** and " "\x60" "code" "\x60\n"
            "> quoted\n"
            "\x60\x60\x60" "c\n"
            "printf(\"ok\\n\");\n"
            "\x60\x60\x60";

        return emit_event(emit, emit_context,
                          TELOS_FRONTEND_RESPONSE_STARTED, NULL, NULL,
                          error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                          markdown, NULL, error);
    }
    if (strncmp(input, "scroll-", sizeof("scroll-") - 1) == 0) {
        return emit_event(emit, emit_context,
                          TELOS_FRONTEND_RESPONSE_STARTED, NULL, NULL,
                          error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                          input, NULL, error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE,
                          "scroll complete", NULL, error);
    }
    if (!emit_event(emit, emit_context, TELOS_FRONTEND_RESPONSE_STARTED, NULL,
                    NULL, error) ||
        !emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                    turn == 0 ? "streaming fixture" : "second turn", NULL,
                    error)) {
        return false;
    }
    if (turn == 0) {
        char wrapped[9U * 1024U];
        const struct timespec delay = {
            .tv_nsec = 200000000,
        };

        memset(wrapped, 'x', sizeof(wrapped));
        wrapped[0] = ' ';
        wrapped[sizeof(wrapped) - 6] = '\r';
        wrapped[sizeof(wrapped) - 5] = '\t';
        wrapped[sizeof(wrapped) - 4] = '\n';
        wrapped[sizeof(wrapped) - 3] = 0x01;
        wrapped[sizeof(wrapped) - 2] = 0x7f;
        wrapped[sizeof(wrapped) - 1] = '\0';
        nanosleep(&delay, NULL);
        return emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                          wrapped, NULL, error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_STARTED,
                          NULL, "look\tup", error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_COMPLETED,
                          NULL, "\177\001done", error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_FAILED,
                          NULL, NULL, error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE,
                          "notice from fixture\nsubsequent line\n", NULL,
                          error) &&
               emit_event(emit, emit_context,
                          (enum telos_frontend_event_kind)999, NULL, NULL,
                          error);
    }
    return emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE,
                      "second complete", NULL, error);
}

static bool read_until(int descriptor, char *output, size_t capacity,
                       const char *pattern)
{
    size_t used = 0;

    size_t idle_attempts = 0;

    while (idle_attempts < 100 && used + 1 < capacity) {
        struct pollfd poll_descriptor = {
            .fd = descriptor,
            .events = POLLIN,
        };
        int ready = poll(&poll_descriptor, 1, 20);

        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            return false;
        }
        if (ready == 0) {
            ++idle_attempts;
            continue;
        }
        idle_attempts = 0;
        {
            ssize_t size = read(descriptor, output + used,
                                capacity - used - 1);

            if (size < 0 && errno == EIO) {
                break;
            }
            if (size <= 0) {
                return false;
            }
            used += (size_t)size;
            output[used] = '\0';
            if (strstr(output, pattern) != NULL) {
                return true;
            }
        }
    }
    return false;
}

static char *last_substring(char *text, const char *pattern)
{
    char *last = NULL;
    char *match = text;

    while ((match = strstr(match, pattern)) != NULL) {
        last = match;
        ++match;
    }
    return last;
}

static bool read_until_after(int descriptor, char *output, size_t capacity,
                             const char *first_pattern,
                             const char *second_pattern)
{
    size_t used = 0;
    size_t idle_attempts = 0;

    while (idle_attempts < 100 && used + 1 < capacity) {
        struct pollfd poll_descriptor = {
            .fd = descriptor,
            .events = POLLIN,
        };
        int ready = poll(&poll_descriptor, 1, 20);

        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            return false;
        }
        if (ready == 0) {
            ++idle_attempts;
            continue;
        }
        idle_attempts = 0;
        {
            ssize_t size = read(descriptor, output + used,
                                capacity - used - 1);
            char *first;

            if (size < 0 && errno == EIO) {
                break;
            }
            if (size <= 0) {
                return false;
            }
            used += (size_t)size;
            output[used] = '\0';
            first = strstr(output, first_pattern);
            if (first != NULL &&
                strstr(first + strlen(first_pattern), second_pattern) != NULL) {
                return true;
            }
        }
    }
    return false;
}

static bool wait_for_exit(int descriptor, pid_t child, int *status)
{
    char output[4096];
    size_t attempts = 0;

    while (attempts < 500) {
        struct pollfd poll_descriptor = {
            .fd = descriptor,
            .events = POLLIN,
        };
        pid_t result = waitpid(child, status, WNOHANG);
        int ready;

        if (result == child) {
            return true;
        }
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        ready = poll(&poll_descriptor, 1, 20);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            return false;
        }
        if (ready > 0 &&
            (poll_descriptor.revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
            ssize_t size = read(descriptor, output, sizeof(output));

            if (size < 0 && errno != EINTR && errno != EIO) {
                return false;
            }
            if (size == 0 || (size < 0 && errno == EIO)) {
                do {
                    result = waitpid(child, status, 0);
                } while (result < 0 && errno == EINTR);
                return result == child;
            }
        }
        ++attempts;
    }
    return false;
}

static void run_exit_scenario(const struct telos_frontend_session *session,
                              unsigned short columns, const char *term,
                              bool no_color, const char *keys,
                              size_t key_count)
{
    struct winsize window = {
        .ws_row = 24,
        .ws_col = columns,
    };
    char output[8192] = {0};
    char *slave_name;
    int master = posix_openpt(O_RDWR | O_NOCTTY);
    pid_t child;
    int status;

    assert(master >= 0);
    assert(grantpt(master) == 0);
    assert(unlockpt(master) == 0);
    slave_name = ptsname(master);
    assert(slave_name != NULL);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct telos_tui_frontend_config config;
        struct telos_error *error = NULL;
        int slave = open(slave_name, O_RDWR | O_NOCTTY);
        bool result;

        close(master);
        if (slave < 0) {
            _exit(2);
        }
        assert(ioctl(slave, TIOCSWINSZ, &window) == 0);
        if (term == NULL) {
            assert(unsetenv("TERM") == 0);
        } else {
            assert(setenv("TERM", term, 1) == 0);
        }
        if (no_color) {
            assert(setenv("NO_COLOR", "1", 1) == 0);
        } else {
            assert(unsetenv("NO_COLOR") == 0);
        }
        config = (struct telos_tui_frontend_config){
            .session = session,
            .input_descriptor = slave,
            .output_descriptor = slave,
        };
        result = telos_tui_frontend_run(&config, &error);
        telos_error_release(error);
        close(slave);
        exit(result ? 0 : 1);
    }
    if (session->initial_prompt != NULL &&
        strcmp(session->initial_prompt, "cancel") == 0) {
        assert(read_until(master, output, sizeof(output),
                          "waiting for cancel"));
        memset(output, 0, sizeof(output));
        assert(write(master, "\033", 1) == 1);
        assert(read_until(master, output, sizeof(output), "turn cancelled"));
        assert(write(master, "/exit\r", 6) == 6);
    } else if (session->initial_prompt != NULL) {
        assert(read_until(master, output, sizeof(output), "short complete"));
        assert(write(master, "/exit\r", 6) == 6);
    } else {
        assert(read_until(master, output, sizeof(output), "╭"));
        assert(write(master, keys, key_count) == (ssize_t)key_count);
    }
    memset(output, 0, sizeof(output));
    assert(read_until(master, output, sizeof(output), "\033[?2004l"));
    assert(wait_for_exit(master, child, &status));
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    close(master);
}

int main(void)
{
    struct turn_fixture fixture = {0};
    struct telos_command_registry commands;
    struct telos_model_catalog model_catalog;
    struct telos_error *command_error = NULL;
    struct telos_error *model_error = NULL;
    const struct telos_frontend_session session = {
        .application = "Telos",
        .version = "test",
        .provider = "fixture",
        .model = "fixture-model",
        .working_directory = "/tmp",
        .commands = &commands,
        .completion_count = session_completion_count,
        .completion_at = session_completion_at,
        .model_catalog = &model_catalog,
        .turn = run_turn,
        .turn_context = &fixture,
    };
    static const char pasted_prompt[] =
        "\033[200~one\xff\ntwo\rthree\nfour\nfive\nsix\nseven\neight\n"
        "nine\n你\xc2\x80好\001\033[201~";
    static const char move_to_middle[] =
        "\177\b\033[3~\033[1~\033[F\033[4~\033[3~\033[D";
    static const char finish_edit[] =
        "\033[CX\177\n\033[13;2u\033\r\001tail";
    char oversized_prompt[140];
    static char output[128U * 1024U];
    struct winsize window = {
        .ws_row = 24,
        .ws_col = 40,
    };
    char *slave_name;
    int master;
    pid_t child;
    int status;

    telos_command_registry_initialize(&commands);
    assert(telos_command_registry_add(
        &commands,
        &(const struct telos_command){
            .name = "login",
            .help = "login fixture",
            .run = run_named_command,
            .context = "login command",
        },
        &command_error));
    assert(command_error == NULL);
    assert(telos_command_registry_add(
        &commands,
        &(const struct telos_command){
            .name = "model",
            .help = "model fixture",
            .run = run_model_command,
        },
        &command_error));
    assert(command_error == NULL);

    telos_model_catalog_initialize(&model_catalog);
    assert(telos_model_catalog_add(
        &model_catalog,
        &(const struct telos_model_descriptor){
            .provider = "fixture",
            .id = "first",
            .name = "First Model",
            .api = TELOS_MODEL_API_OPENAI_CHAT,
        },
        &model_error));
    assert(model_error == NULL);
    assert(telos_model_catalog_add(
        &model_catalog,
        &(const struct telos_model_descriptor){
            .provider = "fixture",
            .id = "second",
            .name = "Second Model",
            .api = TELOS_MODEL_API_OPENAI_CHAT,
        },
        &model_error));
    assert(model_error == NULL);
    assert(telos_model_catalog_select(&model_catalog, "fixture", "first",
                                      &model_error));
    assert(model_error == NULL);
    assert(telos_command_registry_add(
        &commands,
        &(const struct telos_command){
            .name = "logout",
            .help = "logout fixture",
            .run = run_named_command,
            .context = "logout command",
        },
        &command_error));
    assert(command_error == NULL);

    master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0);
    assert(grantpt(master) == 0);
    assert(unlockpt(master) == 0);
    slave_name = ptsname(master);
    assert(slave_name != NULL);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct telos_tui_frontend_config config;
        struct telos_error *error = NULL;
        int slave = open(slave_name, O_RDWR | O_NOCTTY);
        bool result;

        close(master);
        if (slave < 0) {
            _exit(2);
        }
        assert(ioctl(slave, TIOCSWINSZ, &window) == 0);
        config = (struct telos_tui_frontend_config){
            .session = &session,
            .input_descriptor = slave,
            .output_descriptor = slave,
            .maximum_input_bytes = 128,
        };
        assert(setenv("TERM", "xterm-256color", 1) == 0);
        result = telos_tui_frontend_run(&config, &error);
        telos_error_release(error);
        close(slave);
        exit(result ? 0 : 1);
    }
    assert(read_until(master, output, sizeof(output), "╭"));
    assert(strstr(output, "Telos test") != NULL);
    assert(strstr(output, "\033[?2026h") != NULL);
    memset(output, 0, sizeof(output));
    memset(oversized_prompt, 'z', sizeof(oversized_prompt));
    assert(write(master, oversized_prompt, sizeof(oversized_prompt)) ==
           (ssize_t)sizeof(oversized_prompt));
    assert(read_until(master, output, sizeof(output), "zzzzzzzz"));
    memset(output, 0, sizeof(output));
    assert(write(master, "\033[A", 3) == 3);
    assert(write(master, "\033", 1) == 1);
    assert(write(master, pasted_prompt, sizeof(pasted_prompt) - 1) ==
           (ssize_t)(sizeof(pasted_prompt) - 1));
    assert(read_until(master, output, sizeof(output), "好"));
    memset(output, 0, sizeof(output));
    assert(write(master, "\033[H", 3) == 3);
    assert(read_until(master, output, sizeof(output), "one"));
    memset(output, 0, sizeof(output));
    assert(write(master, move_to_middle, sizeof(move_to_middle) - 1) ==
           (ssize_t)(sizeof(move_to_middle) - 1));
    assert(read_until(master, output, sizeof(output), "好"));
    memset(output, 0, sizeof(output));
    assert(write(master, finish_edit, sizeof(finish_edit) - 1) ==
           (ssize_t)(sizeof(finish_edit) - 1));
    assert(read_until(master, output, sizeof(output), "tail"));
    memset(output, 0, sizeof(output));
    assert(write(master, "\r", 1) == 1);
    assert(read_until(master, output, sizeof(output), "streaming fixture"));
    memset(output, 0, sizeof(output));
    assert(write(master, "queued\rignored\r", 15) == 15);
    assert(read_until(master, output, sizeof(output), "second complete"));
    assert(strstr(output, "notice from fixture") != NULL);
    assert(strstr(output, "subsequent line") != NULL);
    assert(strstr(output, "fixturesubsequent") == NULL);
    assert(strstr(output, "• \r\n") == NULL);
    assert(strstr(output, "second turn") != NULL);
    {
        static const char *const scroll_prompts[] = {
            "scroll-one", "scroll-two", "scroll-three", "scroll-four",
            "scroll-five", "scroll-six",
        };

        for (size_t index = 0;
             index < sizeof(scroll_prompts) / sizeof(scroll_prompts[0]);
             ++index) {
            memset(output, 0, sizeof(output));
            assert(write(master, scroll_prompts[index],
                         strlen(scroll_prompts[index])) ==
                   (ssize_t)strlen(scroll_prompts[index]));
            assert(write(master, "\r", 1) == 1);
            assert(read_until(master, output, sizeof(output),
                              "scroll complete"));
        }
    }
    memset(output, 0, sizeof(output));
    assert(write(master, "\033[5~", 4) == 4);
    assert(read_until(master, output, sizeof(output), "scroll-one"));
    memset(output, 0, sizeof(output));
    assert(write(master, "\033[6~", 4) == 4);
    assert(write(master, "\033[6~", 4) == 4);
    assert(read_until(master, output, sizeof(output), "scroll-six"));
    memset(output, 0, sizeof(output));
    assert(write(master, "markdown\r", 9) == 9);
    assert(read_until(master, output, sizeof(output), "Heading"));
    assert(strstr(output, "# Heading") == NULL);
    memset(output, 0, sizeof(output));
    assert(read_until(master, output, sizeof(output), "bold"));
    assert(strstr(output, "bold") != NULL);
    memset(output, 0, sizeof(output));
    assert(write(master, "\033[A\033[Bcancel\r",
                 sizeof("\033[A\033[Bcancel\r") - 1) ==
           (ssize_t)(sizeof("\033[A\033[Bcancel\r") - 1));
    assert(read_until(master, output, sizeof(output), "waiting for cancel"));
    memset(output, 0, sizeof(output));
    assert(write(master, "\033", 1) == 1);
    assert(read_until(master, output, sizeof(output), "turn cancelled"));
    memset(output, 0, sizeof(output));
    assert(write(master, "silent-fail\r", 12) == 12);
    assert(read_until(master, output, sizeof(output), "Agent turn failed"));
    memset(output, 0, sizeof(output));
    assert(write(master, "invalid-event\r", 14) == 14);
    assert(read_until(master, output, sizeof(output),
                      "Terminal Frontend Event is invalid"));
    memset(output, 0, sizeof(output));
    assert(write(master, "invalid-context\r", 16) == 16);
    assert(read_until(master, output, sizeof(output),
                      "Terminal Frontend Event is invalid"));
    memset(output, 0, sizeof(output));
    assert(write(master, "/resume\t", 8) == 8);
    assert(read_until(master, output, sizeof(output),
                      "second saved conversat"));
    assert(strstr(output, "first session") != NULL);
    assert(strstr(output, "second saved conversat") != NULL);
    assert(write(master, "\033", 1) == 1);
    memset(output, 0, sizeof(output));
    assert(write(master, "/l", 2) == 2);
    assert(read_until(master, output, sizeof(output), "/l"));
    assert(strstr(output, "(1/2)") != NULL);
    assert(strstr(output, "login fixture") != NULL);
    assert(strstr(output, "logout fixture") != NULL);
    assert(last_substring(output, "login fixture") <
           last_substring(output, "╭"));
    assert(write(master, "\033", 1) == 1);
    memset(output, 0, sizeof(output));
    assert(write(master, "/q\t", 3) == 3);
    assert(read_until(master, output, sizeof(output), "leave Telos"));
    assert(strstr(output, "quit") != NULL);
    assert(write(master, "\033", 1) == 1);
    memset(output, 0, sizeof(output));
    assert(write(master, "/l\t\t\r", 5) == 5);
    assert(read_until(master, output, sizeof(output), "logout command"));
    memset(output, 0, sizeof(output));
    assert(write(master, "/model\r", 7) == 7);
    assert(read_until_after(master, output, sizeof(output),
                            "Model Name: First Model", "ready"));
    assert(strstr(output, "✓") != NULL);
    assert(last_substring(output, "Model Name: First Model") <
           last_substring(output, "╭"));
    memset(output, 0, sizeof(output));
    assert(write(master, "\033[B\r", 4) == 4);
    assert(read_until(master, output, sizeof(output), "selected second"));
    memset(output, 0, sizeof(output));
    assert(write(master, "/help\r", 6) == 6);
    assert(read_until(master, output, sizeof(output), "Telos commands"));
    memset(output, 0, sizeof(output));
    assert(write(master, "/quit\r", 6) == 6);
    assert(read_until(master, output, sizeof(output), "\033[?2004l"));
    assert(wait_for_exit(master, child, &status));
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status) == 0);
    close(master);
    run_exit_scenario(&session, 10, "dumb", false, "\r\033", 2);
    run_exit_scenario(&session, 600, "xterm-256color", true, "\003", 1);
    run_exit_scenario(&session, 80, NULL, false, "\033", 1);
    run_exit_scenario(&session, 80, "xterm-256color", false, "/exit\r", 6);
    {
        struct telos_frontend_session initial_session = session;

        initial_session.initial_prompt = "cancel";
        run_exit_scenario(&initial_session, 80, "xterm-256color", false,
                          NULL, 0);
    }
    {
        struct telos_frontend_session short_session = session;

        short_session.initial_prompt = "short";
        short_session.turn = run_short_turn;
        short_session.turn_context = NULL;
        run_exit_scenario(&short_session, 80, "xterm-256color", true, NULL,
                          0);
    }
    return 0;
}
