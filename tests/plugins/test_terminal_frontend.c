#include <assert.h>
#include <errno.h>
#include <string.h>
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

static bool run_turn(const char *input, const struct telos_cancel *cancel,
                     telos_frontend_emit_fn emit, void *emit_context,
                     void *turn_context, struct telos_error **error)
{
    struct turn_fixture *fixture = turn_context;

    assert(!telos_cancel_requested(cancel));
    fixture->turns += 1;
    if (strcmp(input, "/clear") == 0) {
        return emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE,
                          "cleared", NULL, error);
    }
    if (strcmp(input, "delta") == 0 || strcmp(input, "delta\t") == 0) {
        return emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                          "implicit\ttext\n", NULL, error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_FAILED,
                          NULL, NULL, error) &&
               emit_event(emit, emit_context, TELOS_FRONTEND_NOTICE, NULL,
                          NULL, error) &&
               emit_event(emit, emit_context,
                          (enum telos_frontend_event_kind)999, NULL, NULL,
                          error);
    }
    if (strcmp(input, "fail") == 0) {
        *error = telos_error_create(TELOS_ERROR_DOMAIN_STATE, EIO,
                                    "fixture failed", NULL);
        return false;
    }
    assert(strcmp(input, "hello") == 0);
    return emit_event(emit, emit_context, TELOS_FRONTEND_RESPONSE_STARTED,
                      NULL, NULL, error) &&
           emit_event(emit, emit_context, TELOS_FRONTEND_TEXT_DELTA,
                      "hello \033world", NULL, error) &&
           emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_STARTED, NULL,
                      "lookup", error) &&
           emit_event(emit, emit_context, TELOS_FRONTEND_TOOL_COMPLETED, NULL,
                      "lookup", error);
}

static void expect_invalid(const telos_terminal_frontend_config *config)
{
    struct telos_error *error = NULL;

    assert(!telos_terminal_frontend_run(config, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_ARGUMENT);
    telos_error_release(error);
}

static ssize_t read_output(int descriptor, char *output, size_t capacity)
{
    size_t used = 0;

    while (used + 1 < capacity) {
        ssize_t result = read(descriptor, output + used, capacity - used - 1);

        if (result <= 0) {
            break;
        }
        used += (size_t)result;
    }
    output[used] = '\0';
    return (ssize_t)used;
}

int main(void)
{
    static const char input[] =
        "\001hello\n/help\n/clear\ndelta\t\n/quit\n";
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
    int input_pipe[2];
    int output_pipe[2];
    struct telos_terminal_frontend_config config;
    struct telos_frontend_session invalid_session;
    struct telos_error *error = NULL;
    char output[4096];
    int invalid_descriptor;

    assert(pipe(input_pipe) == 0);
    assert(pipe(output_pipe) == 0);
    assert(write(input_pipe[1], input, sizeof(input) - 1) ==
           (ssize_t)(sizeof(input) - 1));
    close(input_pipe[1]);
    config = (struct telos_terminal_frontend_config){
        .session = &session,
        .input_descriptor = input_pipe[0],
        .output_descriptor = output_pipe[1],
        .maximum_input_bytes = 128,
        .force_plain = true,
    };
    assert(telos_terminal_frontend_run(&config, &error));
    assert(error == NULL);
    close(input_pipe[0]);
    close(output_pipe[1]);
    assert(read_output(output_pipe[0], output, sizeof(output)) > 0);
    close(output_pipe[0]);
    assert(fixture.turns == 3);
    assert(strstr(output, "Telos test") != NULL);
    assert(strstr(output, "Telos > hello world") != NULL);
    assert(strstr(output, "[tool] lookup") != NULL);
    assert(strstr(output, "[done] lookup") != NULL);
    assert(strstr(output, "[failed] ") != NULL);
    assert(strstr(output, "[notice] cleared") != NULL);
    assert(strstr(output, "implicit\ttext") != NULL);
    assert(strstr(output, "/help /clear /quit") != NULL);
    assert(strchr(output, '\033') == NULL);

    {
        struct turn_fixture single_fixture = {0};
        const struct telos_frontend_session single_session = {
            .application = "Telos",
            .version = "test",
            .provider = "fixture",
            .model = "fixture-model",
            .working_directory = "/tmp",
            .initial_prompt = "delta",
            .single_turn = true,
            .turn = run_turn,
            .turn_context = &single_fixture,
        };

        assert(pipe(output_pipe) == 0);
        config = (struct telos_terminal_frontend_config){
            .session = &single_session,
            .input_descriptor = STDIN_FILENO,
            .output_descriptor = output_pipe[1],
            .force_plain = true,
        };
        assert(telos_terminal_frontend_run(&config, &error));
        assert(error == NULL);
        close(output_pipe[1]);
        assert(read_output(output_pipe[0], output, sizeof(output)) > 0);
        close(output_pipe[0]);
        assert(single_fixture.turns == 1);
        assert(strstr(output, "You > delta") != NULL);
        assert(telos_terminal_frontend_run_stdio(&single_session, &error));
        assert(error == NULL);
    }

    {
        static const char exit_input[] = "/exit\r";

        assert(pipe(input_pipe) == 0);
        assert(pipe(output_pipe) == 0);
        assert(write(input_pipe[1], exit_input, sizeof(exit_input) - 1) ==
               (ssize_t)(sizeof(exit_input) - 1));
        close(input_pipe[1]);
        config = (struct telos_terminal_frontend_config){
            .session = &session,
            .input_descriptor = input_pipe[0],
            .output_descriptor = output_pipe[1],
            .force_plain = true,
        };
        assert(telos_terminal_frontend_run(&config, &error));
        assert(error == NULL);
        close(input_pipe[0]);
        close(output_pipe[0]);
        close(output_pipe[1]);
    }

    {
        static const char failure_input[] = "fail\n";

        assert(pipe(input_pipe) == 0);
        assert(pipe(output_pipe) == 0);
        assert(write(input_pipe[1], failure_input,
                     sizeof(failure_input) - 1) ==
               (ssize_t)(sizeof(failure_input) - 1));
        close(input_pipe[1]);
        config = (struct telos_terminal_frontend_config){
            .session = &session,
            .input_descriptor = input_pipe[0],
            .output_descriptor = output_pipe[1],
            .force_plain = true,
        };
        assert(!telos_terminal_frontend_run(&config, &error));
        assert(error != NULL);
        assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_STATE);
        telos_error_release(error);
        error = NULL;
        close(input_pipe[0]);
        close(output_pipe[0]);
        close(output_pipe[1]);
    }

    {
        struct telos_frontend_session empty_session = session;

        empty_session.initial_prompt = "";
        assert(pipe(input_pipe) == 0);
        assert(pipe(output_pipe) == 0);
        close(input_pipe[1]);
        config = (struct telos_terminal_frontend_config){
            .session = &empty_session,
            .input_descriptor = input_pipe[0],
            .output_descriptor = output_pipe[1],
        };
        assert(telos_terminal_frontend_run(&config, &error));
        assert(error == NULL);
        close(input_pipe[0]);
        close(output_pipe[0]);
        close(output_pipe[1]);
    }

    {
        struct telos_frontend_session failure_session = session;

        failure_session.initial_prompt = "fail";
        failure_session.single_turn = true;
        assert(pipe(output_pipe) == 0);
        config = (struct telos_terminal_frontend_config){
            .session = &failure_session,
            .input_descriptor = STDIN_FILENO,
            .output_descriptor = output_pipe[1],
            .force_plain = true,
        };
        assert(!telos_terminal_frontend_run(&config, &error));
        assert(error != NULL);
        assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_STATE);
        telos_error_release(error);
        error = NULL;
        close(output_pipe[0]);
        close(output_pipe[1]);
    }

    assert(pipe(output_pipe) == 0);
    invalid_descriptor = dup(STDIN_FILENO);
    assert(invalid_descriptor >= 0);
    close(invalid_descriptor);
    config = (struct telos_terminal_frontend_config){
        .session = &session,
        .input_descriptor = invalid_descriptor,
        .output_descriptor = output_pipe[1],
        .force_plain = true,
    };
    assert(!telos_terminal_frontend_run(&config, &error));
    assert(error != NULL);
    assert(telos_error_domain(error) == TELOS_ERROR_DOMAIN_IO);
    telos_error_release(error);
    error = NULL;
    close(output_pipe[0]);
    close(output_pipe[1]);

    config = (struct telos_terminal_frontend_config){
        .session = &session,
        .input_descriptor = STDIN_FILENO,
        .output_descriptor = STDOUT_FILENO,
        .force_plain = true,
    };
    assert(!telos_terminal_frontend_run(NULL, NULL));
    expect_invalid(NULL);
    config.session = NULL;
    expect_invalid(&config);
    config.session = &invalid_session;
    invalid_session = session;
    invalid_session.application = NULL;
    expect_invalid(&config);
    invalid_session = session;
    invalid_session.version = NULL;
    expect_invalid(&config);
    invalid_session = session;
    invalid_session.provider = NULL;
    expect_invalid(&config);
    invalid_session = session;
    invalid_session.model = NULL;
    expect_invalid(&config);
    invalid_session = session;
    invalid_session.working_directory = NULL;
    expect_invalid(&config);
    invalid_session = session;
    invalid_session.turn = NULL;
    expect_invalid(&config);
    config.session = &session;
    config.input_descriptor = -1;
    expect_invalid(&config);
    config.input_descriptor = STDIN_FILENO;
    config.output_descriptor = -1;
    expect_invalid(&config);
    config.output_descriptor = STDOUT_FILENO;
    config.maximum_input_bytes =
        TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U;
    expect_invalid(&config);
    return 0;
}
