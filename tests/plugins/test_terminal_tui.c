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

#include <telos/plugins/terminal_frontend.h>

struct turn_fixture {
    size_t turns;
};

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

static bool run_short_turn(const char *input,
                           const struct telos_cancel *cancel,
                           telos_frontend_emit_fn emit,
                           void *emit_context,
                           void *turn_context,
                           struct telos_error **error)
{
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
                     void *turn_context, struct telos_error **error)
{
    struct turn_fixture *fixture = turn_context;
    size_t turn = fixture->turns++;

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
        struct telos_terminal_frontend_config config;
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
        config = (struct telos_terminal_frontend_config){
            .session = session,
            .input_descriptor = slave,
            .output_descriptor = slave,
        };
        result = telos_terminal_frontend_run(&config, &error);
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
    const struct telos_frontend_session session = {
        .application = "Telos",
        .version = "test",
        .provider = "fixture",
        .model = "fixture-model",
        .working_directory = "/tmp",
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

    master = posix_openpt(O_RDWR | O_NOCTTY);
    assert(master >= 0);
    assert(grantpt(master) == 0);
    assert(unlockpt(master) == 0);
    slave_name = ptsname(master);
    assert(slave_name != NULL);
    child = fork();
    assert(child >= 0);
    if (child == 0) {
        struct telos_terminal_frontend_config config;
        struct telos_error *error = NULL;
        int slave = open(slave_name, O_RDWR | O_NOCTTY);
        bool result;

        close(master);
        if (slave < 0) {
            _exit(2);
        }
        assert(ioctl(slave, TIOCSWINSZ, &window) == 0);
        config = (struct telos_terminal_frontend_config){
            .session = &session,
            .input_descriptor = slave,
            .output_descriptor = slave,
            .maximum_input_bytes = 128,
        };
        assert(setenv("TERM", "xterm-256color", 1) == 0);
        result = telos_terminal_frontend_run(&config, &error);
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
