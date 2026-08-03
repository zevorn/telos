#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700

#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wchar.h>

#include <telos/plugins/terminal_frontend.h>
#include <telos/command.h>
#include <telos/value.h>

#define TERMINAL_EVENT_CAPACITY 64U
#define TERMINAL_EVENT_TEXT_SIZE 2048U
#define TERMINAL_EVENT_NAME_SIZE 256U
#define TERMINAL_STREAM_SIZE 8192U
#define TERMINAL_MAXIMUM_COLUMNS 512U
#define TERMINAL_MAXIMUM_EDITOR_ROWS 8U
#define TERMINAL_RENDER_LINE_SIZE (TERMINAL_MAXIMUM_COLUMNS * 4U + 128U)
#define TERMINAL_POLL_MILLISECONDS 80
#define TERMINAL_SHELL_OUTPUT_SIZE (256U * 1024U)
#define TERMINAL_SHELL_TIMEOUT_MILLISECONDS 30000U
#define TERMINAL_CLIPBOARD_SIZE (1024U * 1024U)

_Static_assert(TERMINAL_STREAM_SIZE >
                   TERMINAL_EVENT_TEXT_SIZE + TERMINAL_MAXIMUM_COLUMNS,
               "stream must hold an Event chunk and a partial row");

enum queued_event_kind {
    QUEUED_FRONTEND_EVENT = 1,
    QUEUED_TURN_ERROR,
    QUEUED_TURN_COMPLETED,
};

struct queued_event {
    enum queued_event_kind kind;
    enum telos_frontend_event_kind frontend_kind;
    char text[TERMINAL_EVENT_TEXT_SIZE];
    char name[TERMINAL_EVENT_NAME_SIZE];
};

struct terminal_state {
    const struct telos_frontend_session *session;
    int input_descriptor;
    int output_descriptor;
    size_t maximum_input_bytes;
    bool color;
    bool raw_enabled;
    struct termios saved_terminal;

    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_changed;
    struct queued_event events[TERMINAL_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;
    bool queue_shutdown;

    pthread_t worker;
    bool worker_active;
    struct telos_cancel *cancel;
    char active_prompt[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    char queued_prompt[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    bool prompt_queued;

    char input[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    size_t input_size;
    size_t input_cursor;
    char prior_prompt[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    char completion_prefix[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    size_t completion_index;
    bool paste_active;
    bool exit_requested;

    char stream[TERMINAL_STREAM_SIZE];
    size_t stream_size;
    char shell_output[TERMINAL_SHELL_OUTPUT_SIZE];
    char clipboard[TERMINAL_CLIPBOARD_SIZE];
    size_t clipboard_size;
    bool response_active;
    bool response_first_line;
    size_t rendered_rows;
    size_t rendered_cursor_row;
    unsigned int spinner;
};

struct editor_metrics {
    size_t total_rows;
    size_t cursor_row;
    size_t cursor_column;
};

struct plain_context {
    int output_descriptor;
    bool response_started;
};

struct json_context {
    int output_descriptor;
};

static bool plain_emit(const struct telos_frontend_event *event,
                       void *context, struct telos_error **error);

static bool json_emit(const struct telos_frontend_event *event,
                      void *context, struct telos_error **error);

static void disable_raw_mode(struct terminal_state *state);
static bool enable_raw_mode(struct terminal_state *state,
                            struct telos_error **error);
static bool editor_external(struct terminal_state *state,
                            struct telos_error **error);
static void write_header(struct terminal_state *state);

static void set_error(struct telos_error **error,
                      enum telos_error_domain domain,
                      int code,
                      const char *message)
{
    if (error != NULL && *error == NULL) {
        *error = telos_error_create(domain, code, message, NULL);
    }
}

static bool write_all(int descriptor, const char *data, size_t size)
{
    while (size > 0) {
        ssize_t written = write(descriptor, data, size);

        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            return false;
        }
        data += (size_t)written;
        size -= (size_t)written;
    }
    return true;
}

static bool write_text(int descriptor, const char *text)
{
    return write_all(descriptor, text, strlen(text));
}

static int64_t monotonic_milliseconds(void)
{
    struct timespec current;

    if (clock_gettime(CLOCK_MONOTONIC, &current) != 0) {
        return 0;
    }
    return (int64_t)current.tv_sec * INT64_C(1000) +
           current.tv_nsec / INT64_C(1000000);
}

static bool run_shell_command(const char *working_directory,
                              const char *command,
                              char *output,
                              size_t output_size,
                              int *exit_status,
                              struct telos_error **error)
{
    int descriptors[2];
    pid_t child;
    int status = 0;
    int64_t started;
    size_t used = 0;
    bool closed = false;
    bool truncated = false;
    const char *shell = getenv("TELOS_AGENT_SHELL");

    if (working_directory == NULL || command == NULL || output == NULL ||
        output_size < 2 || exit_status == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Shell command arguments are invalid");
        return false;
    }
    if (shell == NULL || shell[0] == '\0') {
        shell = getenv("SHELL");
    }
    if (shell == NULL || shell[0] == '\0') {
        shell = "/bin/sh";
    }
    if (pipe(descriptors) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Shell output pipe could not be created");
        return false;
    }
    child = fork();
    if (child < 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Shell process could not be created");
        return false;
    }
    if (child == 0) {
        int null_descriptor = open("/dev/null", O_RDONLY);

        if (null_descriptor >= 0) {
            dup2(null_descriptor, STDIN_FILENO);
            close(null_descriptor);
        }
        if (chdir(working_directory) != 0 ||
            dup2(descriptors[1], STDOUT_FILENO) < 0 ||
            dup2(descriptors[1], STDERR_FILENO) < 0) {
            _exit(127);
        }
        close(descriptors[0]);
        close(descriptors[1]);
        execl(shell, shell, "-c", command, (char *)NULL);
        _exit(127);
    }
    close(descriptors[1]);
    if (fcntl(descriptors[0], F_SETFL, O_NONBLOCK) != 0) {
        kill(child, SIGKILL);
        close(descriptors[0]);
        waitpid(child, NULL, 0);
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Shell output pipe could not be configured");
        return false;
    }
    started = monotonic_milliseconds();
    while (!closed) {
        struct pollfd descriptor = {
            .fd = descriptors[0],
            .events = POLLIN | POLLHUP,
        };
        char chunk[4096];
        int ready;

        ready = poll(&descriptor, 1, 100);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            kill(child, SIGKILL);
            close(descriptors[0]);
            waitpid(child, NULL, 0);
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Shell output could not be read");
            return false;
        }
        if (ready > 0) {
            ssize_t count;

            do {
                count = read(descriptors[0], chunk, sizeof(chunk));
                if (count > 0) {
                    size_t copy = (size_t)count;

                    if (copy > output_size - 1 - used) {
                        copy = output_size - 1 - used;
                        truncated = true;
                    }
                    if (copy > 0) {
                        memcpy(output + used, chunk, copy);
                        used += copy;
                    }
                }
            } while (count > 0);
            if (count == 0) {
                closed = true;
            } else if (count < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
                       errno != EINTR) {
                kill(child, SIGKILL);
                close(descriptors[0]);
                waitpid(child, NULL, 0);
                set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                          "Shell output could not be read");
                return false;
            }
        }
        if (monotonic_milliseconds() - started >=
            TERMINAL_SHELL_TIMEOUT_MILLISECONDS) {
            kill(child, SIGKILL);
            truncated = true;
            if (used + sizeof("[shell command timed out]\n") < output_size) {
                memcpy(output + used, "[shell command timed out]\n",
                       sizeof("[shell command timed out]\n") - 1);
                used += sizeof("[shell command timed out]\n") - 1;
            }
            closed = true;
        }
    }
    close(descriptors[0]);
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    if (truncated &&
        used + sizeof("[shell output truncated]\n") < output_size) {
        memcpy(output + used, "[shell output truncated]\n",
               sizeof("[shell output truncated]\n") - 1);
        used += sizeof("[shell output truncated]\n") - 1;
    }
    output[used] = '\0';
    *exit_status = WIFEXITED(status) ? WEXITSTATUS(status)
                                     : 128 + WTERMSIG(status);
    return true;
}

static size_t copy_text(char *target, size_t target_size, const char *source)
{
    size_t size = source == NULL ? 0 : strlen(source);

    if (target_size == 0) {
        return 0;
    }
    if (size >= target_size) {
        size = target_size - 1;
    }
    if (size > 0) {
        memcpy(target, source, size);
    }
    target[size] = '\0';
    return size;
}

static size_t utf8_character(const char *text, size_t size, int *columns)
{
    mbstate_t state = {0};
    wchar_t value;
    size_t length;
    int width;

    if (size == 0) {
        *columns = 0;
        return 0;
    }
    length = mbrtowc(&value, text, size, &state);
    if (length == (size_t)-1 || length == (size_t)-2 || length == 0) {
        *columns = 1;
        return 1;
    }
    width = wcwidth(value);
    *columns = width < 0 ? 1 : width;
    return length;
}

static size_t visible_width(const char *text, size_t size)
{
    size_t offset = 0;
    size_t width = 0;

    while (offset < size) {
        int columns;
        size_t length = utf8_character(text + offset, size - offset, &columns);

        if (length == 0) {
            break;
        }
        width += (size_t)columns;
        offset += length;
    }
    return width;
}

static size_t bytes_for_width(const char *text, size_t size, size_t maximum)
{
    size_t offset = 0;
    size_t width = 0;

    while (offset < size) {
        int columns;
        size_t length = utf8_character(text + offset, size - offset, &columns);

        if (length == 0 || width + (size_t)columns > maximum) {
            break;
        }
        width += (size_t)columns;
        offset += length;
    }
    return offset;
}

static size_t previous_character(const char *text, size_t offset)
{
    if (offset == 0) {
        return 0;
    }
    --offset;
    while (offset > 0 && ((unsigned char)text[offset] & 0xc0U) == 0x80U) {
        --offset;
    }
    return offset;
}

static size_t next_character(const char *text, size_t size, size_t offset)
{
    int columns;
    size_t length;

    if (offset >= size) {
        return size;
    }
    length = utf8_character(text + offset, size - offset, &columns);
    return length == 0 ? size : offset + length;
}

static size_t terminal_columns(const struct terminal_state *state)
{
    struct winsize size = {0};
    size_t columns = 80;

    if (ioctl(state->output_descriptor, TIOCGWINSZ, &size) == 0 &&
        size.ws_col > 0) {
        columns = size.ws_col;
    }
    if (columns < 20) {
        columns = 20;
    }
    if (columns > TERMINAL_MAXIMUM_COLUMNS) {
        columns = TERMINAL_MAXIMUM_COLUMNS;
    }
    return columns;
}

static void begin_frame(struct terminal_state *state)
{
    write_text(state->output_descriptor, "\033[?2026h");
}

static void end_frame(struct terminal_state *state)
{
    write_text(state->output_descriptor, "\033[?2026l");
}

static void clear_dynamic(struct terminal_state *state)
{
    char sequence[64];
    int size;

    if (state->rendered_rows == 0) {
        return;
    }
    write_text(state->output_descriptor, "\r");
    if (state->rendered_cursor_row > 0) {
        size = snprintf(sequence, sizeof(sequence), "\033[%zuA",
                        state->rendered_cursor_row);
        write_all(state->output_descriptor, sequence, (size_t)size);
    }
    write_text(state->output_descriptor, "\033[0J");
    state->rendered_rows = 0;
    state->rendered_cursor_row = 0;
}

static bool write_render_line(struct terminal_state *state, const char *line,
                              bool final)
{
    return write_text(state->output_descriptor, line) &&
           write_text(state->output_descriptor, "\033[0m\033[K") &&
           (final || write_text(state->output_descriptor, "\r\n"));
}

static void append_bytes(char *line, size_t line_size, size_t *used,
                         const char *text, size_t size)
{
    if (*used >= line_size - 1) {
        return;
    }
    if (size > line_size - *used - 1) {
        size = line_size - *used - 1;
    }
    memcpy(line + *used, text, size);
    *used += size;
    line[*used] = '\0';
}

static void append_text(char *line, size_t line_size, size_t *used,
                        const char *text)
{
    append_bytes(line, line_size, used, text, strlen(text));
}

static void append_repeat(char *line, size_t line_size, size_t *used,
                          const char *text, size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        append_text(line, line_size, used, text);
    }
}

static void editor_metrics(const struct terminal_state *state,
                           size_t content_width,
                           struct editor_metrics *metrics)
{
    size_t offset = 0;
    size_t row = 0;
    size_t column = 0;
    bool cursor_found = false;

    memset(metrics, 0, sizeof(*metrics));
    while (offset < state->input_size) {
        int columns;
        size_t length;

        if (!cursor_found && offset == state->input_cursor) {
            metrics->cursor_row = row;
            metrics->cursor_column = column;
            cursor_found = true;
        }
        if (state->input[offset] == '\n') {
            ++row;
            column = 0;
            ++offset;
            continue;
        }
        length = utf8_character(state->input + offset,
                                state->input_size - offset, &columns);
        if (column > 0 && column + (size_t)columns > content_width) {
            ++row;
            column = 0;
            continue;
        }
        column += (size_t)columns;
        offset += length;
    }
    if (!cursor_found) {
        metrics->cursor_row = row;
        metrics->cursor_column = column;
    }
    metrics->total_rows = row + 1;
}

static bool write_border(struct terminal_state *state, size_t columns,
                         bool top)
{
    char line[TERMINAL_RENDER_LINE_SIZE] = {0};
    size_t used = 0;

    if (state->color) {
        append_text(line, sizeof(line), &used,
                    state->worker_active ? "\033[38;5;214m"
                                         : "\033[38;5;75m");
    }
    append_text(line, sizeof(line), &used, top ? "╭" : "╰");
    append_repeat(line, sizeof(line), &used, "─", columns - 2);
    append_text(line, sizeof(line), &used, top ? "╮" : "╯");
    return write_render_line(state, line, false);
}

static bool write_editor_row(struct terminal_state *state, const char *text,
                             size_t size, size_t width)
{
    char line[TERMINAL_RENDER_LINE_SIZE] = {0};
    size_t used = 0;
    size_t cells = visible_width(text, size);

    if (state->color) {
        append_text(line, sizeof(line), &used, "\033[38;5;75m");
    }
    append_text(line, sizeof(line), &used, "│ ");
    if (state->color) {
        append_text(line, sizeof(line), &used, "\033[0m");
    }
    append_bytes(line, sizeof(line), &used, text, size);
    append_repeat(line, sizeof(line), &used, " ", width - cells);
    if (state->color) {
        append_text(line, sizeof(line), &used, "\033[38;5;75m");
    }
    append_text(line, sizeof(line), &used, " │");
    return write_render_line(state, line, false);
}

static bool render_editor_rows(struct terminal_state *state,
                               size_t content_width,
                               size_t first_row,
                               size_t last_row)
{
    size_t offset = 0;
    size_t row = 0;
    size_t segment_start = 0;
    size_t column = 0;

    while (offset < state->input_size) {
        int columns;
        size_t length;
        bool newline = state->input[offset] == '\n';

        if (newline) {
            if (row >= first_row && row <= last_row &&
                !write_editor_row(state, state->input + segment_start,
                                  offset - segment_start, content_width)) {
                return false;
            }
            ++row;
            ++offset;
            segment_start = offset;
            column = 0;
            continue;
        }
        length = utf8_character(state->input + offset,
                                state->input_size - offset, &columns);
        if (column > 0 && column + (size_t)columns > content_width) {
            if (row >= first_row && row <= last_row &&
                !write_editor_row(state, state->input + segment_start,
                                  offset - segment_start, content_width)) {
                return false;
            }
            ++row;
            segment_start = offset;
            column = 0;
            continue;
        }
        column += (size_t)columns;
        offset += length;
    }
    if (row >= first_row && row <= last_row) {
        return write_editor_row(state, state->input + segment_start,
                                state->input_size - segment_start,
                                content_width);
    }
    return true;
}

static void sanitize_label(char *target, size_t target_size,
                           const char *source)
{
    size_t used = 0;

    if (target_size == 0) {
        return;
    }
    for (size_t index = 0; source != NULL && source[index] != '\0' &&
                           used + 1 < target_size;
         ++index) {
        unsigned char value = (unsigned char)source[index];

        target[used++] = value < 0x20U || value == 0x7fU ? '?' : (char)value;
    }
    target[used] = '\0';
}

static bool write_footer(struct terminal_state *state, size_t columns,
                         bool final)
{
    static const char *const spinners[] = {"⠋", "⠙", "⠹", "⠸",
                                            "⠼", "⠴", "⠦", "⠧"};
    char working[256];
    char provider[128];
    char model[128];
    char footer[TERMINAL_RENDER_LINE_SIZE] = {0};
    char visible[TERMINAL_RENDER_LINE_SIZE] = {0};
    size_t used = 0;
    size_t size;

    sanitize_label(working, sizeof(working), state->session->working_directory);
    sanitize_label(provider, sizeof(provider),
                   state->session->provider_get == NULL
                       ? state->session->provider
                       : state->session->provider_get(
                             state->session->identity_context));
    sanitize_label(model, sizeof(model),
                   state->session->model_get == NULL
                       ? state->session->model
                       : state->session->model_get(
                             state->session->identity_context));
    snprintf(visible, sizeof(visible), "%s · %s/%s · %s", working, provider,
             model,
             state->worker_active
                 ? spinners[state->spinner %
                            (sizeof(spinners) / sizeof(spinners[0]))]
                 : "ready");
    size = bytes_for_width(visible, strlen(visible), columns);
    if (state->color) {
        append_text(footer, sizeof(footer), &used, "\033[38;5;245m");
    }
    append_bytes(footer, sizeof(footer), &used, visible, size);
    return write_render_line(state, footer, final);
}

static bool render_dynamic(struct terminal_state *state)
{
    struct editor_metrics metrics;
    size_t columns = terminal_columns(state);
    size_t content_width = columns - 4;
    size_t first_row;
    size_t last_row;
    size_t response_rows = state->stream_size > 0 ? 1 : 0;
    size_t editor_rows;
    size_t total_rows;
    char response[TERMINAL_RENDER_LINE_SIZE] = {0};
    size_t response_used = 0;
    char sequence[64];
    int sequence_size;

    editor_metrics(state, content_width, &metrics);
    first_row = metrics.cursor_row >= TERMINAL_MAXIMUM_EDITOR_ROWS
                    ? metrics.cursor_row - TERMINAL_MAXIMUM_EDITOR_ROWS + 1
                    : 0;
    if (first_row + TERMINAL_MAXIMUM_EDITOR_ROWS > metrics.total_rows) {
        first_row = metrics.total_rows > TERMINAL_MAXIMUM_EDITOR_ROWS
                        ? metrics.total_rows - TERMINAL_MAXIMUM_EDITOR_ROWS
                        : 0;
    }
    last_row = first_row + TERMINAL_MAXIMUM_EDITOR_ROWS - 1;
    if (last_row >= metrics.total_rows) {
        last_row = metrics.total_rows - 1;
    }
    editor_rows = last_row - first_row + 1;
    total_rows = response_rows + editor_rows + 3;

    begin_frame(state);
    clear_dynamic(state);
    if (state->stream_size > 0) {
        if (state->color) {
            append_text(response, sizeof(response), &response_used,
                        "\033[38;5;75m");
        }
        append_text(response, sizeof(response), &response_used,
                    state->response_first_line ? "Telos › " : "        ");
        if (state->color) {
            append_text(response, sizeof(response), &response_used,
                        "\033[0m");
        }
        append_bytes(response, sizeof(response), &response_used, state->stream,
                     state->stream_size);
        if (!write_render_line(state, response, false)) {
            end_frame(state);
            return false;
        }
    }
    if (!write_border(state, columns, true) ||
        !render_editor_rows(state, content_width, first_row, last_row) ||
        !write_border(state, columns, false) ||
        !write_footer(state, columns, true)) {
        end_frame(state);
        return false;
    }

    state->rendered_rows = total_rows;
    state->rendered_cursor_row =
        response_rows + 1 + (metrics.cursor_row - first_row);
    write_text(state->output_descriptor, "\r");
    if (total_rows - 1 > state->rendered_cursor_row) {
        sequence_size = snprintf(sequence, sizeof(sequence), "\033[%zuA",
                                 total_rows - 1 - state->rendered_cursor_row);
        write_all(state->output_descriptor, sequence,
                  (size_t)sequence_size);
    }
    sequence_size = snprintf(sequence, sizeof(sequence), "\033[%zuC",
                             metrics.cursor_column + 2);
    write_all(state->output_descriptor, sequence, (size_t)sequence_size);
    end_frame(state);
    return true;
}

static void write_sanitized(struct terminal_state *state, const char *text,
                            size_t size)
{
    size_t start = 0;

    for (size_t index = 0; index < size; ++index) {
        unsigned char value = (unsigned char)text[index];

        if (value >= 0x20U && value != 0x7fU) {
            continue;
        }
        if (index > start) {
            write_all(state->output_descriptor, text + start, index - start);
        }
        if (value == '\t') {
            write_text(state->output_descriptor, "    ");
        }
        start = index + 1;
    }
    if (size > start) {
        write_all(state->output_descriptor, text + start, size - start);
    }
}

static void write_response_segment(struct terminal_state *state,
                                   const char *text, size_t size)
{
    if (state->color) {
        write_text(state->output_descriptor, "\033[38;5;75m");
    }
    write_text(state->output_descriptor,
               state->response_first_line ? "Telos › " : "        ");
    if (state->color) {
        write_text(state->output_descriptor, "\033[0m");
    }
    write_sanitized(state, text, size);
    write_text(state->output_descriptor, "\033[0m\033[K\r\n");
    state->response_first_line = false;
}

static void flush_stream(struct terminal_state *state, bool force)
{
    size_t columns = terminal_columns(state);

    while (state->stream_size > 0) {
        size_t width = columns - 8;
        char *newline = memchr(state->stream, '\n', state->stream_size);
        size_t newline_size = newline == NULL
                                  ? state->stream_size
                                  : (size_t)(newline - state->stream);
        size_t segment = bytes_for_width(state->stream, newline_size, width);
        bool complete_width = visible_width(state->stream, segment) >= width;

        if (newline != NULL && segment == newline_size) {
            write_response_segment(state, state->stream, segment);
            memmove(state->stream, state->stream + segment + 1,
                    state->stream_size - segment - 1);
            state->stream_size -= segment + 1;
            continue;
        }
        if (complete_width) {
            write_response_segment(state, state->stream, segment);
            memmove(state->stream, state->stream + segment,
                    state->stream_size - segment);
            state->stream_size -= segment;
            continue;
        }
        if (force) {
            write_response_segment(state, state->stream, state->stream_size);
            state->stream_size = 0;
        }
        break;
    }
    state->stream[state->stream_size] = '\0';
}

static void stream_text(struct terminal_state *state, const char *text)
{
    for (size_t index = 0; text[index] != '\0'; ++index) {
        unsigned char value = (unsigned char)text[index];

        if (value == '\r') {
            continue;
        }
        if (value < 0x20U && value != '\n' && value != '\t') {
            continue;
        }
        if (value == 0x7fU) {
            continue;
        }
        state->stream[state->stream_size++] = value == '\t' ? ' ' : (char)value;
    }
    state->stream[state->stream_size] = '\0';
    flush_stream(state, false);
}

static void write_status_line_segment(struct terminal_state *state,
                                      const char *symbol,
                                      const char *text, size_t size,
                                      const char *color)
{
    if (color != NULL) {
        write_text(state->output_descriptor, color);
    }
    write_text(state->output_descriptor, symbol);
    write_text(state->output_descriptor, " ");
    write_sanitized(state, text, size);
    write_text(state->output_descriptor, "\033[0m\033[K\r\n");
}

static void write_status_line(struct terminal_state *state, const char *symbol,
                              const char *name, const char *color)
{
    const char *label = name == NULL || name[0] == '\0' ? "tool" : name;
    size_t size = strlen(label);
    size_t start = 0;

    for (size_t index = 0; index <= size; ++index) {
        if (label[index] == '\n' || index == size) {
            if (index > start) {
                write_status_line_segment(state, symbol, label + start,
                                          index - start, color);
            }
            start = index + 1;
        }
    }
}

static void write_clipboard(struct terminal_state *state)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz"
        "0123456789+/";

    write_text(state->output_descriptor, "\033]52;c;");
    for (size_t index = 0; index < state->clipboard_size; index += 3) {
        char encoded[4];
        size_t remaining = state->clipboard_size - index;
        uint32_t value = (uint32_t)(unsigned char)state->clipboard[index]
                         << 16;

        if (remaining > 1) {
            value |= (uint32_t)(unsigned char)state->clipboard[index + 1]
                     << 8;
        }
        if (remaining > 2) {
            value |= (unsigned char)state->clipboard[index + 2];
        }
        encoded[0] = alphabet[(value >> 18) & 0x3fU];
        encoded[1] = alphabet[(value >> 12) & 0x3fU];
        encoded[2] = remaining > 1 ? alphabet[(value >> 6) & 0x3fU] : '=';
        encoded[3] = remaining > 2 ? alphabet[value & 0x3fU] : '=';
        write_all(state->output_descriptor, encoded, sizeof(encoded));
    }
    write_text(state->output_descriptor, "\033\\");
    write_status_line(state, "•", "response copied to clipboard",
                      state->color ? "\033[38;5;245m" : NULL);
    state->clipboard_size = 0;
    state->clipboard[0] = '\0';
}

static bool queue_push(struct terminal_state *state,
                       const struct queued_event *event)
{
    bool result = true;

    pthread_mutex_lock(&state->queue_mutex);
    while (state->event_count == TERMINAL_EVENT_CAPACITY &&
           !state->queue_shutdown) {
        pthread_cond_wait(&state->queue_changed, &state->queue_mutex);
    }
    if (state->queue_shutdown) {
        result = false;
    } else {
        size_t tail =
            (state->event_head + state->event_count) % TERMINAL_EVENT_CAPACITY;

        state->events[tail] = *event;
        state->event_count += 1;
        pthread_cond_broadcast(&state->queue_changed);
    }
    pthread_mutex_unlock(&state->queue_mutex);
    return result;
}

static bool queue_pop(struct terminal_state *state, struct queued_event *event)
{
    bool result = false;

    pthread_mutex_lock(&state->queue_mutex);
    if (state->event_count > 0) {
        *event = state->events[state->event_head];
        state->event_head =
            (state->event_head + 1) % TERMINAL_EVENT_CAPACITY;
        state->event_count -= 1;
        pthread_cond_broadcast(&state->queue_changed);
        result = true;
    }
    pthread_mutex_unlock(&state->queue_mutex);
    return result;
}

static bool queue_frontend_event(const struct telos_frontend_event *event,
                                 void *context,
                                 struct telos_error **error)
{
    struct terminal_state *state = context;
    const char *text;
    size_t size;

    if (event == NULL || state == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Terminal Frontend Event is invalid");
        return false;
    }
    text = event->text == NULL ? "" : event->text;
    size = strlen(text);
    do {
        struct queued_event queued = {
            .kind = QUEUED_FRONTEND_EVENT,
            .frontend_kind = event->kind,
        };
        size_t chunk = size;

        if (chunk >= sizeof(queued.text)) {
            chunk = sizeof(queued.text) - 1;
        }
        if (chunk > 0) {
            memcpy(queued.text, text, chunk);
            queued.text[chunk] = '\0';
        }
        if (size == chunk) {
            copy_text(queued.name, sizeof(queued.name), event->name);
        }
        if (!queue_push(state, &queued)) {
            set_error(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
                      "Terminal Frontend stopped accepting Events");
            return false;
        }
        text += chunk;
        size -= chunk;
    } while (size > 0);
    return true;
}

static void *run_turn(void *context)
{
    struct terminal_state *state = context;
    struct telos_error *error = NULL;
    bool result = state->session->turn(
        state->active_prompt, state->cancel, queue_frontend_event, state,
        state->session->turn_context, &error);

    if (!result) {
        struct queued_event failed = {
            .kind = QUEUED_TURN_ERROR,
        };

        copy_text(failed.text, sizeof(failed.text),
                  error == NULL ? "Agent turn failed"
                                : telos_error_message(error));
        queue_push(state, &failed);
    }
    {
        const struct queued_event completed = {
            .kind = QUEUED_TURN_COMPLETED,
        };

        queue_push(state, &completed);
    }
    telos_error_release(error);
    return NULL;
}

static void write_user_prompt(struct terminal_state *state, const char *prompt)
{
    if (state->color) {
        write_text(state->output_descriptor, "\033[38;5;110m");
    }
    write_text(state->output_descriptor, "You   › ");
    if (state->color) {
        write_text(state->output_descriptor, "\033[0m");
    }
    for (size_t index = 0; prompt[index] != '\0'; ++index) {
        if (prompt[index] == '\n') {
            write_text(state->output_descriptor, "\033[K\r\n        ");
        } else {
            write_sanitized(state, prompt + index, 1);
        }
    }
    write_text(state->output_descriptor, "\033[0m\033[K\r\n\r\n");
}

static void show_help(struct terminal_state *state)
{
    begin_frame(state);
    clear_dynamic(state);
    write_text(state->output_descriptor,
               "Telos commands\r\n"
               "  /help   show this help\r\n"
               "  /clear  clear the Agent conversation\r\n"
               "  /quit   leave Telos\r\n");
    if (state->session->commands != NULL) {
        for (size_t index = 0;
             index < state->session->commands->count; ++index) {
            const struct telos_command *command =
                &state->session->commands->commands[index];

            write_text(state->output_descriptor, "  /");
            write_sanitized(state, command->name, strlen(command->name));
            if (command->help != NULL && command->help[0] != '\0') {
                write_text(state->output_descriptor, "  ");
                write_sanitized(state, command->help,
                                strlen(command->help));
            }
            write_text(state->output_descriptor, "\r\n");
        }
    }
    if (state->session->command_help != NULL &&
        state->session->command_help[0] != '\0') {
        write_text(state->output_descriptor, "  ");
        write_sanitized(state, state->session->command_help,
                        strlen(state->session->command_help));
        write_text(state->output_descriptor, "\r\n");
    }
    write_text(state->output_descriptor,
               "\r\n"
               "Enter submits · Ctrl+J or Alt+Enter adds a line · "
               "Esc cancels · Ctrl+G opens $EDITOR · !command runs a "
               "shell command · !!command sends its output\r\n\r\n");
    end_frame(state);
}

static bool show_shell_result(struct terminal_state *state,
                              const char *command,
                              struct telos_error **error)
{
    int status;

    if (!run_shell_command(state->session->working_directory, command,
                           state->shell_output, sizeof(state->shell_output),
                           &status, error)) {
        return false;
    }
    begin_frame(state);
    clear_dynamic(state);
    write_text(state->output_descriptor, "\r\n");
    write_text(state->output_descriptor, "! ");
    write_sanitized(state, command, strlen(command));
    write_text(state->output_descriptor, "\r\n");
    write_sanitized(state, state->shell_output,
                    strlen(state->shell_output));
    if (state->shell_output[0] != '\0' &&
        state->shell_output[strlen(state->shell_output) - 1] != '\n') {
        write_text(state->output_descriptor, "\r\n");
    }
    if (status != 0) {
        char message[64];

        if (snprintf(message, sizeof(message), "[shell exited with %d]\r\n",
                     status) < (int)sizeof(message)) {
            write_text(state->output_descriptor, message);
        }
    }
    end_frame(state);
    return true;
}

static bool start_turn(struct terminal_state *state, const char *prompt,
                       struct telos_error **error)
{
    struct telos_error *command_error = NULL;
    bool handled = false;
    bool exit_requested = false;
    int result;

    if (strncmp(prompt, "login", sizeof("login") - 1) == 0 &&
        (prompt[sizeof("login") - 1] == '\0' ||
         prompt[sizeof("login") - 1] == ' ')) {
        char command[sizeof(state->input)];

        if (snprintf(command, sizeof(command), "/%s", prompt) >=
            (int)sizeof(command)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Login command is too long");
            return false;
        }
        return start_turn(state, command, error);
    }
    if ((strncmp(prompt, "logout", sizeof("logout") - 1) == 0 &&
         (prompt[sizeof("logout") - 1] == '\0' ||
          prompt[sizeof("logout") - 1] == ' ')) ||
        (strncmp(prompt, "login-status", sizeof("login-status") - 1) == 0 &&
         (prompt[sizeof("login-status") - 1] == '\0' ||
          prompt[sizeof("login-status") - 1] == ' '))) {
        char command[sizeof(state->input)];

        if (snprintf(command, sizeof(command), "/%s", prompt) >=
            (int)sizeof(command)) {
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "Authentication command is too long");
            return false;
        }
        return start_turn(state, command, error);
    }

    if (strcmp(prompt, "/quit") == 0 || strcmp(prompt, "/exit") == 0) {
        state->exit_requested = true;
        return true;
    }
    if (strcmp(prompt, "/help") == 0) {
        show_help(state);
        return true;
    }
    if (state->session->commands != NULL && prompt[0] == '/') {
        if (!telos_command_registry_dispatch(
                state->session->commands, prompt, NULL, queue_frontend_event,
                state, &handled, &exit_requested, &command_error)) {
            if (command_error != NULL &&
                telos_error_code(command_error) == ENOENT) {
                char message[TELOS_COMMAND_ARGUMENT_SIZE];

                if (snprintf(message, sizeof(message),
                             "Unknown command: %s", prompt) >=
                    (int)sizeof(message)) {
                    telos_error_release(command_error);
                    set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                              "Unknown command is too long");
                    return false;
                }
                telos_error_release(command_error);
                return queue_frontend_event(
                    &(const struct telos_frontend_event){
                        .kind = TELOS_FRONTEND_NOTICE,
                        .text = message,
                    },
                    state, error);
            }
            if (error != NULL && *error == NULL) {
                *error = command_error;
                command_error = NULL;
            }
            telos_error_release(command_error);
            return false;
        }
        if (handled) {
            state->exit_requested = state->exit_requested || exit_requested;
            return true;
        }
    }
    if (state->worker_active) {
        if (state->prompt_queued) {
            write_all(state->output_descriptor, "\a", 1);
            return true;
        }
        copy_text(state->queued_prompt, sizeof(state->queued_prompt), prompt);
        state->prompt_queued = true;
        return true;
    }

    if (prompt[0] == '!') {
        const bool capture = prompt[1] == '!';
        const char *command = prompt + (capture ? 2 : 1);

        if (command[0] == '\0') {
            return queue_frontend_event(
                &(const struct telos_frontend_event){
                    .kind = TELOS_FRONTEND_NOTICE,
                    .text = "Usage: !command or !!command",
                },
                state, error);
        }
        if (!show_shell_result(state, command, error)) {
            return false;
        }
        if (capture && state->shell_output[0] != '\0') {
            return start_turn(state, state->shell_output, error);
        }
        return true;
    }

    begin_frame(state);
    clear_dynamic(state);
    write_user_prompt(state, prompt);
    end_frame(state);
    copy_text(state->active_prompt, sizeof(state->active_prompt), prompt);
    copy_text(state->prior_prompt, sizeof(state->prior_prompt), prompt);
    state->response_active = false;
    state->response_first_line = true;
    state->stream_size = 0;
    state->cancel = telos_cancel_create();
    if (state->cancel == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Terminal turn cancellation allocation failed");
        return false;
    }
    result = pthread_create(&state->worker, NULL, run_turn, state);
    if (result != 0) {
        telos_cancel_release(state->cancel);
        state->cancel = NULL;
        set_error(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Terminal Agent worker could not be started");
        return false;
    }
    state->worker_active = true;
    return true;
}

static void handle_frontend_event(struct terminal_state *state,
                                  const struct queued_event *event)
{
    switch (event->frontend_kind) {
    case TELOS_FRONTEND_RESPONSE_STARTED:
        state->response_active = true;
        state->response_first_line = true;
        break;
    case TELOS_FRONTEND_TEXT_DELTA:
        state->response_active = true;
        stream_text(state, event->text);
        break;
    case TELOS_FRONTEND_TOOL_STARTED:
        flush_stream(state, true);
        write_status_line(state, "◆", event->name,
                          state->color ? "\033[38;5;214m" : NULL);
        break;
    case TELOS_FRONTEND_TOOL_COMPLETED:
        flush_stream(state, true);
        write_status_line(state, "✓", event->name,
                          state->color ? "\033[38;5;78m" : NULL);
        break;
    case TELOS_FRONTEND_TOOL_FAILED:
        flush_stream(state, true);
        write_status_line(state, "✗", event->name,
                          state->color ? "\033[38;5;203m" : NULL);
        break;
    case TELOS_FRONTEND_NOTICE:
        flush_stream(state, true);
        write_status_line(state, "•", event->text,
                          state->color ? "\033[38;5;245m" : NULL);
        break;
    case TELOS_FRONTEND_CLIPBOARD:
        {
            const char *text = event->text;
            size_t size = strlen(text);

            if (size > sizeof(state->clipboard) - 1 -
                           state->clipboard_size) {
                state->clipboard_size = 0;
                state->clipboard[0] = '\0';
            }
            if (size <= sizeof(state->clipboard) - 1 -
                           state->clipboard_size) {
                memcpy(state->clipboard + state->clipboard_size, text, size);
                state->clipboard_size += size;
                state->clipboard[state->clipboard_size] = '\0';
            }
        }
        if (strcmp(event->name, "clipboard") == 0) {
            write_clipboard(state);
        }
        break;
    default:
        break;
    }
}

static bool drain_events(struct terminal_state *state,
                         struct telos_error **error)
{
    struct queued_event event;
    bool handled = false;

    while (queue_pop(state, &event)) {
        if (!handled) {
            begin_frame(state);
            clear_dynamic(state);
            handled = true;
        }
        if (event.kind == QUEUED_FRONTEND_EVENT) {
            handle_frontend_event(state, &event);
        } else if (event.kind == QUEUED_TURN_ERROR) {
            flush_stream(state, true);
            write_status_line(state, "✗", event.text,
                              state->color ? "\033[38;5;203m" : NULL);
        } else if (event.kind == QUEUED_TURN_COMPLETED) {
            flush_stream(state, true);
            if (state->response_active) {
                write_text(state->output_descriptor, "\r\n");
            }
            if (pthread_join(state->worker, NULL) != 0) {
                set_error(error, TELOS_ERROR_DOMAIN_STATE, EIO,
                          "Terminal Agent worker could not be joined");
                end_frame(state);
                return false;
            }
            state->worker_active = false;
            state->response_active = false;
            telos_cancel_release(state->cancel);
            state->cancel = NULL;
            if (state->prompt_queued) {
                char prompt[sizeof(state->queued_prompt)];

                copy_text(prompt, sizeof(prompt), state->queued_prompt);
                state->prompt_queued = false;
                state->queued_prompt[0] = '\0';
                end_frame(state);
                if (!start_turn(state, prompt, error)) {
                    return false;
                }
                handled = false;
            }
        }
    }
    if (handled) {
        end_frame(state);
    }
    return true;
}

static bool editor_insert(struct terminal_state *state, const char *text,
                          size_t size)
{
    if (size > state->maximum_input_bytes - state->input_size) {
        write_all(state->output_descriptor, "\a", 1);
        return false;
    }
    memmove(state->input + state->input_cursor + size,
            state->input + state->input_cursor,
            state->input_size - state->input_cursor + 1);
    memcpy(state->input + state->input_cursor, text, size);
    state->input_cursor += size;
    state->input_size += size;
    return true;
}

static void editor_backspace(struct terminal_state *state)
{
    size_t prior = previous_character(state->input, state->input_cursor);

    if (prior == state->input_cursor) {
        return;
    }
    memmove(state->input + prior, state->input + state->input_cursor,
            state->input_size - state->input_cursor + 1);
    state->input_size -= state->input_cursor - prior;
    state->input_cursor = prior;
}

static void editor_delete(struct terminal_state *state)
{
    size_t next = next_character(state->input, state->input_size,
                                 state->input_cursor);

    if (next == state->input_cursor) {
        return;
    }
    memmove(state->input + state->input_cursor, state->input + next,
            state->input_size - next + 1);
    state->input_size -= next - state->input_cursor;
}

static void editor_clear(struct terminal_state *state)
{
    state->input[0] = '\0';
    state->input_size = 0;
    state->input_cursor = 0;
}

static bool submit_editor(struct terminal_state *state,
                          struct telos_error **error)
{
    char prompt[sizeof(state->input)];

    if (state->input_size == 0) {
        return true;
    }
    memcpy(prompt, state->input, state->input_size + 1);
    editor_clear(state);
    return start_turn(state, prompt, error);
}

static bool complete_command(struct terminal_state *state)
{
    const char *prefix;
    size_t prefix_size;
    size_t matches = 0;
    size_t selected = 0;

    if (state->session->commands == NULL || state->input_size < 1 ||
        state->input[0] != '/' ||
        memchr(state->input, ' ', state->input_size) != NULL ||
        memchr(state->input, '\t', state->input_size) != NULL) {
        return false;
    }
    prefix = state->input + 1;
    prefix_size = state->input_size - 1;
    if (prefix_size >= sizeof(state->completion_prefix) ||
        strncmp(state->completion_prefix, state->input,
                sizeof(state->completion_prefix)) != 0) {
        copy_text(state->completion_prefix, sizeof(state->completion_prefix),
                  state->input);
        state->completion_index = 0;
    }
    for (size_t index = 0; index < state->session->commands->count; ++index) {
        const char *name = state->session->commands->commands[index].name;

        if (strncmp(name, prefix, prefix_size) == 0) {
            if (matches == state->completion_index) {
                selected = index;
            }
            ++matches;
        }
    }
    if (matches == 0) {
        return false;
    }
    state->completion_index =
        (state->completion_index + 1) % matches;
    state->input_size = (size_t)snprintf(state->input, sizeof(state->input),
                                         "/%s",
                                         state->session->commands
                                             ->commands[selected]
                                             .name);
    state->input_cursor = state->input_size;
    return true;
}

static void normalize_auth_command(char *line, size_t capacity)
{
    const bool login = line != NULL &&
                       strncmp(line, "login", sizeof("login") - 1) == 0 &&
                       (line[sizeof("login") - 1] == '\0' ||
                        line[sizeof("login") - 1] == ' ');
    const bool logout = line != NULL &&
                        strncmp(line, "logout", sizeof("logout") - 1) == 0 &&
                        (line[sizeof("logout") - 1] == '\0' ||
                         line[sizeof("logout") - 1] == ' ');
    const bool status = line != NULL &&
                        strncmp(line, "login-status",
                                sizeof("login-status") - 1) == 0 &&
                        (line[sizeof("login-status") - 1] == '\0' ||
                         line[sizeof("login-status") - 1] == ' ');

    if (line == NULL || capacity < 2 || line[0] == '/' ||
        !(login || logout || status)) {
        return;
    }
    if (strlen(line) + 2 > capacity) {
        return;
    }
    memmove(line + 1, line, strlen(line) + 1);
    line[0] = '/';
}

static bool key_sequence(const char *data, size_t size, const char *sequence)
{
    size_t sequence_size = strlen(sequence);

    return size >= sequence_size && memcmp(data, sequence, sequence_size) == 0;
}

static size_t handle_key(struct terminal_state *state, const char *data,
                         size_t size, struct telos_error **error)
{
    if (key_sequence(data, size, "\033[200~")) {
        state->paste_active = true;
        return 6;
    }
    if (key_sequence(data, size, "\033[201~")) {
        state->paste_active = false;
        return 6;
    }
    if (state->paste_active) {
        char value = data[0] == '\r' ? '\n' : data[0];

        if (value == '\n' || (unsigned char)value >= 0x20U) {
            editor_insert(state, &value, 1);
        }
        return 1;
    }
    if (key_sequence(data, size, "\033[13;2u") ||
        key_sequence(data, size, "\033\r")) {
        editor_insert(state, "\n", 1);
        return data[1] == '\r' ? 2 : 7;
    }
    if (key_sequence(data, size, "\033[A")) {
        if (state->input_size == 0 && state->prior_prompt[0] != '\0') {
            state->input_size = copy_text(state->input, sizeof(state->input),
                                          state->prior_prompt);
            state->input_cursor = state->input_size;
        }
        return 3;
    }
    if (key_sequence(data, size, "\033[B")) {
        editor_clear(state);
        return 3;
    }
    if (key_sequence(data, size, "\033[C")) {
        state->input_cursor = next_character(
            state->input, state->input_size, state->input_cursor);
        return 3;
    }
    if (key_sequence(data, size, "\033[D")) {
        state->input_cursor =
            previous_character(state->input, state->input_cursor);
        return 3;
    }
    if (key_sequence(data, size, "\033[H") ||
        key_sequence(data, size, "\033[1~")) {
        state->input_cursor = 0;
        return 3;
    }
    if (key_sequence(data, size, "\033[F") ||
        key_sequence(data, size, "\033[4~")) {
        state->input_cursor = state->input_size;
        return 3;
    }
    if ((unsigned char)data[0] == 0x07U) {
        if (state->worker_active) {
            write_all(state->output_descriptor, "\a", 1);
        } else if (!editor_external(state, error)) {
            return 1;
        }
        return 1;
    }
    if (key_sequence(data, size, "\033[3~")) {
        editor_delete(state);
        return 4;
    }
    if (data[0] == '\t') {
        if (!complete_command(state)) {
            write_all(state->output_descriptor, "\a", 1);
        }
        return 1;
    }
    if ((unsigned char)data[0] == 0x0cU) {
        begin_frame(state);
        clear_dynamic(state);
        write_text(state->output_descriptor, "\033[2J\033[H");
        write_header(state);
        end_frame(state);
        return 1;
    }
    if ((unsigned char)data[0] == 0x03U || data[0] == '\033') {
        if (state->worker_active && state->cancel != NULL) {
            telos_cancel_request(state->cancel);
        } else if (state->input_size > 0) {
            editor_clear(state);
        } else {
            state->exit_requested = true;
        }
        return 1;
    }
    if (data[0] == '\r') {
        submit_editor(state, error);
        return 1;
    }
    if (data[0] == '\n') {
        editor_insert(state, "\n", 1);
        return 1;
    }
    if ((unsigned char)data[0] == 0x7fU || data[0] == '\b') {
        editor_backspace(state);
        return 1;
    }
    if ((unsigned char)data[0] >= 0x20U) {
        int columns;
        size_t length = utf8_character(data, size, &columns);

        editor_insert(state, data, length);
        return length;
    }
    return 1;
}

static bool handle_input(struct terminal_state *state,
                         struct telos_error **error)
{
    char input[256];
    ssize_t size = read(state->input_descriptor, input, sizeof(input));
    size_t offset = 0;

    if (size < 0 && (errno == EINTR || errno == EAGAIN)) {
        return true;
    }
    if (size < 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Terminal input could not be read");
        return false;
    }
    if (size == 0) {
        state->exit_requested = true;
        if (state->cancel != NULL) {
            telos_cancel_request(state->cancel);
        }
        return true;
    }
    while (offset < (size_t)size) {
        size_t consumed =
            handle_key(state, input + offset, (size_t)size - offset, error);

        if (consumed == 0) {
            break;
        }
        offset += consumed;
    }
    return error == NULL || *error == NULL;
}

static bool enable_raw_mode(struct terminal_state *state,
                            struct telos_error **error)
{
    struct termios terminal;

    if (tcgetattr(state->input_descriptor, &state->saved_terminal) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Terminal settings could not be read");
        return false;
    }
    terminal = state->saved_terminal;
    terminal.c_iflag &=
        (tcflag_t) ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
    terminal.c_oflag &= (tcflag_t)~OPOST;
    terminal.c_cflag |= CS8;
    terminal.c_lflag &= (tcflag_t) ~(ECHO | ICANON | IEXTEN | ISIG);
    terminal.c_cc[VMIN] = 0;
    terminal.c_cc[VTIME] = 0;
    if (tcsetattr(state->input_descriptor, TCSAFLUSH, &terminal) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Terminal raw mode could not be enabled");
        return false;
    }
    state->raw_enabled = true;
    write_text(state->output_descriptor, "\033[?2004h");
    return true;
}

static void disable_raw_mode(struct terminal_state *state)
{
    if (!state->raw_enabled) {
        return;
    }
    begin_frame(state);
    clear_dynamic(state);
    write_text(state->output_descriptor, "\033[?2004l\033[0m\033[?25h");
    end_frame(state);
    tcsetattr(state->input_descriptor, TCSAFLUSH, &state->saved_terminal);
    state->raw_enabled = false;
}

static bool editor_external(struct terminal_state *state,
                            struct telos_error **error)
{
    char path[] = "/tmp/telos-editor-XXXXXX";
    const char *editor = getenv("VISUAL");
    int descriptor;
    pid_t child;
    int status;
    ssize_t received;
    size_t used = 0;

    if (editor == NULL || editor[0] == '\0') {
        editor = getenv("EDITOR");
    }
    if (editor == NULL || editor[0] == '\0') {
        editor = "vi";
    }
    descriptor = mkstemp(path);
    if (descriptor < 0 ||
        !write_all(descriptor, state->input, state->input_size) ||
        close(descriptor) != 0) {
        if (descriptor >= 0) {
            close(descriptor);
        }
        unlink(path);
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "External editor file could not be prepared");
        return false;
    }
    disable_raw_mode(state);
    child = fork();
    if (child < 0) {
        enable_raw_mode(state, error);
        unlink(path);
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "External editor process could not be created");
        return false;
    }
    if (child == 0) {
        execlp(editor, editor, path, (char *)NULL);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
    }
    if (!enable_raw_mode(state, error)) {
        unlink(path);
        return false;
    }
    descriptor = open(path, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        unlink(path);
        set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "External editor output could not be opened");
        return false;
    }
    while (used < state->maximum_input_bytes) {
        received = read(descriptor, state->input + used,
                        state->maximum_input_bytes - used);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received < 0) {
            close(descriptor);
            unlink(path);
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "External editor output could not be read");
            return false;
        }
        if (received == 0) {
            break;
        }
        used += (size_t)received;
    }
    if (used == state->maximum_input_bytes) {
        char value;

        if (read(descriptor, &value, 1) > 0) {
            close(descriptor);
            unlink(path);
            set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "External editor input is too large");
            return false;
        }
    }
    close(descriptor);
    unlink(path);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        set_error(error, TELOS_ERROR_DOMAIN_STATE, ECHILD,
                  "External editor did not complete successfully");
        return false;
    }
    state->input_size = used;
    state->input_cursor = used;
    state->input[used] = '\0';
    return true;
}

static void write_header(struct terminal_state *state)
{
    char application[64];
    char version[32];
    char provider[128];
    char model[128];

    sanitize_label(application, sizeof(application),
                   state->session->application);
    sanitize_label(version, sizeof(version), state->session->version);
    sanitize_label(provider, sizeof(provider),
                   state->session->provider_get == NULL
                       ? state->session->provider
                       : state->session->provider_get(
                             state->session->identity_context));
    sanitize_label(model, sizeof(model),
                   state->session->model_get == NULL
                       ? state->session->model
                       : state->session->model_get(
                             state->session->identity_context));
    if (state->color) {
        write_text(state->output_descriptor, "\033[1;38;5;75m");
    }
    write_text(state->output_descriptor, application);
    write_text(state->output_descriptor, " ");
    write_text(state->output_descriptor, version);
    write_text(state->output_descriptor, "\033[0m\r\n");
    write_text(state->output_descriptor, "Provider: ");
    write_text(state->output_descriptor, provider);
    write_text(state->output_descriptor, " · Model: ");
    write_text(state->output_descriptor, model);
    write_text(state->output_descriptor,
               "\r\nType /help for commands. Esc cancels the active turn.\r\n"
               "\r\n");
}

static bool run_interactive(struct terminal_state *state,
                            struct telos_error **error)
{
    struct pollfd descriptor = {
        .fd = state->input_descriptor,
        .events = POLLIN,
    };
    bool result = false;

    if (!enable_raw_mode(state, error)) {
        return false;
    }
    write_header(state);
    if (state->session->initial_prompt != NULL &&
        state->session->initial_prompt[0] != '\0' &&
        !start_turn(state, state->session->initial_prompt, error)) {
        goto cleanup;
    }
    while (!state->exit_requested || state->worker_active) {
        int ready;

        if (!drain_events(state, error) ||
            (error != NULL && *error != NULL)) {
            goto cleanup;
        }
        state->spinner += 1;
        if (!render_dynamic(state)) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "Terminal frame could not be rendered");
            goto cleanup;
        }
        ready = poll(&descriptor, 1, TERMINAL_POLL_MILLISECONDS);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Terminal input polling failed");
            goto cleanup;
        }
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0 &&
            !handle_input(state, error)) {
            goto cleanup;
        }
    }
    result = true;

cleanup:
    state->prompt_queued = false;
    if (state->worker_active && state->cancel != NULL) {
        telos_cancel_request(state->cancel);
        while (state->worker_active && drain_events(state, NULL)) {
            struct timespec delay = {
                .tv_nsec = 1000000,
            };

            nanosleep(&delay, NULL);
        }
    }
    disable_raw_mode(state);
    return result;
}

static void plain_write_sanitized(int descriptor, const char *text)
{
    size_t start = 0;
    size_t size = text == NULL ? 0 : strlen(text);

    for (size_t index = 0; index < size; ++index) {
        unsigned char value = (unsigned char)text[index];

        if ((value >= 0x20U && value != 0x7fU) || value == '\n' ||
            value == '\t') {
            continue;
        }
        if (index > start) {
            write_all(descriptor, text + start, index - start);
        }
        start = index + 1;
    }
    if (size > start) {
        write_all(descriptor, text + start, size - start);
    }
}

static bool plain_emit(const struct telos_frontend_event *event,
                       void *context,
                       struct telos_error **error)
{
    struct plain_context *plain = context;

    (void)error;
    if (event->kind == TELOS_FRONTEND_RESPONSE_STARTED) {
        plain->response_started = true;
        write_text(plain->output_descriptor, "Telos > ");
    } else if (event->kind == TELOS_FRONTEND_TEXT_DELTA) {
        if (!plain->response_started) {
            plain->response_started = true;
            write_text(plain->output_descriptor, "Telos > ");
        }
        plain_write_sanitized(plain->output_descriptor, event->text);
    } else if (event->kind == TELOS_FRONTEND_TOOL_STARTED) {
        write_text(plain->output_descriptor, "\n[tool] ");
        plain_write_sanitized(plain->output_descriptor, event->name);
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_TOOL_COMPLETED) {
        write_text(plain->output_descriptor, "\n[done] ");
        plain_write_sanitized(plain->output_descriptor, event->name);
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_TOOL_FAILED) {
        write_text(plain->output_descriptor, "\n[failed] ");
        plain_write_sanitized(plain->output_descriptor, event->name);
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_NOTICE) {
        write_text(plain->output_descriptor, "\n[notice] ");
        plain_write_sanitized(plain->output_descriptor, event->text);
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_CLIPBOARD) {
        write_text(plain->output_descriptor, "\n[clipboard] ");
        plain_write_sanitized(plain->output_descriptor, event->text);
        write_text(plain->output_descriptor, "\n");
    }
    return true;
}

static ssize_t read_plain_line(int descriptor, char *buffer, size_t capacity)
{
    size_t used = 0;

    while (used + 1 < capacity) {
        char value;
        ssize_t result = read(descriptor, &value, 1);

        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result < 0) {
            return -1;
        }
        if (result == 0) {
            break;
        }
        if (value == '\n' || value == '\r') {
            break;
        }
        if ((unsigned char)value >= 0x20U || value == '\t') {
            buffer[used++] = value;
        }
    }
    buffer[used] = '\0';
    return (ssize_t)used;
}

static bool run_plain_turn(struct terminal_state *state, const char *line,
                           struct telos_error **error)
{
    struct plain_context plain = {
        .output_descriptor = state->output_descriptor,
    };
    struct telos_cancel *cancel = telos_cancel_create();
    bool result;

    if (cancel == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Terminal turn cancellation allocation failed");
        return false;
    }
    result = state->session->turn(line, cancel, plain_emit, &plain,
                                  state->session->turn_context, error);
    telos_cancel_release(cancel);
    write_text(state->output_descriptor, "\n");
    return result;
}

static bool run_plain_shell(struct terminal_state *state, const char *line,
                            struct telos_error **error)
{
    const bool capture = line[1] == '!';
    const char *command = line + (capture ? 2 : 1);
    int status;

    if (command[0] == '\0') {
        write_text(state->output_descriptor,
                   "[notice] Usage: !command or !!command\n");
        return true;
    }
    if (!run_shell_command(state->session->working_directory, command,
                           state->shell_output, sizeof(state->shell_output),
                           &status, error)) {
        return false;
    }
    write_text(state->output_descriptor, "! ");
    plain_write_sanitized(state->output_descriptor, command);
    write_text(state->output_descriptor, "\n");
    plain_write_sanitized(state->output_descriptor, state->shell_output);
    if (state->shell_output[0] != '\0' &&
        state->shell_output[strlen(state->shell_output) - 1] != '\n') {
        write_text(state->output_descriptor, "\n");
    }
    if (status != 0) {
        dprintf(state->output_descriptor, "[shell exited with %d]\n", status);
    }
    if (capture && state->shell_output[0] != '\0') {
        return run_plain_turn(state, state->shell_output, error);
    }
    return true;
}

static bool run_plain(struct terminal_state *state, struct telos_error **error)
{
    char line[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];

    write_text(state->output_descriptor, "Telos ");
    write_text(state->output_descriptor, state->session->version);
    write_text(state->output_descriptor, "\n");
    if (state->session->initial_prompt != NULL &&
        state->session->initial_prompt[0] != '\0') {
        write_text(state->output_descriptor, "You > ");
        plain_write_sanitized(state->output_descriptor,
                              state->session->initial_prompt);
        write_text(state->output_descriptor, "\n");
        if (!run_plain_turn(state, state->session->initial_prompt, error)) {
            return false;
        }
        if (state->session->single_turn) {
            return true;
        }
    }
    while (true) {
        ssize_t size;

        write_text(state->output_descriptor, "You > ");
        size = read_plain_line(state->input_descriptor, line,
                               state->maximum_input_bytes + 1);
        if (size < 0) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Terminal input could not be read");
            return false;
        }
        if (size == 0) {
            return true;
        }
        normalize_auth_command(line, sizeof(line));
        if (strcmp(line, "/quit") == 0 || strcmp(line, "/exit") == 0) {
            return true;
        }
        if (line[0] == '!') {
            if (!run_plain_shell(state, line, error)) {
                return false;
            }
            continue;
        }
        if (strcmp(line, "/help") == 0) {
            write_text(state->output_descriptor,
                       "/help /clear /quit\n");
            if (state->session->commands != NULL) {
                for (size_t index = 0;
                     index < state->session->commands->count; ++index) {
                    const struct telos_command *command =
                        &state->session->commands->commands[index];

                    write_text(state->output_descriptor, "/");
                    plain_write_sanitized(state->output_descriptor,
                                          command->name);
                    write_text(state->output_descriptor, " ");
                    plain_write_sanitized(state->output_descriptor,
                                          command->help);
                    write_text(state->output_descriptor, "\n");
                }
            }
            if (state->session->command_help != NULL &&
                state->session->command_help[0] != '\0') {
                plain_write_sanitized(state->output_descriptor,
                                      state->session->command_help);
                write_text(state->output_descriptor, "\n");
            }
            continue;
        }
        if (state->session->commands != NULL && line[0] == '/') {
            struct plain_context command_context = {
                .output_descriptor = state->output_descriptor,
            };
            struct telos_error *command_error = NULL;
            bool handled = false;
            bool exit_requested = false;

            if (!telos_command_registry_dispatch(
                    state->session->commands, line, NULL, plain_emit,
                    &command_context, &handled, &exit_requested,
                    &command_error)) {
                if (command_error != NULL &&
                    telos_error_code(command_error) == ENOENT) {
                    write_text(state->output_descriptor,
                               "[notice] Unknown command: ");
                    plain_write_sanitized(state->output_descriptor, line);
                    write_text(state->output_descriptor, "\n");
                    telos_error_release(command_error);
                    continue;
                }
                if (error != NULL && *error == NULL) {
                    *error = command_error;
                    command_error = NULL;
                }
                telos_error_release(command_error);
                return false;
            }
            if (handled) {
                if (exit_requested) {
                    return true;
                }
                continue;
            }
        }
        if (!run_plain_turn(state, line, error)) {
            return false;
        }
    }
}

static const char *json_event_name(enum telos_frontend_event_kind kind)
{
    switch (kind) {
    case TELOS_FRONTEND_RESPONSE_STARTED:
        return "response_started";
    case TELOS_FRONTEND_TEXT_DELTA:
        return "text_delta";
    case TELOS_FRONTEND_TOOL_STARTED:
        return "tool_started";
    case TELOS_FRONTEND_TOOL_COMPLETED:
        return "tool_completed";
    case TELOS_FRONTEND_TOOL_FAILED:
        return "tool_failed";
    case TELOS_FRONTEND_NOTICE:
        return "notice";
    case TELOS_FRONTEND_CLIPBOARD:
        return "clipboard";
    default:
        return NULL;
    }
}

static bool json_write_event(const struct json_context *json,
                             const char *kind, const char *text,
                             const char *name, struct telos_error **error)
{
    const char *keys[3] = {"event", NULL, NULL};
    const struct telos_value *values[3] = {0};
    struct telos_value *owned[3] = {0};
    struct telos_value *root = NULL;
    char *output = NULL;
    size_t count = 1;
    size_t size;
    bool result = false;

    owned[0] = telos_value_new_string(kind);
    if (owned[0] == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "JSON event allocation failed");
        goto cleanup;
    }
    values[0] = owned[0];
    if (text != NULL) {
        keys[count] = "text";
        values[count] = owned[count] = telos_value_new_string(text);
        if (owned[count] == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "JSON event text allocation failed");
            goto cleanup;
        }
        ++count;
    }
    if (name != NULL) {
        keys[count] = "name";
        values[count] = owned[count] = telos_value_new_string(name);
        if (owned[count] == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "JSON event name allocation failed");
            goto cleanup;
        }
        ++count;
    }
    root = telos_value_new_object(keys, values, count);
    if (root == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "JSON event object allocation failed");
        goto cleanup;
    }
    size = telos_value_json_size(root);
    output = malloc(size);
    if (output == NULL || !telos_value_write_json(root, output, size, NULL,
                                                   error) ||
        !write_all(json->output_descriptor, output, size - 1) ||
        !write_all(json->output_descriptor, "\n", 1)) {
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, EIO,
                      "JSON event could not be written");
        }
        goto cleanup;
    }
    result = true;

cleanup:
    free(output);
    telos_value_release(root);
    for (size_t index = 0; index < count; ++index) {
        telos_value_release(owned[index]);
    }
    return result;
}

static bool json_emit(const struct telos_frontend_event *event,
                      void *context, struct telos_error **error)
{
    const struct json_context *json = context;
    const char *kind;
    bool tool_event;
    bool clipboard_event;

    if (event == NULL || json == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "JSON Frontend Event is invalid");
        return false;
    }
    kind = json_event_name(event->kind);
    if (kind == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "JSON Frontend Event kind is invalid");
        return false;
    }
    tool_event = event->kind == TELOS_FRONTEND_TOOL_STARTED ||
                 event->kind == TELOS_FRONTEND_TOOL_COMPLETED ||
                 event->kind == TELOS_FRONTEND_TOOL_FAILED;
    clipboard_event = event->kind == TELOS_FRONTEND_CLIPBOARD;
    return json_write_event(json, kind, tool_event ? NULL : event->text,
                            tool_event || clipboard_event ? event->name : NULL,
                            error);
}

static bool run_json_turn(struct terminal_state *state, const char *line,
                          struct json_context *json,
                          struct telos_error **error)
{
    struct telos_cancel *cancel = telos_cancel_create();
    struct telos_error *turn_error = NULL;
    bool result;

    if (cancel == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "JSON turn cancellation allocation failed");
        return false;
    }
    if (!json_write_event(json, "user", line, NULL, error)) {
        telos_cancel_release(cancel);
        return false;
    }
    result = state->session->turn(line, cancel, json_emit, json,
                                  state->session->turn_context, &turn_error);
    if (!result) {
        if (!json_write_event(json, "error",
                              turn_error == NULL ? "Agent turn failed"
                                                  : telos_error_message(
                                                        turn_error),
                              NULL, error)) {
            telos_error_release(turn_error);
            telos_cancel_release(cancel);
            return false;
        }
        if (error != NULL && *error == NULL) {
            *error = turn_error;
            turn_error = NULL;
        }
    } else if (!json_write_event(json, "turn_completed", NULL, NULL, error)) {
        result = false;
    }
    telos_error_release(turn_error);
    telos_cancel_release(cancel);
    return result;
}

static bool run_json_shell(struct terminal_state *state, const char *line,
                           struct json_context *json,
                           struct telos_error **error)
{
    const bool capture = line[1] == '!';
    const char *command = line + (capture ? 2 : 1);
    int status;
    char status_text[64];

    if (command[0] == '\0') {
        return json_write_event(json, "notice",
                                "Usage: !command or !!command", NULL,
                                error);
    }
    if (!run_shell_command(state->session->working_directory, command,
                           state->shell_output, sizeof(state->shell_output),
                           &status, error)) {
        return false;
    }
    if (!json_write_event(json, "shell", state->shell_output, command,
                          error)) {
        return false;
    }
    if (snprintf(status_text, sizeof(status_text), "shell exited with %d",
                 status) >= (int)sizeof(status_text)) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Shell status message is too long");
        return false;
    }
    if (status != 0 && !json_write_event(json, "notice", status_text, NULL,
                                         error)) {
        return false;
    }
    if (capture && state->shell_output[0] != '\0') {
        return run_json_turn(state, state->shell_output, json, error);
    }
    return true;
}

static bool run_json(struct terminal_state *state, struct telos_error **error)
{
    struct json_context json = {
        .output_descriptor = state->output_descriptor,
    };
    char line[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];

    if (state->session->initial_prompt != NULL &&
        state->session->initial_prompt[0] != '\0' &&
        !run_json_turn(state, state->session->initial_prompt, &json, error)) {
        return false;
    }
    if (state->session->single_turn) {
        return true;
    }
    while (true) {
        ssize_t size = read_plain_line(state->input_descriptor, line,
                                       state->maximum_input_bytes + 1);
        bool handled = false;
        bool exit_requested = false;

        if (size < 0) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "JSON input could not be read");
            return false;
        }
        if (size == 0) {
            return true;
        }
        normalize_auth_command(line, sizeof(line));
        if (strcmp(line, "/quit") == 0 || strcmp(line, "/exit") == 0) {
            return true;
        }
        if (line[0] == '!') {
            if (!run_json_shell(state, line, &json, error)) {
                return false;
            }
            continue;
        }
        if (strcmp(line, "/help") == 0) {
            if (!json_write_event(
                    &json, "notice",
                    "Use /help or /quit; commands are listed by the session",
                    NULL, error)) {
                return false;
            }
            continue;
        }
        if (state->session->commands != NULL && line[0] == '/') {
            if (!telos_command_registry_dispatch(
                    state->session->commands, line, NULL, json_emit, &json,
                    &handled, &exit_requested, error)) {
                return false;
            }
            if (handled) {
                if (exit_requested) {
                    return true;
                }
                continue;
            }
        }
        if (!run_json_turn(state, line, &json, error)) {
            return false;
        }
    }
}

enum rpc_request_kind {
    RPC_REQUEST_PROMPT = 1,
    RPC_REQUEST_COMMAND,
    RPC_REQUEST_QUIT,
};

static bool parse_rpc_request(const char *line, char *payload,
                              size_t payload_size,
                              enum rpc_request_kind *kind,
                              struct telos_error **error)
{
    struct telos_value *request;
    const char *type;
    const char *text;

    if (line == NULL || payload == NULL || payload_size == 0 || kind == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "RPC request storage is invalid");
        return false;
    }
    request = telos_value_parse_json(line, strlen(line), error);
    if (request == NULL || telos_value_type(request) != TELOS_VALUE_OBJECT) {
        telos_value_release(request);
        if (error != NULL && *error == NULL) {
            set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                      "RPC request must be a JSON object");
        }
        return false;
    }
    type = telos_value_string(telos_value_get(request, "type"));
    if (type == NULL) {
        type = "prompt";
    }
    if (strcmp(type, "quit") == 0 || strcmp(type, "shutdown") == 0) {
        *kind = RPC_REQUEST_QUIT;
        payload[0] = '\0';
        telos_value_release(request);
        return true;
    }
    if (strcmp(type, "prompt") == 0) {
        text = telos_value_string(telos_value_get(request, "message"));
        if (text == NULL) {
            text = telos_value_string(telos_value_get(request, "prompt"));
        }
        *kind = RPC_REQUEST_PROMPT;
    } else if (strcmp(type, "command") == 0) {
        text = telos_value_string(telos_value_get(request, "command"));
        *kind = RPC_REQUEST_COMMAND;
    } else {
        telos_value_release(request);
        set_error(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "RPC request type is unsupported");
        return false;
    }
    if (text == NULL || text[0] == '\0' || strlen(text) >= payload_size) {
        telos_value_release(request);
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "RPC request payload is missing or too large");
        return false;
    }
    memcpy(payload, text, strlen(text) + 1);
    telos_value_release(request);
    return true;
}

static bool run_rpc(struct terminal_state *state, struct telos_error **error)
{
    struct json_context json = {
        .output_descriptor = state->output_descriptor,
    };
    char line[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    char payload[TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];

    if (state->session->initial_prompt != NULL &&
        state->session->initial_prompt[0] != '\0' &&
        !run_json_turn(state, state->session->initial_prompt, &json, error)) {
        return false;
    }
    if (state->session->single_turn) {
        return true;
    }
    while (true) {
        enum rpc_request_kind kind;
        ssize_t size = read_plain_line(state->input_descriptor, line,
                                       state->maximum_input_bytes + 1U);

        if (size < 0) {
            set_error(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "RPC input could not be read");
            return false;
        }
        if (size == 0) {
            return true;
        }
        if (!parse_rpc_request(line, payload, sizeof(payload), &kind, error)) {
            return false;
        }
        if (kind == RPC_REQUEST_QUIT) {
            return true;
        }
        if (kind == RPC_REQUEST_PROMPT) {
            if (payload[0] == '!'
                ? !run_json_shell(state, payload, &json, error)
                : !run_json_turn(state, payload, &json, error)) {
                return false;
            }
            continue;
        }
        {
            bool handled = false;
            bool exit_requested = false;

            normalize_auth_command(payload, sizeof(payload));

            if (state->session->commands == NULL ||
                !telos_command_registry_dispatch(
                    state->session->commands, payload, NULL, json_emit, &json,
                    &handled, &exit_requested, error)) {
                return false;
            }
            if (!handled) {
                if (!json_write_event(&json, "error", "Unknown RPC command",
                                      NULL, error)) {
                    return false;
                }
            } else if (exit_requested) {
                return true;
            }
        }
    }
}

bool telos_terminal_frontend_run(const telos_terminal_frontend_config *config,
                                 struct telos_error **error)
{
    struct terminal_state *state;
    const char *term = getenv("TERM");
    const char *no_color = getenv("NO_COLOR");
    bool interactive;
    bool result;

    if (error != NULL) {
        *error = NULL;
    }
    if (config == NULL || config->session == NULL ||
        config->session->application == NULL ||
        config->session->version == NULL || config->session->provider == NULL ||
        config->session->model == NULL ||
        config->session->working_directory == NULL ||
        config->session->turn == NULL || config->input_descriptor < 0 ||
        config->output_descriptor < 0 ||
        config->maximum_input_bytes >
            TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES) {
        set_error(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Terminal Frontend configuration is invalid");
        return false;
    }
    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        set_error(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Terminal Frontend allocation failed");
        return false;
    }
    state->session = config->session;
    state->input_descriptor = config->input_descriptor;
    state->output_descriptor = config->output_descriptor;
    state->maximum_input_bytes =
        config->maximum_input_bytes == 0
            ? TELOS_TERMINAL_DEFAULT_MAXIMUM_INPUT_BYTES
            : config->maximum_input_bytes;
    state->response_first_line = true;
    interactive = !config->force_plain && isatty(state->input_descriptor) &&
                  isatty(state->output_descriptor);
    state->color = interactive && no_color == NULL && term != NULL &&
                   strcmp(term, "dumb") != 0;
    pthread_mutex_init(&state->queue_mutex, NULL);
    pthread_cond_init(&state->queue_changed, NULL);
    setlocale(LC_CTYPE, "");

    result = config->rpc_mode
                 ? run_rpc(state, error)
                 : (config->json_output
                        ? run_json(state, error)
                 : (interactive ? run_interactive(state, error)
                                : run_plain(state, error)));

    pthread_mutex_lock(&state->queue_mutex);
    state->queue_shutdown = true;
    pthread_cond_broadcast(&state->queue_changed);
    pthread_mutex_unlock(&state->queue_mutex);
    pthread_cond_destroy(&state->queue_changed);
    pthread_mutex_destroy(&state->queue_mutex);
    telos_cancel_release(state->cancel);
    free(state);
    return result;
}

bool telos_terminal_frontend_run_stdio(const telos_frontend_session *session,
                                       struct telos_error **error)
{
    const struct telos_terminal_frontend_config config = {
        .session = session,
        .input_descriptor = STDIN_FILENO,
        .output_descriptor = STDOUT_FILENO,
    };

    return telos_terminal_frontend_run(&config, error);
}
