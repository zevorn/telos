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

#include <telos/plugins/tui_frontend.h>
#include <telos/command.h>
#include <telos/tui_plugin.h>
#include <telos/value.h>

#define TUI_EVENT_CAPACITY 64U
#define TUI_EVENT_TEXT_SIZE 2048U
#define TUI_EVENT_NAME_SIZE 256U
#define TUI_STREAM_SIZE 8192U
#define TUI_MAXIMUM_COLUMNS 512U
#define TUI_MAXIMUM_EDITOR_ROWS 8U
#define TUI_RENDER_LINE_SIZE (TUI_MAXIMUM_COLUMNS * 4U + 128U)
#define TUI_POLL_MILLISECONDS 80
#define TUI_SHELL_OUTPUT_SIZE (256U * 1024U)
#define TUI_SHELL_TIMEOUT_MILLISECONDS 30000U
#define TUI_CLIPBOARD_SIZE (1024U * 1024U)
#define TUI_MODEL_SELECTOR_VISIBLE 8U
#define TUI_COMMAND_COMPLETION_VISIBLE 8U
#define TUI_TOOL_CAPACITY 64U
#define TUI_TOOL_VISIBLE 4U
#define TUI_TOOL_NAME_SIZE 128U
#define TUI_TOOL_DETAIL_SIZE 256U
#define TUI_STEER_CAPACITY 8U
#define TUI_HISTORY_CAPACITY 1024U
#define TUI_HISTORY_VISIBLE 20U
#define TUI_HISTORY_PAGE_SIZE 8U

_Static_assert(TUI_STREAM_SIZE >
                   TUI_EVENT_TEXT_SIZE + TUI_MAXIMUM_COLUMNS,
               "stream must hold an Event chunk and a partial row");

enum queued_event_kind {
    QUEUED_FRONTEND_EVENT = 1,
    QUEUED_TURN_ERROR,
    QUEUED_TURN_COMPLETED,
};

struct queued_event {
    enum queued_event_kind kind;
    enum telos_frontend_event_kind frontend_kind;
    char text[TUI_EVENT_TEXT_SIZE];
    char name[TUI_EVENT_NAME_SIZE];
};

enum tui_tool_state {
    TUI_TOOL_RUNNING = 1,
    TUI_TOOL_COMPLETED,
    TUI_TOOL_FAILED,
};

struct tui_tool_entry {
    enum tui_tool_state state;
    char name[TUI_TOOL_NAME_SIZE];
    char detail[TUI_TOOL_DETAIL_SIZE];
};

enum tui_history_kind {
    TUI_HISTORY_PLAIN = 1,
    TUI_HISTORY_USER,
    TUI_HISTORY_RESPONSE,
    TUI_HISTORY_STATUS,
};

struct tui_history_entry {
    enum tui_history_kind kind;
    bool continuation;
    char symbol[8];
    char text[TUI_RENDER_LINE_SIZE];
};

struct tui_state {
    const struct telos_frontend_session *session;
    int input_descriptor;
    int output_descriptor;
    size_t maximum_input_bytes;
    bool color;
    bool raw_enabled;
    struct termios saved_terminal;

    pthread_mutex_t queue_mutex;
    pthread_cond_t queue_changed;
    struct queued_event events[TUI_EVENT_CAPACITY];
    size_t event_head;
    size_t event_count;
    bool queue_shutdown;

    pthread_t worker;
    bool worker_active;
    struct telos_cancel *cancel;
    char active_prompt[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    char steer_prompts[TUI_STEER_CAPACITY]
                      [TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    size_t steer_head;
    size_t steer_count;
    /*
     * When the worker runs a blocking command such as /login instead of an
     * agent turn, this holds the command line and flags the worker dispatch
     * path.
     */
    char worker_command_line[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES +
                             1U];
    bool worker_command;

    char input[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    size_t input_size;
    size_t input_cursor;
    char prior_prompt[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    char completion_prefix[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    size_t completion_index;
    bool completion_active;
    bool model_selector_active;
    size_t model_selector_index;
    size_t model_selector_offset;
    bool paste_active;
    bool exit_requested;

    char stream[TUI_STREAM_SIZE];
    size_t stream_size;
    char shell_output[TUI_SHELL_OUTPUT_SIZE];
    char clipboard[TUI_CLIPBOARD_SIZE];
    size_t clipboard_size;
    bool response_active;
    bool response_first_line;
    bool markdown_code_block;
    enum tui_turn_phase {
        TUI_PHASE_IDLE = 0,
        TUI_PHASE_WAITING,
        TUI_PHASE_THINKING,
        TUI_PHASE_RESPONSE,
        TUI_PHASE_TOOLS,
    } turn_phase;
    int64_t turn_start_ms;
    int64_t thinking_start_ms;
    int64_t response_start_ms;
    int64_t tools_start_ms;
    int64_t tools_total_ms;
    bool turn_summary_written;
    char thinking_buffer[4096];
    size_t thinking_size;
    bool thinking_collapsed;
    struct tui_tool_entry tools[TUI_TOOL_CAPACITY];
    size_t tool_count;
    bool tools_collapsed;
    struct tui_history_entry history[TUI_HISTORY_CAPACITY];
    size_t history_head;
    size_t history_count;
    size_t history_scroll;
    size_t rendered_rows;
    size_t rendered_cursor_row;
    unsigned int spinner;
    volatile sig_atomic_t resize_pending;

    const struct telos_tui_plugin_definition_v1
        *tui_plugins[TELOS_TUI_MAXIMUM_PLUGINS];
    size_t tui_plugin_count;

    const struct telos_tui_overlay *active_overlay;
    void *active_overlay_context;
    size_t overlay_columns;
    size_t overlay_rows;
};

struct telos_tui_host {
    struct tui_state *state;
};

/*
 * SIGWINCH forwarding: the interactive loop repaints on resize, so
 * the user's drag/resize interactions never leave a stale frame.
 */
static struct tui_state *interactive_state;

static void handle_resize(int signal_number)
{
    (void)signal_number;
    if (interactive_state != NULL) {
        interactive_state->resize_pending = 1;
    }
}

struct editor_metrics {
    size_t total_rows;
    size_t cursor_row;
    size_t cursor_column;
};

static bool render_model_selector_rows(struct tui_state *state,
                                       size_t columns, size_t *rows);
static bool render_tool_panel(struct tui_state *state, size_t columns,
                              size_t *rows);
static bool render_history(struct tui_state *state, size_t columns,
                           size_t maximum_rows, size_t *rows);
static size_t model_selector_count(const struct tui_state *state);
static void sanitize_label(char *target, size_t target_size,
                           const char *source);
static void write_markdown_segment(struct tui_state *state,
                                   const char *text, size_t size);

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

static void disable_raw_mode(struct tui_state *state);
static bool enable_raw_mode(struct tui_state *state,
                            struct telos_error **error);
static bool editor_external(struct tui_state *state,
                            struct telos_error **error);
static void write_header(struct tui_state *state);
static bool refresh_footer(struct tui_state *state);

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
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Shell output pipe could not be created");
        return false;
    }
    child = fork();
    if (child < 0) {
        close(descriptors[0]);
        close(descriptors[1]);
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
            telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
                telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
                          "Shell output could not be read");
                return false;
            }
        }
        if (monotonic_milliseconds() - started >=
            TUI_SHELL_TIMEOUT_MILLISECONDS) {
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

static size_t terminal_columns(const struct tui_state *state)
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
    if (columns > TUI_MAXIMUM_COLUMNS) {
        columns = TUI_MAXIMUM_COLUMNS;
    }
    return columns;
}

static size_t terminal_rows(const struct tui_state *state)
{
    struct winsize size = {0};
    size_t rows = 24;

    if (ioctl(state->output_descriptor, TIOCGWINSZ, &size) == 0 &&
        size.ws_row > 0) {
        rows = size.ws_row;
    }
    if (rows < 8) {
        rows = 8;
    }
    if (rows > 256) {
        rows = 256;
    }
    return rows;
}

static struct tui_history_entry *history_at(
    struct tui_state *state, size_t ordinal)
{
    return &state->history[(state->history_head + ordinal) %
                            TUI_HISTORY_CAPACITY];
}

static struct tui_history_entry *history_push(
    struct tui_state *state)
{
    struct tui_history_entry *entry;

    if (state->history_scroll > 0) {
        ++state->history_scroll;
    }
    if (state->history_count == TUI_HISTORY_CAPACITY) {
        state->history_head =
            (state->history_head + 1) % TUI_HISTORY_CAPACITY;
        --state->history_count;
    }
    entry = history_at(state, state->history_count++);
    memset(entry, 0, sizeof(*entry));
    return entry;
}

static size_t history_prefix_width(enum tui_history_kind kind)
{
    switch (kind) {
    case TUI_HISTORY_USER:
    case TUI_HISTORY_RESPONSE:
        return 8;
    case TUI_HISTORY_STATUS:
        return 2;
    default:
        return 0;
    }
}

static void history_append_line(struct tui_state *state,
                                enum tui_history_kind kind,
                                const char *symbol, const char *text,
                                size_t size, bool continuation)
{
    size_t columns = terminal_columns(state);
    size_t prefix = history_prefix_width(kind);
    size_t width = columns > prefix ? columns - prefix : 1;

    if (size == 0) {
        struct tui_history_entry *entry = history_push(state);

        entry->kind = kind;
        entry->continuation = continuation;
        copy_text(entry->symbol, sizeof(entry->symbol), symbol);
        return;
    }
    while (size > 0) {
        size_t chunk = bytes_for_width(text, size, width);
        struct tui_history_entry *entry;

        if (chunk == 0) {
            chunk = next_character(text, size, 0);
            if (chunk == 0) {
                chunk = 1;
            }
        }
        entry = history_push(state);
        entry->kind = kind;
        entry->continuation = continuation;
        copy_text(entry->symbol, sizeof(entry->symbol), symbol);
        if (chunk >= sizeof(entry->text)) {
            chunk = sizeof(entry->text) - 1;
        }
        memcpy(entry->text, text, chunk);
        entry->text[chunk] = '\0';
        text += chunk;
        size -= chunk;
        continuation = true;
    }
}

static void history_append_text(struct tui_state *state,
                                enum tui_history_kind kind,
                                const char *symbol, const char *text)
{
    size_t size = text == NULL ? 0 : strlen(text);
    size_t offset = 0;
    bool continuation = false;

    if (size == 0) {
        history_append_line(state, kind, symbol, "", 0, false);
        return;
    }
    while (offset < size) {
        const char *newline = memchr(text + offset, '\n', size - offset);
        size_t end = newline == NULL ? size : (size_t)(newline - text);

        history_append_line(state, kind, symbol, text + offset,
                            end - offset, continuation);
        continuation = true;
        if (newline == NULL) {
            break;
        }
        offset = end + 1;
        if (offset == size) {
            break;
        }
    }
}

static void begin_frame(struct tui_state *state)
{
    write_text(state->output_descriptor, "\033[?2026h");
}

static void end_frame(struct tui_state *state)
{
    write_text(state->output_descriptor, "\033[?2026l");
}

static void clear_dynamic(struct tui_state *state)
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

static bool write_render_line(struct tui_state *state, const char *line,
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

static void editor_metrics(const struct tui_state *state,
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

static bool write_border_line(struct tui_state *state, size_t columns,
                              bool top, bool final)
{
    char line[TUI_RENDER_LINE_SIZE] = {0};
    size_t used = 0;

    if (state->color) {
        append_text(line, sizeof(line), &used,
                    state->worker_active ? "\033[38;5;214m"
                                         : "\033[38;5;75m");
    }
    append_text(line, sizeof(line), &used, top ? "╭" : "╰");
    append_repeat(line, sizeof(line), &used, "─", columns - 2);
    append_text(line, sizeof(line), &used, top ? "╮" : "╯");
    return write_render_line(state, line, final);
}

static bool write_border(struct tui_state *state, size_t columns,
                         bool top)
{
    return write_border_line(state, columns, top, false);
}

static bool write_selector_line(struct tui_state *state,
                                const char *text, size_t columns, bool final)
{
    char safe[TUI_RENDER_LINE_SIZE] = {0};
    char line[TUI_RENDER_LINE_SIZE] = {0};
    size_t content_width = columns - 4;
    size_t text_size;
    size_t used = 0;

    sanitize_label(safe, sizeof(safe), text);
    text_size = bytes_for_width(safe, strlen(safe), content_width);
    append_text(line, sizeof(line), &used, "│ ");
    append_bytes(line, sizeof(line), &used, safe, text_size);
    append_repeat(line, sizeof(line), &used, " ",
                  content_width - visible_width(safe, text_size));
    append_text(line, sizeof(line), &used, " │");
    return write_render_line(state, line, final);
}

static bool write_editor_row(struct tui_state *state, const char *text,
                             size_t size, size_t width)
{
    char line[TUI_RENDER_LINE_SIZE] = {0};
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

static bool render_editor_rows(struct tui_state *state,
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

static void format_working_directory(char *target, size_t target_size,
                                     const char *directory,
                                     const char *home_directory)
{
    size_t home_size = home_directory == NULL ? 0 : strlen(home_directory);

    if (home_size > 0 && directory != NULL &&
        strncmp(directory, home_directory, home_size) == 0 &&
        (directory[home_size] == '\0' || directory[home_size] == '/')) {
        if (snprintf(target, target_size, "~%s", directory + home_size) >=
            (int)target_size) {
            target[target_size - 1] = '\0';
        }
        sanitize_label(target, target_size, target);
        return;
    }
    sanitize_label(target, target_size, directory);
}

static void append_footer_segment(char *line, size_t line_size, size_t *used,
                                  bool *has_segment, const char *text)
{
    if (text == NULL || text[0] == '\0') {
        return;
    }
    if (*has_segment) {
        append_text(line, line_size, used, " · ");
    }
    append_text(line, line_size, used, text);
    *has_segment = true;
}

static const char *command_completion_source(
    const struct tui_state *state)
{
    return state->completion_active ? state->completion_prefix : state->input;
}

static const struct telos_command terminal_quit_command = {
    .name = "quit",
    .help = "leave Telos",
};

static bool session_completion_valid(const struct tui_state *state)
{
    const char *source = command_completion_source(state);
    const size_t command_size = sizeof("/resume") - 1U;

    return state->session->completion_count != NULL &&
           state->session->completion_at != NULL && state->input_size >= 1 &&
           state->input[0] == '/' &&
           state->input_cursor == state->input_size &&
           strncmp(source, "/resume", command_size) == 0 &&
           (source[command_size] == '\0' || source[command_size] == ' ' ||
            source[command_size] == '\t');
}

static bool command_completion_valid(const struct tui_state *state)
{
    const char *source = command_completion_source(state);
    size_t source_size = strlen(source);

    return state->input_size >= 1 && state->input[0] == '/' &&
           state->input_cursor == state->input_size && source_size >= 1 &&
           source[0] == '/' && memchr(state->input, ' ', state->input_size) ==
                                    NULL &&
           memchr(state->input, '\t', state->input_size) == NULL &&
           memchr(source, ' ', source_size) == NULL &&
           memchr(source, '\t', source_size) == NULL;
}

static bool command_completion_matches(const struct tui_state *state,
                                       const struct telos_command *command)
{
    const char *source = command_completion_source(state) + 1;
    size_t source_size = strlen(source);

    return strncmp(command->name, source, source_size) == 0;
}

static size_t command_completion_count(const struct tui_state *state)
{
    size_t count = 0;

    if (!command_completion_valid(state)) {
        return 0;
    }
    if (state->session->commands != NULL) {
        for (size_t index = 0; index < state->session->commands->count;
             ++index) {
            if (command_completion_matches(
                    state, &state->session->commands->commands[index])) {
                ++count;
            }
        }
    }
    if (command_completion_matches(state, &terminal_quit_command)) {
        ++count;
    }
    return count;
}

static const struct telos_command *command_completion_at(
    const struct tui_state *state, size_t ordinal)
{
    if (!command_completion_valid(state)) {
        return NULL;
    }
    if (state->session->commands != NULL) {
        for (size_t index = 0; index < state->session->commands->count;
             ++index) {
            const struct telos_command *command =
                &state->session->commands->commands[index];

            if (!command_completion_matches(state, command)) {
                continue;
            }
            if (ordinal == 0) {
                return command;
            }
            --ordinal;
        }
    }
    if (command_completion_matches(state, &terminal_quit_command) &&
        ordinal == 0) {
        return &terminal_quit_command;
    }
    return NULL;
}

static size_t command_completion_selected(const struct tui_state *state,
                                          size_t count)
{
    const char *current = state->input + 1;
    size_t ordinal = 0;

    if (state->session->commands != NULL) {
        for (size_t index = 0; index < state->session->commands->count;
             ++index) {
            const struct telos_command *command =
                &state->session->commands->commands[index];

            if (!command_completion_matches(state, command)) {
                continue;
            }
            if (strcmp(command->name, current) == 0) {
                return ordinal;
            }
            ++ordinal;
        }
    }
    if (command_completion_matches(state, &terminal_quit_command)) {
        if (strcmp(terminal_quit_command.name, current) == 0) {
            return ordinal;
        }
        ++ordinal;
    }
    if (state->completion_active && count > 0) {
        return (state->completion_index + count - 1) % count;
    }
    return 0;
}

static size_t completion_count(const struct tui_state *state)
{
    size_t base;

    if (session_completion_valid(state)) {
        return state->session->completion_count(
            command_completion_source(state), state->session->completion_context);
    }
    base = command_completion_count(state);
    if (base == 0 && state->input_size > 0 && state->input[0] != '/') {
        /* Query plugin completion providers for non-command input. */
        for (size_t i = 0; i < state->tui_plugin_count; ++i) {
            const struct telos_tui_plugin_definition_v1 *plugin =
                state->tui_plugins[i];

            for (size_t j = 0; j < plugin->completion_provider_count; ++j) {
                const struct telos_tui_completion_provider *provider =
                    &plugin->completion_providers[j];

                if (provider->count != NULL) {
                    size_t n = provider->count(provider->context,
                                               state->input);

                    if (n > 0) {
                        return n;
                    }
                }
            }
        }
    }
    return base;
}

static bool completion_item_at(
    const struct tui_state *state, size_t ordinal,
    struct telos_frontend_completion_item *item)
{
    const struct telos_command *command;

    if (item == NULL) {
        return false;
    }
    memset(item, 0, sizeof(*item));
    if (session_completion_valid(state)) {
        return state->session->completion_at(
            command_completion_source(state), ordinal, item,
            state->session->completion_context);
    }
    command = command_completion_at(state, ordinal);
    if (command != NULL) {
        if (snprintf(item->value, sizeof(item->value), "/%s",
                      command->name) >= (int)sizeof(item->value) ||
            snprintf(item->label, sizeof(item->label), "%s",
                      command->name) >= (int)sizeof(item->label)) {
            return false;
        }
        if (command->help != NULL &&
            snprintf(item->detail, sizeof(item->detail), "%s",
                      command->help) >= (int)sizeof(item->detail)) {
            return false;
        }
        return true;
    }

    /* Query plugin completion providers. */
    if (state->input_size > 0 && state->input[0] != '/') {
        for (size_t i = 0; i < state->tui_plugin_count; ++i) {
            const struct telos_tui_plugin_definition_v1 *plugin =
                state->tui_plugins[i];

            for (size_t j = 0; j < plugin->completion_provider_count;
                 ++j) {
                const struct telos_tui_completion_provider *provider =
                    &plugin->completion_providers[j];

                if (provider->at != NULL) {
                    struct telos_tui_completion_item plugin_item;

                    if (provider->at(provider->context, state->input,
                                     ordinal, &plugin_item)) {
                        copy_text(item->value, sizeof(item->value),
                                  plugin_item.value);
                        copy_text(item->label, sizeof(item->label),
                                  plugin_item.value);
                        copy_text(item->detail, sizeof(item->detail),
                                  plugin_item.detail);
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

static size_t completion_selected(const struct tui_state *state,
                                  size_t count)
{
    if (!session_completion_valid(state)) {
        return command_completion_selected(state, count);
    }
    for (size_t index = 0; index < count; ++index) {
        struct telos_frontend_completion_item item;

        if (!completion_item_at(state, index, &item)) {
            return 0;
        }
        if (strcmp(item.value, state->input) == 0) {
            return index;
        }
    }
    if (state->completion_active && count > 0) {
        return (state->completion_index + count - 1) % count;
    }
    return 0;
}

static bool write_completion_text_line(struct tui_state *state,
                                       const char *text, size_t columns,
                                       bool final)
{
    char safe[TUI_RENDER_LINE_SIZE] = {0};
    char line[TUI_RENDER_LINE_SIZE] = {0};
    size_t size;
    size_t used = 0;

    sanitize_label(safe, sizeof(safe), text);
    size = bytes_for_width(safe, strlen(safe), columns);
    append_bytes(line, sizeof(line), &used, safe, size);
    return write_render_line(state, line, final);
}

static bool write_completion_item(struct tui_state *state,
                                  const struct telos_frontend_completion_item *item,
                                  size_t name_width, size_t columns,
                                  bool selected)
{
    char safe_name[TUI_RENDER_LINE_SIZE] = {0};
    char safe_help[TUI_RENDER_LINE_SIZE] = {0};
    char line[TUI_RENDER_LINE_SIZE] = {0};
    size_t used = 0;
    size_t line_width = 0;
    size_t name_size;

    sanitize_label(safe_name, sizeof(safe_name), item->label);
    sanitize_label(safe_help, sizeof(safe_help), item->detail);
    append_text(line, sizeof(line), &used, selected ? "→ " : "  ");
    line_width = 2;
    name_size = bytes_for_width(safe_name, strlen(safe_name),
                                columns > line_width ? columns - line_width
                                                      : 0);
    append_bytes(line, sizeof(line), &used, safe_name, name_size);
    line_width += visible_width(safe_name, name_size);
    if (line_width < 2 + name_width) {
        append_repeat(line, sizeof(line), &used, " ",
                      2 + name_width - line_width);
        line_width = 2 + name_width;
    }
    if (safe_help[0] != '\0' && line_width + 2 < columns) {
        append_text(line, sizeof(line), &used, "  ");
        line_width += 2;
        append_bytes(line, sizeof(line), &used, safe_help,
                     bytes_for_width(safe_help, strlen(safe_help),
                                     columns - line_width));
    }
    return write_render_line(state, line, false);
}

static bool render_command_completion(struct tui_state *state,
                                      size_t columns, size_t *rows)
{
    size_t count = completion_count(state);
    size_t selected;
    size_t visible;
    size_t offset;
    size_t name_width = 0;
    char line[64];

    *rows = 0;
    if (count == 0) {
        return true;
    }
    selected = completion_selected(state, count);
    visible = count < TUI_COMMAND_COMPLETION_VISIBLE
                  ? count
                  : TUI_COMMAND_COMPLETION_VISIBLE;
    offset = selected >= visible ? selected - visible + 1 : 0;
    if (offset + visible > count) {
        offset = count - visible;
    }
    for (size_t index = 0; index < count; ++index) {
        struct telos_frontend_completion_item item;
        size_t width;

        if (!completion_item_at(state, index, &item)) {
            return false;
        }
        width = visible_width(item.label, strlen(item.label));
        if (width > name_width) {
            name_width = width;
        }
    }
    if (name_width > 24) {
        name_width = 24;
    }
    if (name_width + 6 > columns) {
        name_width = columns > 6 ? columns - 6 : 1;
    }
    for (size_t index = offset; index < offset + visible; ++index) {
        struct telos_frontend_completion_item item;

        if (!completion_item_at(state, index, &item) ||
            !write_completion_item(state, &item, name_width, columns,
                                   index == selected)) {
            return false;
        }
        ++*rows;
    }
    if (snprintf(line, sizeof(line), "(%zu/%zu)", selected + 1, count) >=
        (int)sizeof(line) ||
        !write_completion_text_line(state, line, columns, false)) {
        return false;
    }
    ++*rows;
    return true;
}

static bool write_tool_panel_line(struct tui_state *state,
                                  const char *text, size_t columns,
                                  bool alternate)
{
    char safe[TUI_RENDER_LINE_SIZE] = {0};
    char line[TUI_RENDER_LINE_SIZE] = {0};
    size_t content_width = columns;
    size_t text_size;
    size_t used = 0;

    sanitize_label(safe, sizeof(safe), text);
    text_size = bytes_for_width(safe, strlen(safe), content_width);
    if (state->color) {
        append_text(line, sizeof(line), &used,
                    alternate ? "\033[48;5;238m\033[38;5;252m"
                              : "\033[48;5;235m\033[38;5;250m");
    }
    append_bytes(line, sizeof(line), &used, safe, text_size);
    append_repeat(line, sizeof(line), &used, " ",
                  content_width - visible_width(safe, text_size));
    return write_render_line(state, line, false);
}

static const char *tool_entry_symbol(enum tui_tool_state state)
{
    switch (state) {
    case TUI_TOOL_COMPLETED:
        return "✓";
    case TUI_TOOL_FAILED:
        return "✗";
    default:
        return "◆";
    }
}

static bool render_tool_panel(struct tui_state *state, size_t columns,
                              size_t *rows)
{
    char header[128];
    size_t visible;
    size_t offset;

    *rows = 0;
    if (state->tool_count == 0) {
        return true;
    }
    visible = state->tool_count < TUI_TOOL_VISIBLE
                  ? state->tool_count
                  : TUI_TOOL_VISIBLE;
    offset = state->tool_count - visible;
    if (state->tools_collapsed) {
        if (snprintf(header, sizeof(header),
                     "Tools · %zu · collapsed · Ctrl+O expand",
                     state->tool_count) >= (int)sizeof(header) ||
            !write_tool_panel_line(state, header, columns, false)) {
            return false;
        }
        ++*rows;
        return true;
    }
    if (snprintf(header, sizeof(header),
                 "Tools · %zu · showing %zu-%zu · Ctrl+O collapse",
                 state->tool_count, offset + 1, state->tool_count) >=
            (int)sizeof(header) ||
        !write_tool_panel_line(state, header, columns, false)) {
        return false;
    }
    ++*rows;
    for (size_t index = offset; index < state->tool_count; ++index) {
        const struct tui_tool_entry *entry = &state->tools[index];
        char line[TUI_RENDER_LINE_SIZE];

        if (snprintf(line, sizeof(line), "%s %s%s%s",
                     tool_entry_symbol(entry->state), entry->name,
                     entry->detail[0] == '\0' ? "" : " · ",
                     entry->detail) >= (int)sizeof(line) ||
            !write_tool_panel_line(state, line, columns, (index - offset) % 2 == 1)) {
            return false;
        }
        ++*rows;
    }
    return true;
}

static bool render_footer_content(struct tui_state *state, size_t columns,
                                   char *footer, size_t footer_size)
{
    static const char *const spinners[] = {"⠋", "⠙", "⠹", "⠸",
                                            "⠼", "⠴", "⠦", "⠧"};
    char working[256];
    char model[128];
    char thinking[64];
    char branch[128];
    char context[160];
    char visible[TUI_RENDER_LINE_SIZE] = {0};
    size_t visible_used = 0;
    size_t footer_used = 0;
    size_t size;
    bool has_segment = false;
    const struct telos_frontend_status *status = state->session->status;

    format_working_directory(
        working, sizeof(working), state->session->working_directory,
        status == NULL ? NULL : status->home_directory);
    sanitize_label(model, sizeof(model),
                   state->session->model_get == NULL
                       ? state->session->model
                       : state->session->model_get(
                             state->session->identity_context));
    if (status == NULL || status->fields == 0) {
        char provider[128];

        sanitize_label(provider, sizeof(provider),
                       state->session->provider_get == NULL
                           ? state->session->provider
                           : state->session->provider_get(
                                 state->session->identity_context));
        snprintf(visible, sizeof(visible), "%s · %s/%s · %s", working,
                 provider, model,
                 state->worker_active
                     ? spinners[state->spinner %
                                (sizeof(spinners) / sizeof(spinners[0]))]
                     : "ready");
    } else {
        if ((status->fields & TELOS_FRONTEND_STATUS_MODEL) != 0) {
            append_footer_segment(visible, sizeof(visible), &visible_used,
                                  &has_segment, model);
        }
        if ((status->fields & TELOS_FRONTEND_STATUS_THINKING) != 0 &&
            status->thinking_get != NULL) {
            sanitize_label(thinking, sizeof(thinking),
                           status->thinking_get(status->context));
            if (thinking[0] != '\0') {
                if (has_segment &&
                    (status->fields & TELOS_FRONTEND_STATUS_MODEL) != 0) {
                    append_text(visible, sizeof(visible), &visible_used, " ");
                    append_text(visible, sizeof(visible), &visible_used,
                                thinking);
                } else {
                    append_footer_segment(visible, sizeof(visible),
                                          &visible_used, &has_segment,
                                          thinking);
                }
            }
        }
        if ((status->fields & TELOS_FRONTEND_STATUS_PATH) != 0) {
            append_footer_segment(visible, sizeof(visible), &visible_used,
                                  &has_segment, working);
        }
        if ((status->fields & TELOS_FRONTEND_STATUS_BRANCH) != 0 &&
            status->branch_get != NULL) {
            sanitize_label(branch, sizeof(branch),
                           status->branch_get(status->context));
            append_footer_segment(visible, sizeof(visible), &visible_used,
                                  &has_segment, branch);
        }
        if ((status->fields & TELOS_FRONTEND_STATUS_CONTEXT) != 0 &&
            status->context_used_get != NULL &&
            status->context_window_get != NULL) {
            size_t context_used =
                status->context_used_get(status->context);
            size_t context_window =
                status->context_window_get(status->context);

            if (context_window > 0) {
                size_t used_percent = context_used >= context_window
                                          ? 100
                                          : context_used * 100 /
                                                context_window;
                size_t window_k = (context_window + 999) / 1000;

                if (snprintf(context, sizeof(context),
                             "Context %zu%% used · %zuK window", used_percent,
                             window_k) <
                    (int)sizeof(context)) {
                    append_footer_segment(visible, sizeof(visible),
                                          &visible_used,
                                          &has_segment, context);
                }
            } else if (context_used > 0) {
                /* Model window unknown: still report usage. */
                size_t used_k = (context_used + 999) / 1000;

                if (snprintf(context, sizeof(context), "Context %zuK used",
                             used_k) < (int)sizeof(context)) {
                    append_footer_segment(visible, sizeof(visible),
                                          &visible_used,
                                          &has_segment, context);
                }
            }
        }
        if (state->worker_active) {
            append_footer_segment(
                visible, sizeof(visible), &visible_used, &has_segment,
                spinners[state->spinner %
                         (sizeof(spinners) / sizeof(spinners[0]))]);
        }
        /*
         * Fallback: a status spec whose fields produced no segment
         * (e.g. only "context" before the model window is known)
         * must not leave the footer blank.
         */
        if (visible_used == 0) {
            append_footer_segment(visible, sizeof(visible), &visible_used,
                                  &has_segment, working);
            append_footer_segment(visible, sizeof(visible), &visible_used,
                                  &has_segment, model);
        }
    }
    size = bytes_for_width(visible, strlen(visible), columns);
    if (state->color) {
        append_text(footer, footer_size, &footer_used,
                    "\033[38;5;245m");
    }
    append_bytes(footer, footer_size, &footer_used, visible, size);

    /* Append TUI plugin footer sections. */
    for (size_t i = 0; i < state->tui_plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin =
            state->tui_plugins[i];

        for (size_t j = 0; j < plugin->footer_section_count; ++j) {
            const struct telos_tui_footer_section *section =
                &plugin->footer_sections[j];
            char segment[TELOS_TUI_PLUGIN_FOOTER_RENDER_SIZE];
            size_t segment_size;

            if (section->render == NULL) {
                continue;
            }
            segment_size =
                section->render(section->context, segment, sizeof(segment));
            if (segment_size == 0) {
                continue;
            }
            if (footer_used + 3 + segment_size < footer_size) {
                append_text(footer, footer_size, &footer_used, " · ");
                append_bytes(footer, footer_size, &footer_used, segment,
                             segment_size);
            }
        }
    }

    return true;
}

static bool write_footer(struct tui_state *state, size_t columns,
                         bool final)
{
    char footer[TUI_RENDER_LINE_SIZE] = {0};

    if (!render_footer_content(state, columns, footer, sizeof(footer))) {
        return false;
    }
    return write_render_line(state, footer, final);
}

/*
 * Move the terminal cursor to the editor caret.  Callers must have
 * the cursor at the last rendered row (the footer, written with
 * final=true) or at the caret itself; the routine returns to the
 * caret row and column so the terminal caret always matches the
 * input cursor, even after an in-place footer refresh.
 */
static void position_cursor(struct tui_state *state)
{
    struct editor_metrics metrics;
    size_t columns = terminal_columns(state);
    char sequence[64];
    int size;

    editor_metrics(state, columns > 4 ? columns - 4 : columns, &metrics);
    write_text(state->output_descriptor, "\r");
    if (state->rendered_rows > 1 &&
        state->rendered_rows - 1 > state->rendered_cursor_row) {
        size = snprintf(sequence, sizeof(sequence), "\033[%zuA",
                        state->rendered_rows - 1 -
                            state->rendered_cursor_row);
        write_all(state->output_descriptor, sequence, (size_t)size);
    }
    size = snprintf(sequence, sizeof(sequence), "\033[%zuC",
                    metrics.cursor_column + 2);
    write_all(state->output_descriptor, sequence, (size_t)size);
}

/*
 * Redraw only the footer line in place, used to animate the spinner
 * between frames so the terminal scrollback is not polluted with
 * full-frame redraws.  The cursor is assumed to sit at the editor
 * cursor position after render_dynamic().
 */
static bool refresh_footer(struct tui_state *state)
{
    char footer[TUI_RENDER_LINE_SIZE] = {0};
    char sequence[64];
    size_t columns = terminal_columns(state);
    int size;

    if (state->rendered_rows == 0 ||
        state->rendered_cursor_row >= state->rendered_rows - 1) {
        return true;
    }
    if (!render_footer_content(state, columns, footer, sizeof(footer))) {
        return false;
    }
    size = snprintf(sequence, sizeof(sequence), "\033[%zuB",
                    state->rendered_rows - 1 - state->rendered_cursor_row);
    write_all(state->output_descriptor, sequence, (size_t)size);
    write_text(state->output_descriptor, "\r\033[K");
    write_all(state->output_descriptor, footer, strlen(footer));
    write_text(state->output_descriptor, "\033[0m");
    /*
     * Return to the caret: moving up alone leaves the cursor column
     * at the end of the footer text, which makes the terminal caret
     * drift away from the input caret.
     */
    position_cursor(state);
    return true;
}

static bool render_dynamic(struct tui_state *state)
{
    struct editor_metrics metrics;
    size_t columns = terminal_columns(state);
    size_t content_width = columns - 4;
    size_t first_row;
    size_t last_row;
    /*
     * Count the response line: either the streaming content or the
     * waiting placeholder.  If this row is not accounted for, the
     * frame bookkeeping drifts and clear_dynamic leaves stale rows.
     */
    size_t response_rows =
        (state->stream_size > 0 || state->worker_active) ? 1 : 0;
    size_t thinking_rows =
        (state->thinking_size > 0 && !state->thinking_collapsed) ? 1 : 0;
    size_t tool_rows = 0;
    size_t completion_rows = 0;
    size_t model_selector_rows = 0;
    size_t history_rows = 0;
    size_t history_limit;
    size_t active_rows;
    size_t editor_rows;
    size_t total_rows;
    char response[TUI_RENDER_LINE_SIZE] = {0};
    size_t response_used = 0;

    /* When an overlay is active, render only the overlay. */
    if (state->active_overlay != NULL &&
        state->active_overlay->render != NULL) {
        char buffer[TELOS_TUI_PLUGIN_OVERLAY_RENDER_SIZE];
        size_t overlay_rows_count;
        const char *cursor;
        size_t remaining;

        overlay_rows_count =
            state->active_overlay->render(state->active_overlay_context,
                                          buffer, sizeof(buffer), columns);
        begin_frame(state);
        clear_dynamic(state);
        cursor = buffer;
        remaining = strlen(buffer);
        for (size_t r = 0; r < overlay_rows_count && remaining > 0; ++r) {
            const char *nl = memchr(cursor, '\n', remaining);
            size_t line_size =
                nl == NULL ? remaining : (size_t)(nl - cursor);
            char line[TUI_RENDER_LINE_SIZE];
            size_t copy = line_size < sizeof(line) - 1
                              ? line_size
                              : sizeof(line) - 1;

            memcpy(line, cursor, copy);
            line[copy] = '\0';
            if (!write_render_line(state, line, r + 1 == overlay_rows_count)) {
                end_frame(state);
                return false;
            }
            if (nl == NULL) {
                break;
            }
            remaining -= line_size + 1;
            cursor = nl + 1;
        }
        state->rendered_rows = overlay_rows_count;
        state->rendered_cursor_row = 0;
        end_frame(state);
        return true;
    }

    editor_metrics(state, content_width, &metrics);
    first_row = metrics.cursor_row >= TUI_MAXIMUM_EDITOR_ROWS
                    ? metrics.cursor_row - TUI_MAXIMUM_EDITOR_ROWS + 1
                    : 0;
    if (first_row + TUI_MAXIMUM_EDITOR_ROWS > metrics.total_rows) {
        first_row = metrics.total_rows > TUI_MAXIMUM_EDITOR_ROWS
                        ? metrics.total_rows - TUI_MAXIMUM_EDITOR_ROWS
                        : 0;
    }
    last_row = first_row + TUI_MAXIMUM_EDITOR_ROWS - 1;
    if (last_row >= metrics.total_rows) {
        last_row = metrics.total_rows - 1;
    }
    editor_rows = last_row - first_row + 1;
    if (state->tool_count > 0) {
        tool_rows = state->tools_collapsed
                        ? 1
                        : 1 + (state->tool_count < TUI_TOOL_VISIBLE
                                   ? state->tool_count
                                   : TUI_TOOL_VISIBLE);
    }
    if (state->model_selector_active) {
        size_t count = model_selector_count(state);
        size_t visible = count < TUI_MODEL_SELECTOR_VISIBLE
                             ? count
                             : TUI_MODEL_SELECTOR_VISIBLE;

        model_selector_rows = count == 0 ? 0 : 11 + visible;
    } else {
        size_t count = completion_count(state);
        size_t visible = count < TUI_COMMAND_COMPLETION_VISIBLE
                             ? count
                             : TUI_COMMAND_COMPLETION_VISIBLE;

        completion_rows = count == 0 ? 0 : visible + 1;
    }
    active_rows = thinking_rows + response_rows + tool_rows +
                  model_selector_rows + completion_rows + editor_rows + 3;
    history_limit = terminal_rows(state) > active_rows
                        ? terminal_rows(state) - active_rows
                        : 0;
    if (history_limit > TUI_HISTORY_VISIBLE) {
        history_limit = TUI_HISTORY_VISIBLE;
    }
    begin_frame(state);
    clear_dynamic(state);
    if (!render_history(state, columns, history_limit, &history_rows)) {
        end_frame(state);
        return false;
    }
    if (state->thinking_size > 0 && !state->thinking_collapsed) {
        char line[TUI_RENDER_LINE_SIZE] = {0};
        size_t used = 0;

        if (state->color) {
            append_text(line, sizeof(line), &used, "\033[3;38;5;245m");
        }
        append_text(line, sizeof(line), &used, "Thinking · ");
        if (state->color) {
            append_text(line, sizeof(line), &used, "\033[0m");
        }
        append_bytes(line, sizeof(line), &used, state->thinking_buffer,
                     bytes_for_width(state->thinking_buffer,
                                     state->thinking_size, columns - 12));
        if (!write_render_line(state, line, false)) {
            end_frame(state);
            return false;
        }
    } else if (state->thinking_size > 0 && state->thinking_collapsed) {
        char line[TUI_RENDER_LINE_SIZE] = {0};
        size_t used = 0;

        if (state->color) {
            append_text(line, sizeof(line), &used, "\033[38;5;245m");
        }
        append_text(line, sizeof(line), &used, "••• thinking (");
        {
            char count[32];

            if (snprintf(count, sizeof(count), "%zu chars", 
                         state->thinking_size) < (int)sizeof(count)) {
                append_text(line, sizeof(line), &used, count);
            }
        }
        append_text(line, sizeof(line), &used, ") · Ctrl+T expand");
        if (!write_render_line(state, line, false)) {
            end_frame(state);
            return false;
        }
    }
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
    } else if (state->worker_active) {
        /*
         * Worker is active but no stream content yet.  Show a
         * placeholder so the user knows the agent is working.
         */
        if (state->color) {
            append_text(response, sizeof(response), &response_used,
                        "\033[38;5;245m");
        }
        append_text(response, sizeof(response), &response_used,
                    state->response_active ? "Telos › …" : "Telos › waiting…");
        if (!write_render_line(state, response, false)) {
            end_frame(state);
            return false;
        }
    }
    if (!render_tool_panel(state, columns, &tool_rows)) {
        end_frame(state);
        return false;
    }
    if (state->model_selector_active) {
        if (!render_model_selector_rows(state, columns,
                                        &model_selector_rows)) {
            end_frame(state);
            return false;
        }
    } else if (!render_command_completion(state, columns, &completion_rows)) {
            end_frame(state);
            return false;
    }

    /* Render TUI plugin panels positioned above the editor. */
    for (size_t i = 0; i < state->tui_plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin =
            state->tui_plugins[i];

        for (size_t j = 0; j < plugin->panel_count; ++j) {
            const struct telos_tui_panel *panel = &plugin->panels[j];

            if (panel->position != TELOS_TUI_PANEL_ABOVE_EDITOR ||
                panel->render == NULL) {
                continue;
            }
            {
                char buffer[TELOS_TUI_PLUGIN_PANEL_RENDER_SIZE];
                size_t panel_rows =
                    panel->render(panel->context, buffer, sizeof(buffer),
                                  columns);
                const char *cursor = buffer;
                size_t remaining = strlen(buffer);

                for (size_t r = 0; r < panel_rows && remaining > 0; ++r) {
                    const char *nl = memchr(cursor, '\n', remaining);
                    size_t line_size =
                        nl == NULL ? remaining : (size_t)(nl - cursor);
                    char line[TUI_RENDER_LINE_SIZE];
                    size_t copy = line_size < sizeof(line) - 1
                                      ? line_size
                                      : sizeof(line) - 1;

                    memcpy(line, cursor, copy);
                    line[copy] = '\0';
                    if (!write_completion_text_line(state, line, columns,
                                                    false)) {
                        end_frame(state);
                        return false;
                    }
                    if (nl == NULL) {
                        break;
                    }
                    remaining -= line_size + 1;
                    cursor = nl + 1;
                }
            }
        }
    }

    if (!write_border(state, columns, true) ||
        !render_editor_rows(state, content_width, first_row, last_row) ||
        !write_border(state, columns, false) ||
        !write_footer(state, columns, true)) {
        end_frame(state);
        return false;
    }
    total_rows = history_rows + thinking_rows + response_rows + tool_rows +
                 model_selector_rows + completion_rows + editor_rows + 3;

    state->rendered_rows = total_rows;
    state->rendered_cursor_row = history_rows + thinking_rows +
                                 response_rows + tool_rows +
                                 model_selector_rows + completion_rows + 1 +
                                 (metrics.cursor_row - first_row);
    position_cursor(state);
    end_frame(state);
    return true;
}

static const char *model_selector_provider(const struct tui_state *state)
{
    const char *provider = state->session->provider_get == NULL
                               ? state->session->provider
                               : state->session->provider_get(
                                     state->session->identity_context);

    if (provider == NULL || provider[0] == '\0' ||
        strcmp(provider, "unconfigured") == 0) {
        return NULL;
    }
    return provider;
}

static bool model_selector_matches(const struct tui_state *state,
                                   const struct telos_model_descriptor *model)
{
    const char *provider = model_selector_provider(state);

    return provider == NULL || strcmp(model->provider, provider) == 0;
}

static size_t model_selector_count(const struct tui_state *state)
{
    const struct telos_model_catalog *catalog = state->session->model_catalog;
    size_t count = 0;

    for (size_t index = 0; catalog != NULL && index < catalog->count; ++index) {
        if (model_selector_matches(state, &catalog->models[index])) {
            ++count;
        }
    }
    return count;
}

static const struct telos_model_descriptor *
model_selector_at(const struct tui_state *state, size_t ordinal)
{
    const struct telos_model_catalog *catalog = state->session->model_catalog;

    for (size_t index = 0; catalog != NULL && index < catalog->count; ++index) {
        if (model_selector_matches(state, &catalog->models[index])) {
            if (ordinal == 0) {
                return &catalog->models[index];
            }
            --ordinal;
        }
    }
    return NULL;
}

static size_t model_selector_index_for(const struct tui_state *state,
                                       const struct telos_model_descriptor *target)
{
    const struct telos_model_catalog *catalog = state->session->model_catalog;
    size_t ordinal = 0;

    for (size_t index = 0; catalog != NULL && index < catalog->count; ++index) {
        const struct telos_model_descriptor *model = &catalog->models[index];

        if (!model_selector_matches(state, model)) {
            continue;
        }
        if (model == target) {
            return ordinal;
        }
        ++ordinal;
    }
    return 0;
}

static void model_selector_adjust_offset(struct tui_state *state)
{
    size_t count = model_selector_count(state);
    size_t visible = count < TUI_MODEL_SELECTOR_VISIBLE
                         ? count
                         : TUI_MODEL_SELECTOR_VISIBLE;

    if (visible == 0) {
        state->model_selector_offset = 0;
        return;
    }
    if (state->model_selector_index < state->model_selector_offset) {
        state->model_selector_offset = state->model_selector_index;
    }
    if (state->model_selector_index >=
        state->model_selector_offset + visible) {
        state->model_selector_offset =
            state->model_selector_index - visible + 1;
    }
    if (state->model_selector_offset + visible > count) {
        state->model_selector_offset = count - visible;
    }
}

static bool render_model_selector_rows(struct tui_state *state,
                                       size_t columns, size_t *rows)
{
    const struct telos_model_catalog *catalog = state->session->model_catalog;
    const struct telos_model_descriptor *current;
    const struct telos_model_descriptor *selected;
    size_t visible;
    size_t end;
    char line[TUI_RENDER_LINE_SIZE];

    if (catalog == NULL || catalog->count == 0) {
        return false;
    }
    size_t count = model_selector_count(state);

    if (count == 0) {
        return false;
    }
    *rows = 0;
    if (state->model_selector_index >= count) {
        state->model_selector_index = 0;
    }
    model_selector_adjust_offset(state);
    visible = count < TUI_MODEL_SELECTOR_VISIBLE
                  ? count
                  : TUI_MODEL_SELECTOR_VISIBLE;
    end = state->model_selector_offset + visible;
    current = telos_model_catalog_current(catalog);
    selected = model_selector_at(state, state->model_selector_index);

    if (!write_border_line(state, columns, true, false)) {
        return false;
    }
    ++*rows;
    if (!write_selector_line(state, "Models", columns, false) ||
        !write_selector_line(state, "Models available to this session",
                             columns, false) ||
        !write_selector_line(state, "", columns, false)) {
        return false;
    }
    *rows += 3;
    for (size_t index = state->model_selector_offset; index < end; ++index) {
        const struct telos_model_descriptor *model = model_selector_at(state,
                                                                        index);

        if (snprintf(line, sizeof(line), "%s %s [%s]%s",
                     index == state->model_selector_index ? "→" : " ",
                     model->id,
                     model->provider,
                     current == model ? " ✓" : "") >= (int)sizeof(line)) {
            return false;
        }
        if (!write_selector_line(state, line, columns, false)) {
            return false;
        }
        ++*rows;
    }
    if (!write_selector_line(state, "", columns, false)) {
        return false;
    }
    ++*rows;
    if (snprintf(line, sizeof(line), "Model Name: %s",
                 selected->name == NULL ? selected->id : selected->name) >=
        (int)sizeof(line)) {
        return false;
    }
    if (!write_selector_line(state, line, columns, false)) {
        return false;
    }
    ++*rows;
    if (snprintf(line, sizeof(line), "Model ID: %s/%s", selected->provider,
                 selected->id) >= (int)sizeof(line)) {
        return false;
    }
    if (!write_selector_line(state, line, columns, false)) {
        return false;
    }
    ++*rows;
    if (snprintf(line, sizeof(line), "Reasoning: %s%s%s",
                 selected->reasoning == NULL ? "off" : selected->reasoning,
                 (selected->capabilities & TELOS_MODEL_CAPABILITY_TOOLS) != 0
                     ? " · tools"
                     : "",
                 (selected->capabilities & TELOS_MODEL_CAPABILITY_VISION) != 0
                     ? " · vision"
                     : "") >= (int)sizeof(line)) {
        return false;
    }
    if (!write_selector_line(state, line, columns, false) ||
        !write_selector_line(state,
                             "↑/↓ navigate · Enter select · Esc close",
                             columns, false) ||
        !write_selector_line(state, "", columns, false) ||
        !write_border_line(state, columns, false, false)) {
        return false;
    }
    *rows += 4;
    return true;
}

static void write_sanitized(struct tui_state *state, const char *text,
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

static void write_sanitized_range(struct tui_state *state,
                                  const char *text, size_t start, size_t end)
{
    if (end > start) {
        write_sanitized(state, text + start, end - start);
    }
}

static size_t markdown_find_closing(const char *text, size_t size,
                                    size_t start, const char *marker,
                                    size_t marker_size)
{
    for (size_t index = start; index + marker_size <= size; ++index) {
        if (memcmp(text + index, marker, marker_size) == 0) {
            return index;
        }
    }
    return size;
}

static void write_markdown_span(struct tui_state *state, const char *text,
                                size_t start, size_t end, const char *style)
{
    if (state->color && style != NULL) {
        write_text(state->output_descriptor, style);
    }
    write_sanitized_range(state, text, start, end);
    if (state->color && style != NULL) {
        write_text(state->output_descriptor, "\033[0m");
    }
}

static void write_markdown_inline(struct tui_state *state,
                                  const char *text, size_t size)
{
    static const char code_marker[] = {96, '\0'};
    size_t index = 0;
    size_t plain_start = 0;

    while (index < size) {
        const char *marker = NULL;
        size_t marker_size = 0;
        size_t closing;
        size_t content_start;
        const char *style;

        if ((unsigned char)text[index] == 96U) {
            marker = code_marker;
            marker_size = 1;
            style = "\033[48;5;236m\033[38;5;222m";
        } else if (index + 1 < size &&
                   (memcmp(text + index, "**", 2) == 0 ||
                    memcmp(text + index, "__", 2) == 0)) {
            marker = text + index;
            marker_size = 2;
            style = "\033[1m";
        } else if (text[index] == '*' || text[index] == '_') {
            marker = text + index;
            marker_size = 1;
            style = "\033[3m";
        } else if (text[index] == '[') {
            size_t label_end = index + 1;
            size_t link_start;
            size_t link_end;

            while (label_end < size && text[label_end] != ']') {
                ++label_end;
            }
            if (label_end < size && label_end + 1 < size &&
                text[label_end + 1] == '(') {
                link_start = label_end + 2;
                link_end = markdown_find_closing(text, size, link_start, ")",
                                                  1);
                if (link_end < size) {
                    write_sanitized_range(state, text, plain_start, index);
                    write_markdown_span(state, text, index + 1, label_end,
                                        "\033[4;38;5;81m");
                    index = link_end + 1;
                    plain_start = index;
                    continue;
                }
            }
            ++index;
            continue;
        } else {
            ++index;
            continue;
        }
        content_start = index + marker_size;
        closing = markdown_find_closing(text, size, content_start, marker,
                                        marker_size);
        if (closing == size || closing == content_start) {
            ++index;
            continue;
        }
        write_sanitized_range(state, text, plain_start, index);
        write_markdown_span(state, text, content_start, closing, style);
        index = closing + marker_size;
        plain_start = index;
    }
    write_sanitized_range(state, text, plain_start, size);
}

static bool markdown_fence(const char *text, size_t size, size_t *start)
{
    size_t offset = 0;

    while (offset < size && offset < 3 && text[offset] == ' ') {
        ++offset;
    }
    if (offset + 3 > size || text[offset] != 96 || text[offset + 1] != 96 ||
        text[offset + 2] != 96) {
        return false;
    }
    *start = offset;
    return true;
}

static void write_markdown_segment(struct tui_state *state,
                                   const char *text, size_t size)
{
    size_t fence_start = 0;
    size_t prefix = 0;

    if (markdown_fence(text, size, &fence_start)) {
        if (state->markdown_code_block) {
            write_markdown_span(state, text, fence_start, size,
                                "\033[38;5;245m");
            state->markdown_code_block = false;
        } else {
            write_markdown_span(state, text, fence_start, size,
                                "\033[38;5;75m");
            state->markdown_code_block = true;
        }
        return;
    }
    if (state->markdown_code_block) {
        write_markdown_span(state, text, 0, size,
                            "\033[48;5;236m\033[38;5;222m");
        return;
    }
    while (prefix < size && text[prefix] == ' ' && prefix < 3) {
        ++prefix;
    }
    if (prefix + 2 <= size && text[prefix] == '#') {
        size_t hashes = 1;

        while (prefix + hashes < size && hashes < 6 &&
               text[prefix + hashes] == '#') {
            ++hashes;
        }
        if (prefix + hashes < size && text[prefix + hashes] == ' ') {
            write_sanitized_range(state, text, 0, prefix);
            write_markdown_span(state, text, prefix + hashes + 1, size,
                                "\033[1;38;5;75m");
            return;
        }
    }
    if (prefix + 2 <= size &&
        (text[prefix] == '-' || text[prefix] == '*' ||
         text[prefix] == '+') &&
        text[prefix + 1] == ' ') {
        static const char bullet[] = "• ";

        write_sanitized_range(state, text, 0, prefix);
        write_markdown_span(state, bullet, 0, strlen(bullet),
                            "\033[38;5;75m");
        write_markdown_inline(state, text + prefix + 2, size - prefix - 2);
        return;
    }
    if (prefix < size && text[prefix] >= '1' && text[prefix] <= '9') {
        size_t number_end = prefix;

        while (number_end < size && text[number_end] >= '0' &&
               text[number_end] <= '9') {
            ++number_end;
        }
        if (number_end + 1 < size && text[number_end] == '.' &&
            text[number_end + 1] == ' ') {
            write_sanitized_range(state, text, 0, prefix);
            write_markdown_span(state, text + prefix, 0,
                                number_end + 2 - prefix, "\033[38;5;75m");
            write_markdown_inline(state, text + number_end + 2,
                                  size - number_end - 2);
            return;
        }
    }
    if (prefix + 2 <= size && text[prefix] == '>' &&
        text[prefix + 1] == ' ') {
        write_sanitized_range(state, text, 0, prefix);
        write_markdown_span(state, text + prefix, 0, 2,
                            "\033[38;5;245m");
        write_markdown_inline(state, text + prefix + 2, size - prefix - 2);
        return;
    }
    write_markdown_inline(state, text, size);
}

static bool write_history_entry(struct tui_state *state,
                                const struct tui_history_entry *entry,
                                size_t columns)
{    size_t prefix = history_prefix_width(entry->kind);
    size_t width = columns > prefix ? columns - prefix : 1;
    size_t size = bytes_for_width(entry->text, strlen(entry->text), width);

    if (entry->kind == TUI_HISTORY_USER) {
        if (state->color) {
            write_text(state->output_descriptor, "\033[38;5;110m");
        }
        write_text(state->output_descriptor,
                   entry->continuation ? "        " : "You   › ");
        if (state->color) {
            write_text(state->output_descriptor, "\033[0m");
        }
        write_sanitized(state, entry->text, size);
    } else if (entry->kind == TUI_HISTORY_RESPONSE) {
        if (state->color) {
            write_text(state->output_descriptor, "\033[38;5;75m");
        }
        write_text(state->output_descriptor,
                   entry->continuation ? "        " : "Telos › ");
        if (state->color) {
            write_text(state->output_descriptor, "\033[0m");
        }
        write_markdown_segment(state, entry->text, size);
    } else if (entry->kind == TUI_HISTORY_STATUS) {
        if (state->color) {
            write_text(state->output_descriptor, "\033[38;5;245m");
        }
        write_text(state->output_descriptor, entry->symbol[0] == '\0'
                                                   ? "•"
                                                   : entry->symbol);
        write_text(state->output_descriptor, " ");
        write_sanitized(state, entry->text, size);
    } else {
        write_sanitized(state, entry->text, size);
    }
    return write_text(state->output_descriptor, "\033[0m\033[K\r\n");
}

static bool render_history(struct tui_state *state, size_t columns,
                           size_t maximum_rows, size_t *rows)
{
    size_t visible;    size_t maximum_scroll;
    size_t end;
    size_t start;

    *rows = 0;
    if (state->history_count == 0 || maximum_rows == 0) {
        state->history_scroll = 0;
        return true;
    }
    visible = state->history_count < maximum_rows ? state->history_count
                                                   : maximum_rows;
    maximum_scroll = state->history_count - visible;
    if (state->history_scroll > maximum_scroll) {
        state->history_scroll = maximum_scroll;
    }
    end = state->history_count - state->history_scroll;
    start = end - visible;
    state->markdown_code_block = false;

    /* Scroll indicator above history. */
    if (state->history_scroll > 0) {
        char indicator[64];

        if (snprintf(indicator, sizeof(indicator),
                     "↑ %zu more · PgUp/Down scroll", state->history_scroll) <
            (int)sizeof(indicator)) {
            if (state->color) {
                write_text(state->output_descriptor, "\033[38;5;245m");
            }
            write_text(state->output_descriptor, indicator);
            write_text(state->output_descriptor, "\033[0m\033[K\r\n");
            ++*rows;
        }
    }

    for (size_t index = start; index < end; ++index) {
        if (!write_history_entry(state, history_at(state, index), columns)) {
            return false;
        }
        ++*rows;
    }
    return true;
}

static void write_response_segment(struct tui_state *state,
                                   const char *text, size_t size)
{
    history_append_line(state, TUI_HISTORY_RESPONSE, NULL, text, size,
                        !state->response_first_line);
    state->response_first_line = false;
}

static void flush_stream(struct tui_state *state, bool force)
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

static void stream_text(struct tui_state *state, const char *text)
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
        if (state->stream_size + 1 >= sizeof(state->stream)) {
            flush_stream(state, true);
        }
        if (state->stream_size + 1 >= sizeof(state->stream)) {
            continue;
        }
        state->stream[state->stream_size++] = value == '\t' ? ' ' : (char)value;
    }
    state->stream[state->stream_size] = '\0';
    flush_stream(state, false);
}

static void write_status_line_segment(struct tui_state *state,
                                      const char *symbol,
                                      const char *text, size_t size,
                                      const char *color)
{
    (void)color;
    history_append_line(state, TUI_HISTORY_STATUS, symbol, text, size,
                        false);
}

static void write_status_line(struct tui_state *state, const char *symbol,
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

static void write_clipboard(struct tui_state *state)
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

static bool queue_push(struct tui_state *state,
                       const struct queued_event *event)
{
    bool accepted = true;

    pthread_mutex_lock(&state->queue_mutex);
    if (state->queue_shutdown) {
        accepted = false;
    } else if (state->event_count == TUI_EVENT_CAPACITY) {
        /*
         * Queue is full — drop the oldest event to make room rather
         * than blocking the producer.  This is safe because the
         * events are lossy by nature (rendering snapshots); the
         * consumer always drains the queue completely on each frame.
         */
        state->event_head =
            (state->event_head + 1) % TUI_EVENT_CAPACITY;
        state->event_count -= 1;
    }
    if (accepted) {
        size_t tail =
            (state->event_head + state->event_count) % TUI_EVENT_CAPACITY;

        state->events[tail] = *event;
        state->event_count += 1;
        pthread_cond_broadcast(&state->queue_changed);
    }
    pthread_mutex_unlock(&state->queue_mutex);
    return accepted;
}

static bool queue_pop(struct tui_state *state, struct queued_event *event)
{
    bool result = false;

    pthread_mutex_lock(&state->queue_mutex);
    if (state->event_count > 0) {
        *event = state->events[state->event_head];
        state->event_head =
            (state->event_head + 1) % TUI_EVENT_CAPACITY;
        state->event_count -= 1;
        pthread_cond_broadcast(&state->queue_changed);
        result = true;
    }
    pthread_mutex_unlock(&state->queue_mutex);
    return result;
}

static bool queue_steer_prompt(struct tui_state *state,
                               const char *prompt)
{
    bool result = false;

    pthread_mutex_lock(&state->queue_mutex);
    if (state->steer_count < TUI_STEER_CAPACITY) {
        size_t tail = (state->steer_head + state->steer_count) %
                      TUI_STEER_CAPACITY;

        copy_text(state->steer_prompts[tail],
                  sizeof(state->steer_prompts[tail]), prompt);
        state->steer_count += 1;
        result = true;
    }
    pthread_mutex_unlock(&state->queue_mutex);
    return result;
}

static bool pop_steer_prompt(struct tui_state *state, char *prompt,
                             size_t prompt_size)
{
    bool result = false;

    pthread_mutex_lock(&state->queue_mutex);
    if (state->steer_count > 0) {
        copy_text(prompt, prompt_size, state->steer_prompts[state->steer_head]);
        state->steer_prompts[state->steer_head][0] = '\0';
        state->steer_head = (state->steer_head + 1) % TUI_STEER_CAPACITY;
        state->steer_count -= 1;
        result = true;
    }
    pthread_mutex_unlock(&state->queue_mutex);
    return result;
}

static char *next_steer_prompt(void *context, struct telos_error **error)
{
    struct tui_state *state = context;
    char prompt[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    char *result;

    if (state == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Terminal steer context is invalid");
        return NULL;
    }
    if (!pop_steer_prompt(state, prompt, sizeof(prompt))) {
        return NULL;
    }
    result = malloc(strlen(prompt) + 1);
    if (result == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Terminal steer prompt allocation failed");
        return NULL;
    }
    memcpy(result, prompt, strlen(prompt) + 1);
    return result;
}

static bool queue_frontend_event(const struct telos_frontend_event *event,
                                 void *context,
                                 struct telos_error **error)
{
    struct tui_state *state = context;
    const char *text;
    size_t size;

    if (event == NULL || state == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
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
            telos_error_set(error, TELOS_ERROR_DOMAIN_CANCELLED, ECANCELED,
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
    struct tui_state *state = context;
    struct telos_error *error = NULL;
    const struct telos_frontend_steer steer = {
        .next = next_steer_prompt,
        .context = state,
    };
    bool result;

    if (state->worker_command) {
        bool handled = false;
        bool exit_requested = false;

        state->worker_command = false;
        result = telos_command_registry_dispatch(
            state->session->commands, state->worker_command_line,
            state->cancel, queue_frontend_event, state, &handled,
            &exit_requested, &error);
        if (exit_requested) {
            state->exit_requested = true;
            queue_push(state, &(const struct queued_event){
                .kind = QUEUED_TURN_COMPLETED,
            });
            telos_error_release(error);
            return NULL;
        }
    } else {
        result = state->session->turn(
            state->active_prompt, state->cancel, queue_frontend_event, state,
            &steer, state->session->turn_context, &error);
    }

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

static void write_user_prompt(struct tui_state *state, const char *prompt)
{
    size_t newlines = 0;    /*
     * Suppress consecutive duplicate submissions while a turn is
     * active.  The same prompt queued multiple times before the
     * first response would flood history.
     */
    if (state->worker_active && state->active_prompt[0] != '\0' &&
        strcmp(state->active_prompt, prompt) == 0) {
        return;
    }

    for (const char *p = prompt; *p != '\0'; ++p) {
        if (*p == '\n') {
            ++newlines;
        }
    }
    if (newlines > 4) {
        /*
         * Long pasted text: show first line plus a summary instead
         * of expanding the entire paste into the history.
         */
        const char *nl = strchr(prompt, '\n');
        size_t first_line = nl == NULL ? strlen(prompt)
                                        : (size_t)(nl - prompt);
        char summary[256];

        history_append_line(state, TUI_HISTORY_USER, NULL, prompt,
                            first_line, false);
        if (snprintf(summary, sizeof(summary),
                     "[%zu lines pasted]", newlines + 1) <
            (int)sizeof(summary)) {
            history_append_text(state, TUI_HISTORY_STATUS, "•", summary);
        }
    } else {
        history_append_text(state, TUI_HISTORY_USER, NULL, prompt);
    }
    history_append_line(state, TUI_HISTORY_PLAIN, NULL, "", 0, false);
}

static void archive_tool_panel(struct tui_state *state)
{
    char line[128];

    if (state->tool_count == 0 ||
        snprintf(line, sizeof(line),
                 "Tools · %zu · collapsed · Ctrl+O expand",
                 state->tool_count) >= (int)sizeof(line)) {
        return;
    }
    history_append_text(state, TUI_HISTORY_PLAIN, NULL, line);
    state->tool_count = 0;
    state->tools_collapsed = false;
}

static void show_help(struct tui_state *state)
{
    char line[TUI_RENDER_LINE_SIZE];

    history_append_text(state, TUI_HISTORY_PLAIN, NULL,
                        "Telos commands\n"
                        "  /help   show this help\n"
                        "  /clear  clear the Agent conversation\n"
                        "  /quit   leave Telos");
    if (state->session->commands != NULL) {
        for (size_t index = 0;
             index < state->session->commands->count; ++index) {
            const struct telos_command *command =
                &state->session->commands->commands[index];

            if (snprintf(line, sizeof(line), "  /%s%s%s", command->name,
                         command->help == NULL || command->help[0] == '\0'
                             ? ""
                             : "  ",
                         command->help == NULL ? "" : command->help) <
                (int)sizeof(line)) {
                history_append_text(state, TUI_HISTORY_PLAIN, NULL,
                                    line);
            }
        }
    }
    if (state->session->command_help != NULL &&
        state->session->command_help[0] != '\0') {
        if (snprintf(line, sizeof(line), "  %s", state->session->command_help) <
            (int)sizeof(line)) {
            history_append_text(state, TUI_HISTORY_PLAIN, NULL, line);
        }
    }
    history_append_text(
        state, TUI_HISTORY_PLAIN, NULL,
        "\nEnter submits · Ctrl+J or Alt+Enter adds a line · "
        "Esc cancels · Ctrl+G opens $EDITOR · !command runs a shell command · "
        "!!command sends its output\n");
}

static bool show_shell_result(struct tui_state *state,
                              const char *command,
                              struct telos_error **error)
{
    char line[TUI_RENDER_LINE_SIZE];
    int status;

    if (!run_shell_command(state->session->working_directory, command,
                           state->shell_output, sizeof(state->shell_output),
                           &status, error)) {
        return false;
    }
    if (snprintf(line, sizeof(line), "\n! %s", command) < (int)sizeof(line)) {
        history_append_text(state, TUI_HISTORY_PLAIN, NULL, line);
    }
    history_append_text(state, TUI_HISTORY_PLAIN, NULL,
                        state->shell_output);
    if (state->shell_output[0] != '\0' &&
        state->shell_output[strlen(state->shell_output) - 1] != '\n') {
        history_append_line(state, TUI_HISTORY_PLAIN, NULL, "", 0,
                            false);
    }
    if (status != 0) {
        if (snprintf(line, sizeof(line), "[shell exited with %d]", status) <
            (int)sizeof(line)) {
            history_append_text(state, TUI_HISTORY_PLAIN, NULL, line);
        }
    }
    return true;
}

static bool open_model_selector(struct tui_state *state,
                                struct telos_error **error)
{
    /*
     * Prefer a plugin-provided "model-selector" overlay.
     */
    for (size_t i = 0; i < state->tui_plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin =
            state->tui_plugins[i];

        for (size_t j = 0; j < plugin->overlay_count; ++j) {
            const struct telos_tui_overlay *overlay = &plugin->overlays[j];

            if (strcmp(overlay->id, "model-selector") == 0) {
                const struct telos_model_catalog *catalog =
                    state->session->model_catalog;
                const char *provider;
                size_t n = 0;

                if (state->session->provider_get != NULL) {
                    provider = state->session->provider_get(
                        state->session->identity_context);
                } else {
                    provider = state->session->provider;
                }
                for (size_t k = 0; catalog != NULL && k < catalog->count;
                     ++k) {
                    if (provider == NULL ||
                        strcmp(catalog->models[k].provider, provider) == 0) {
                        ++n;
                    }
                }
                if (catalog == NULL || n == 0) {
                    return queue_frontend_event(
                        &(const struct telos_frontend_event){
                            .kind = TELOS_FRONTEND_NOTICE,
                            .text = "No models are configured",
                        },
                        state, error);
                }
                state->active_overlay = overlay;
                state->active_overlay_context = overlay->context;
                state->overlay_columns = terminal_columns(state);
                state->overlay_rows = terminal_rows(state);
                state->input[0] = '\0';
                state->input_size = 0;
                state->input_cursor = 0;
                state->completion_active = false;
                return true;
            }
        }
    }

    /* Fall back to built-in model selector. */
    {
        const struct telos_model_catalog *catalog =
            state->session->model_catalog;
        const struct telos_model_descriptor *current;
        size_t count;

        count = model_selector_count(state);
        if (catalog == NULL || count == 0) {
            return queue_frontend_event(
                &(const struct telos_frontend_event){
                    .kind = TELOS_FRONTEND_NOTICE,
                    .text = "No models are configured",
                },
                state, error);
        }
        current = telos_model_catalog_current(catalog);
        state->model_selector_index = current == NULL
                                          ? 0
                                          : model_selector_index_for(state, current);
        state->model_selector_offset = 0;
        state->model_selector_active = true;
        state->input[0] = '\0';
        state->input_size = 0;
        state->input_cursor = 0;
        state->completion_active = false;
        return true;
    }
}

static bool start_turn(struct tui_state *state, const char *prompt,
                       struct telos_error **error)
{
    struct telos_error *command_error = NULL;
    bool handled = false;
    bool exit_requested = false;
    int result;
    char processed[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    size_t processed_size;
    size_t processed_cursor;

    /* Run TUI plugin input preprocessors. */
    processed_size = copy_text(processed, sizeof(processed), prompt);
    processed_cursor = processed_size;
    for (size_t i = 0; i < state->tui_plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin =
            state->tui_plugins[i];

        for (size_t j = 0; j < plugin->input_preprocessor_count; ++j) {
            const struct telos_tui_input_preprocessor *preprocessor =
                &plugin->input_preprocessors[j];

            if (preprocessor->preprocess == NULL) {
                continue;
            }
            if (!preprocessor->preprocess(preprocessor->context, processed,
                                          &processed_size,
                                          &processed_cursor)) {
                return true;
            }
        }
    }
    prompt = processed;

    /* Try TUI plugin command interceptors before built-in handling. */
    {
        struct telos_tui_host host = { .state = state };

        for (size_t i = 0; i < state->tui_plugin_count; ++i) {
            const struct telos_tui_plugin_definition_v1 *plugin =
                state->tui_plugins[i];

            for (size_t j = 0; j < plugin->command_interceptor_count; ++j) {
                const struct telos_tui_command_interceptor *interceptor =
                    &plugin->command_interceptors[j];
                size_t prefix_len;

                if (interceptor->prefix == NULL ||
                    interceptor->handle == NULL) {
                    continue;
                }
                prefix_len = strlen(interceptor->prefix);
                if (prefix_len == 0 ||
                    strncmp(prompt, interceptor->prefix, prefix_len) != 0) {
                    continue;
                }
                if (interceptor->handle(interceptor->context, prompt, &host,
                                        error)) {
                    return true;
                }
            }
        }
    }

    if (strncmp(prompt, "login", sizeof("login") - 1) == 0 &&
        (prompt[sizeof("login") - 1] == '\0' ||
         prompt[sizeof("login") - 1] == ' ')) {
        char command[sizeof(state->input)];

        if (snprintf(command, sizeof(command), "/%s", prompt) >=
            (int)sizeof(command)) {
            telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
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
            telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
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
    if (strcmp(prompt, "/model") == 0 && !state->worker_active &&
        state->session->model_catalog != NULL) {
        return open_model_selector(state, error);
    }
    if (state->session->commands != NULL && prompt[0] == '/') {
        const char *slash = prompt + 1;
        const bool blocking_command =
            (strncmp(slash, "login", sizeof("login") - 1) == 0 &&
             (slash[sizeof("login") - 1] == '\0' ||
              slash[sizeof("login") - 1] == ' ')) ||
            (strncmp(slash, "logout", sizeof("logout") - 1) == 0 &&
             (slash[sizeof("logout") - 1] == '\0' ||
              slash[sizeof("logout") - 1] == ' ')) ||
            (strncmp(slash, "login-status", sizeof("login-status") - 1) ==
                 0 &&
             (slash[sizeof("login-status") - 1] == '\0' ||
              slash[sizeof("login-status") - 1] == ' '));

        if (blocking_command) {
            copy_text(state->worker_command_line,
                      sizeof(state->worker_command_line), prompt);
            state->worker_command = true;
        } else if (!telos_command_registry_dispatch(
                       state->session->commands, prompt, NULL,
                       queue_frontend_event, state, &handled,
                       &exit_requested, &command_error)) {
            if (command_error != NULL &&
                telos_error_code(command_error) == ENOENT) {
                char message[TELOS_COMMAND_ARGUMENT_SIZE];

                if (snprintf(message, sizeof(message),
                             "Unknown command: %s", prompt) >=
                    (int)sizeof(message)) {
                    telos_error_release(command_error);
                    telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
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
        if (handled && !blocking_command) {
            state->exit_requested = state->exit_requested || exit_requested;
            return true;
        }
    }
    if (state->worker_active) {
        if (!queue_steer_prompt(state, prompt)) {
            write_all(state->output_descriptor, "\a", 1);
        }
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

    archive_tool_panel(state);
    write_user_prompt(state, prompt);
    copy_text(state->active_prompt, sizeof(state->active_prompt), prompt);
    copy_text(state->prior_prompt, sizeof(state->prior_prompt), prompt);
    state->response_active = false;
    state->response_first_line = true;
    state->markdown_code_block = false;
    state->stream_size = 0;
    state->tool_count = 0;
    state->turn_phase = TUI_PHASE_WAITING;
    state->turn_start_ms = monotonic_milliseconds();
    state->thinking_start_ms = 0;
    state->response_start_ms = 0;
    state->tools_start_ms = 0;
    state->tools_total_ms = 0;
    state->turn_summary_written = false;
    state->thinking_size = 0;
    state->thinking_buffer[0] = '\0';
    state->thinking_collapsed = false;
    state->tools_collapsed = false;
    state->cancel = telos_cancel_create();
    if (state->cancel == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Terminal turn cancellation allocation failed");
        return false;
    }
    result = pthread_create(&state->worker, NULL, run_turn, state);
    if (result != 0) {
        telos_cancel_release(state->cancel);
        state->cancel = NULL;
        telos_error_set(error, TELOS_ERROR_DOMAIN_STATE, result,
                  "Terminal Agent worker could not be started");
        return false;
    }
    state->worker_active = true;
    return true;
}

static void record_tool_event(struct tui_state *state,
                              enum tui_tool_state event_state,
                              const char *name, const char *detail)
{
    struct tui_tool_entry *entry = NULL;
    const char *safe_name = name == NULL || name[0] == '\0' ? "tool" : name;

    if (event_state == TUI_TOOL_RUNNING) {
        if (state->tool_count == TUI_TOOL_CAPACITY) {
            memmove(state->tools, state->tools + 1,
                    (TUI_TOOL_CAPACITY - 1) * sizeof(state->tools[0]));
            state->tool_count -= 1;
        }
        entry = &state->tools[state->tool_count++];
    } else {
        for (size_t index = state->tool_count; index > 0; --index) {
            struct tui_tool_entry *candidate =
                &state->tools[index - 1];

            if (candidate->state == TUI_TOOL_RUNNING &&
                strcmp(candidate->name, safe_name) == 0) {
                entry = candidate;
                break;
            }
        }
        if (entry == NULL) {
            if (state->tool_count == TUI_TOOL_CAPACITY) {
                memmove(state->tools, state->tools + 1,
                        (TUI_TOOL_CAPACITY - 1) *
                            sizeof(state->tools[0]));
                state->tool_count -= 1;
            }
            entry = &state->tools[state->tool_count++];
        }
    }
    entry->state = event_state;
    copy_text(entry->name, sizeof(entry->name), safe_name);
    copy_text(entry->detail, sizeof(entry->detail), detail);
}

static void handle_frontend_event(struct tui_state *state,
                                  const struct queued_event *event)
{
    /* Notify TUI plugin event hooks first. */
    if (event->kind == QUEUED_FRONTEND_EVENT) {
        struct telos_frontend_event fe = {
            .kind = event->frontend_kind,
            .text = event->text,
            .name = event->name,
        };

        for (size_t i = 0; i < state->tui_plugin_count; ++i) {
            const struct telos_tui_plugin_definition_v1 *plugin =
                state->tui_plugins[i];

            for (size_t j = 0; j < plugin->event_hook_count; ++j) {
                const struct telos_tui_event_hook *hook =
                    &plugin->event_hooks[j];

                if (hook->on_event != NULL) {
                    hook->on_event(hook->context, &fe);
                }
            }
        }
    }

    switch (event->frontend_kind) {
    case TELOS_FRONTEND_RESPONSE_STARTED:
        state->response_active = true;
        if (state->turn_phase == TUI_PHASE_WAITING) {
            state->turn_phase = TUI_PHASE_THINKING;
            state->thinking_start_ms = monotonic_milliseconds();
        }
        break;
    case TELOS_FRONTEND_TEXT_DELTA:
        state->response_active = true;
        if (state->turn_phase != TUI_PHASE_RESPONSE) {
            if (state->turn_phase == TUI_PHASE_THINKING) {
                state->response_start_ms = monotonic_milliseconds();
            }
            state->turn_phase = TUI_PHASE_RESPONSE;
        }
        stream_text(state, event->text);
        break;
    case TELOS_FRONTEND_TOOL_STARTED:
        flush_stream(state, true);
        if (state->turn_phase != TUI_PHASE_TOOLS) {
            state->turn_phase = TUI_PHASE_TOOLS;
            state->tools_start_ms = monotonic_milliseconds();
        }
        record_tool_event(state, TUI_TOOL_RUNNING, event->name,
                          event->text);
        break;
    case TELOS_FRONTEND_TOOL_COMPLETED:
        flush_stream(state, true);
        if (state->turn_phase == TUI_PHASE_TOOLS) {
            state->tools_total_ms +=
                monotonic_milliseconds() - state->tools_start_ms;
        }
        record_tool_event(state, TUI_TOOL_COMPLETED, event->name,
                          event->text);
        break;
    case TELOS_FRONTEND_TOOL_FAILED:
        flush_stream(state, true);
        if (state->turn_phase == TUI_PHASE_TOOLS) {
            state->tools_total_ms +=
                monotonic_milliseconds() - state->tools_start_ms;
        }
        record_tool_event(state, TUI_TOOL_FAILED, event->name,
                          event->text);
        break;
    case TELOS_FRONTEND_THINKING_DELTA:
        if (event->text[0] != '\0') {
            size_t room = sizeof(state->thinking_buffer) -
                          state->thinking_size - 1;
            size_t copy = strlen(event->text);

            if (copy > room) {
                copy = room;
            }
            if (copy > 0) {
                memcpy(state->thinking_buffer + state->thinking_size,
                       event->text, copy);
                state->thinking_size += copy;
                state->thinking_buffer[state->thinking_size] = '\0';
            }
        }
        break;
    case TELOS_FRONTEND_THINKING_COMPLETED:
        state->thinking_collapsed = true;
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

static void write_turn_summary(struct tui_state *state)
{
    static const char *const spinners[] = {"⠋", "⠙", "⠹", "⠸",
                                            "⠼", "⠴", "⠦", "⠧"};
    int64_t now = monotonic_milliseconds();
    int64_t wait_ms = 0;
    int64_t thinking_ms = 0;
    int64_t response_ms = 0;
    int64_t tools_ms = state->tools_total_ms;
    int64_t total_ms;
    struct tm clock_tm;
    time_t clock_seconds;
    char completed[32];
    char summary[TUI_RENDER_LINE_SIZE];

    (void)spinners;

    if (state->turn_start_ms > 0) {
        wait_ms = state->thinking_start_ms > 0
                      ? state->thinking_start_ms - state->turn_start_ms
                      : now - state->turn_start_ms;
    }
    if (state->thinking_start_ms > 0 && state->response_start_ms > 0) {
        thinking_ms = state->response_start_ms - state->thinking_start_ms;
    }
    if (state->response_start_ms > 0) {
        response_ms = state->turn_phase == TUI_PHASE_TOOLS
                          ? state->tools_start_ms - state->response_start_ms
                          : now - state->response_start_ms;
    }
    if (response_ms < 0) {
        response_ms = 0;
    }
    if (wait_ms < 0) {
        wait_ms = 0;
    }
    if (thinking_ms < 0) {
        thinking_ms = 0;
    }
    total_ms = wait_ms + thinking_ms + response_ms + tools_ms;

    clock_seconds = time(NULL);
    if (localtime_r(&clock_seconds, &clock_tm) == NULL ||
        strftime(completed, sizeof(completed), "%H:%M:%S",
                 &clock_tm) == 0) {
        copy_text(completed, sizeof(completed), "--:--:--");
    }
    if (snprintf(summary, sizeof(summary),
                 "Model wait %.1fs · Thinking %.1fs · Response %.1fs · "
                 "Tools %.1fs · Total %.1fs · Completed %s",
                 wait_ms / 1000.0, thinking_ms / 1000.0,
                 response_ms / 1000.0, tools_ms / 1000.0,
                 total_ms / 1000.0, completed) < (int)sizeof(summary)) {
        history_append_text(state, TUI_HISTORY_STATUS, "•", summary);
    }
    state->turn_phase = TUI_PHASE_IDLE;
}

static bool drain_events(struct tui_state *state,
                         bool *did_work,
                         struct telos_error **error)
{
    struct queued_event event;
    bool processed = false;

    /*
     * Process queued events without rendering directly.  All visual
     * output happens in render_dynamic() on the next frame; direct
     * rendering here would corrupt the frame bookkeeping and leave
     * stale content on screen.
     */
    while (queue_pop(state, &event)) {
        processed = true;
        if (event.kind == QUEUED_FRONTEND_EVENT) {
            handle_frontend_event(state, &event);
        } else if (event.kind == QUEUED_TURN_ERROR) {
            flush_stream(state, true);
            write_status_line(state, "✗", event.text,
                              state->color ? "\033[38;5;203m" : NULL);
        } else if (event.kind == QUEUED_TURN_COMPLETED) {
            flush_stream(state, true);
            if (state->response_active) {
                history_append_line(state, TUI_HISTORY_PLAIN, NULL, "",
                                    0, false);
            }
            if (!state->turn_summary_written) {
                write_turn_summary(state);
                state->turn_summary_written = true;
            }
            if (pthread_join(state->worker, NULL) != 0) {
                telos_error_set(error, TELOS_ERROR_DOMAIN_STATE, EIO,
                          "Terminal Agent worker could not be joined");
                return false;
            }
            state->worker_active = false;
            state->response_active = false;
            if (state->tool_count > 0) {
                state->tools_collapsed = true;
            }
            telos_cancel_release(state->cancel);
            state->cancel = NULL;
            {
                char prompt[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];

                if (pop_steer_prompt(state, prompt, sizeof(prompt))) {
                    if (!start_turn(state, prompt, error)) {
                        return false;
                    }
                }
            }
        }
    }
    if (did_work != NULL) {
        *did_work = processed;
    }
    return true;
}

static bool editor_insert(struct tui_state *state, const char *text,
                          size_t size)
{
    if (size > state->maximum_input_bytes - state->input_size) {
        write_all(state->output_descriptor, "\a", 1);
        return false;
    }
    state->completion_active = false;
    memmove(state->input + state->input_cursor + size,
            state->input + state->input_cursor,
            state->input_size - state->input_cursor + 1);
    memcpy(state->input + state->input_cursor, text, size);
    state->input_cursor += size;
    state->input_size += size;
    state->history_scroll = 0;
    return true;
}

static void editor_backspace(struct tui_state *state)
{
    size_t prior = previous_character(state->input, state->input_cursor);

    if (prior == state->input_cursor) {
        return;
    }
    state->completion_active = false;
    memmove(state->input + prior, state->input + state->input_cursor,
            state->input_size - state->input_cursor + 1);
    state->input_size -= state->input_cursor - prior;
    state->input_cursor = prior;
    state->history_scroll = 0;
}

static void editor_delete(struct tui_state *state)
{
    size_t next = next_character(state->input, state->input_size,
                                 state->input_cursor);

    if (next == state->input_cursor) {
        return;
    }
    state->completion_active = false;
    memmove(state->input + state->input_cursor, state->input + next,
            state->input_size - next + 1);
    state->input_size -= next - state->input_cursor;
    state->history_scroll = 0;
}

static void editor_clear(struct tui_state *state)
{
    state->input[0] = '\0';
    state->input_size = 0;
    state->input_cursor = 0;
    state->completion_active = false;
    state->history_scroll = 0;
}

static bool submit_editor(struct tui_state *state,
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

static bool complete_command(struct tui_state *state)
{
    size_t count;
    struct telos_frontend_completion_item item;

    if (!session_completion_valid(state) &&
        !command_completion_valid(state)) {
        state->completion_active = false;
        return false;
    }
    if (!state->completion_active) {
        if (state->input_size >= sizeof(state->completion_prefix)) {
            return false;
        }
        copy_text(state->completion_prefix, sizeof(state->completion_prefix),
                  state->input);
        state->completion_index = 0;
        state->completion_active = true;
    }
    count = completion_count(state);
    if (count == 0 ||
        !completion_item_at(state, state->completion_index % count, &item) ||
        item.value[0] == '\0') {
        state->completion_active = false;
        return false;
    }
    state->completion_index = (state->completion_index + 1) % count;
    if (strlen(item.value) >= sizeof(state->input)) {
        state->completion_active = false;
        return false;
    }
    state->input_size = strlen(item.value);
    memcpy(state->input, item.value, state->input_size + 1);
    state->input_cursor = state->input_size;
    return true;
}

static bool select_model_selector(struct tui_state *state,
                                  struct telos_error **error)
{
    const struct telos_model_catalog *catalog = state->session->model_catalog;
    const struct telos_model_descriptor *model;
    char command[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    size_t count = model_selector_count(state);

    if (catalog == NULL || state->model_selector_index >= count) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_STATE, EINVAL,
                  "Model selector is unavailable");
        return false;
    }
    model = model_selector_at(state, state->model_selector_index);
    if (snprintf(command, sizeof(command), "/model %s/%s", model->provider,
                 model->id) >= (int)sizeof(command)) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "Selected model name is too long");
        return false;
    }
    state->model_selector_active = false;
    state->model_selector_offset = 0;
    return start_turn(state, command, error);
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

static void history_scroll_older(struct tui_state *state)
{
    size_t maximum = state->history_count > TUI_HISTORY_VISIBLE
                         ? state->history_count - TUI_HISTORY_VISIBLE
                         : 0;

    if (state->history_scroll < maximum) {
        size_t remaining = maximum - state->history_scroll;

        state->history_scroll += remaining < TUI_HISTORY_PAGE_SIZE
                                     ? remaining
                                     : TUI_HISTORY_PAGE_SIZE;
    }
}

static void history_scroll_newer(struct tui_state *state)
{
    if (state->history_scroll > 0) {
        state->history_scroll = state->history_scroll < TUI_HISTORY_PAGE_SIZE
                                    ? 0
                                    : state->history_scroll -
                                          TUI_HISTORY_PAGE_SIZE;
    }
}

static size_t handle_model_selector_key(struct tui_state *state,
                                        const char *data, size_t size,
                                        struct telos_error **error)
{
    size_t count = model_selector_count(state);

    if (count == 0) {
        state->model_selector_active = false;
        return 1;
    }
    if (key_sequence(data, size, "\033[A") || data[0] == 'k') {
        state->model_selector_index =
            state->model_selector_index == 0
                ? count - 1
                : state->model_selector_index - 1;
        model_selector_adjust_offset(state);
        return data[0] == 'k' ? 1 : 3;
    }
    if (key_sequence(data, size, "\033[B") || data[0] == 'j') {
        state->model_selector_index =
            (state->model_selector_index + 1) % count;
        model_selector_adjust_offset(state);
        return data[0] == 'j' ? 1 : 3;
    }
    if (data[0] == '\r' || data[0] == '\n') {
        select_model_selector(state, error);
        return 1;
    }
    if (data[0] == '\033' || (unsigned char)data[0] == 0x03U) {
        state->model_selector_active = false;
        state->model_selector_offset = 0;
        return 1;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* TUI host and overlay management                                      */
/* ------------------------------------------------------------------ */

bool telos_tui_host_activate_overlay(struct telos_tui_host *host,
                                     const char *overlay_id,
                                     void *context,
                                     struct telos_error **error)
{
    if (host == NULL || host->state == NULL || overlay_id == NULL) {
        return false;
    }
    if (host->state->active_overlay != NULL) {
        return false;
    }
    for (size_t i = 0; i < host->state->tui_plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin =
            host->state->tui_plugins[i];

        for (size_t j = 0; j < plugin->overlay_count; ++j) {
            const struct telos_tui_overlay *overlay = &plugin->overlays[j];

            if (strcmp(overlay->id, overlay_id) == 0) {
                host->state->active_overlay = overlay;
                host->state->active_overlay_context = context;
                host->state->overlay_columns = terminal_columns(host->state);
                host->state->overlay_rows = terminal_rows(host->state);
                return true;
            }
        }
    }
    (void)error;
    return false;
}

bool telos_tui_host_close_overlay(struct telos_tui_host *host)
{
    if (host == NULL || host->state == NULL ||
        host->state->active_overlay == NULL) {
        return false;
    }
    if (host->state->active_overlay->on_close != NULL) {
        host->state->active_overlay->on_close(
            host->state->active_overlay_context);
    }
    host->state->active_overlay = NULL;
    host->state->active_overlay_context = NULL;
    return true;
}

size_t telos_tui_host_columns(const struct telos_tui_host *host)
{
    return host == NULL || host->state == NULL
               ? 80
               : terminal_columns(host->state);
}

size_t telos_tui_host_rows(const struct telos_tui_host *host)
{
    return host == NULL || host->state == NULL
               ? 24
               : terminal_rows(host->state);
}

const struct telos_frontend_session *
telos_tui_host_session(const struct telos_tui_host *host)
{
    return host == NULL || host->state == NULL
               ? NULL
               : host->state->session;
}

bool telos_tui_host_submit(struct telos_tui_host *host,
                           const char *line,
                           struct telos_error **error)
{
    if (host == NULL || host->state == NULL || line == NULL) {
        return false;
    }
    if (host->state->exit_requested) {
        return false;
    }
    return start_turn(host->state, line, error);
}

bool telos_tui_host_notice(struct telos_tui_host *host,
                           const char *text,
                           struct telos_error **error)
{
    if (host == NULL || host->state == NULL || text == NULL) {
        return false;
    }
    return queue_frontend_event(
        &(const struct telos_frontend_event){
            .kind = TELOS_FRONTEND_NOTICE,
            .text = text,
        },
        host->state, error);
}

static bool dispatch_tui_overlay_input(struct tui_state *state,
                                        const char *data, size_t size,
                                        struct telos_error **error)
{
    if (state->active_overlay == NULL ||
        state->active_overlay->handle_input == NULL) {
        return false;
    }
    {
        bool consumed = false;

        return state->active_overlay->handle_input(
                   state->active_overlay_context, data, size, &consumed,
                   error) &&
               consumed;
    }
}

static bool dispatch_tui_keybinding(struct tui_state *state,
                                     const char *data, size_t size,
                                     struct telos_error **error)
{
    struct telos_tui_host host = { .state = state };

    for (size_t i = 0; i < state->tui_plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin =
            state->tui_plugins[i];

        for (size_t j = 0; j < plugin->keybinding_count; ++j) {
            const struct telos_tui_keybinding *kb = &plugin->keybindings[j];

            if (kb->sequence[0] == '\0') {
                continue;
            }
            if (key_sequence(data, size, kb->sequence) &&
                kb->handler(kb->context, &host, error)) {
                return true;
            }
        }
    }
    return false;
}

static bool dispatch_tui_panel_input(struct tui_state *state,
                                      const char *data, size_t size,
                                      struct telos_error **error)
{
    for (size_t i = 0; i < state->tui_plugin_count; ++i) {
        const struct telos_tui_plugin_definition_v1 *plugin =
            state->tui_plugins[i];

        for (size_t j = 0; j < plugin->panel_count; ++j) {
            const struct telos_tui_panel *panel = &plugin->panels[j];

            if (panel->handle_input != NULL) {
                bool consumed = false;

                if (!panel->handle_input(panel->context, data, size,
                                         &consumed, error)) {
                    return false;
                }
                if (consumed) {
                    return true;
                }
            }
        }
    }
    return false;
}

static size_t handle_key(struct tui_state *state, const char *data,
                         size_t size, struct telos_error **error)
{
    if (state->active_overlay != NULL) {
        if (dispatch_tui_overlay_input(state, data, size, error)) {
            return strlen(data) > 0 ? strlen(data) : 1;
        }
        return 1;
    }
    if (state->model_selector_active) {
        return handle_model_selector_key(state, data, size, error);
    }
    if (dispatch_tui_keybinding(state, data, size, error)) {
        return strlen(data) > 0 ? strlen(data) : 1;
    }
    if (dispatch_tui_panel_input(state, data, size, error)) {
        return strlen(data) > 0 ? strlen(data) : 1;
    }
    if (key_sequence(data, size, "\033[5~")) {
        history_scroll_older(state);
        return 4;
    }
    if (key_sequence(data, size, "\033[6~")) {
        history_scroll_newer(state);
        return 4;
    }

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
        state->completion_active = false;
        if (state->input_size == 0 && state->prior_prompt[0] != '\0') {
            state->input_size = copy_text(state->input, sizeof(state->input),
                                          state->prior_prompt);
            state->input_cursor = state->input_size;
        }
        return 3;
    }
    if (key_sequence(data, size, "\033[B")) {
        state->completion_active = false;
        editor_clear(state);
        return 3;
    }
    if (key_sequence(data, size, "\033[C")) {
        state->completion_active = false;
        state->input_cursor = next_character(
            state->input, state->input_size, state->input_cursor);
        return 3;
    }
    if (key_sequence(data, size, "\033[D")) {
        state->completion_active = false;
        state->input_cursor =
            previous_character(state->input, state->input_cursor);
        return 3;
    }
    if (key_sequence(data, size, "\033[H") ||
        key_sequence(data, size, "\033[1~")) {
        state->completion_active = false;
        state->input_cursor = 0;
        return 3;
    }
    if (key_sequence(data, size, "\033[F") ||
        key_sequence(data, size, "\033[4~")) {
        state->completion_active = false;
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
    if ((unsigned char)data[0] == 0x0fU && state->tool_count > 0) {
        state->tools_collapsed = !state->tools_collapsed;
        return 1;
    }
    if ((unsigned char)data[0] == 0x14U && state->thinking_size > 0) {
        state->thinking_collapsed = !state->thinking_collapsed;
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
            /*
             * First Esc: cancel the active turn.  Second Esc
             * (when cancel already requested): force exit.
             */
            if (telos_cancel_requested(state->cancel)) {
                state->exit_requested = true;
            } else {
                telos_cancel_request(state->cancel);
            }
        } else if (state->input_size > 0) {
            editor_clear(state);
        } else {
            state->exit_requested = true;
        }
        return 1;
    }
    if (data[0] == '\r') {
        size_t skip = 1;

        submit_editor(state, error);
        /*
         * Drain consecutive \r / \n bytes so a single keypress
         * (or paste that includes CRLF) does not trigger multiple
         * submissions.
         */
        while (skip < size &&
               (data[skip] == '\r' || data[skip] == '\n')) {
            ++skip;
        }
        return skip;
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

static bool handle_input(struct tui_state *state,
                         struct telos_error **error)
{
    char input[256];
    ssize_t size = read(state->input_descriptor, input, sizeof(input));
    size_t offset = 0;

    if (size < 0 && (errno == EINTR || errno == EAGAIN)) {
        return true;
    }
    if (size < 0) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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

static bool enable_raw_mode(struct tui_state *state,
                            struct telos_error **error)
{
    struct termios terminal;

    if (tcgetattr(state->input_descriptor, &state->saved_terminal) != 0) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "Terminal raw mode could not be enabled");
        return false;
    }
    state->raw_enabled = true;
    write_text(state->output_descriptor, "\033[?2004h");
    return true;
}

static void disable_raw_mode(struct tui_state *state)
{
    if (!state->raw_enabled) {
        return;
    }
    begin_frame(state);
    clear_dynamic(state);
    write_text(state->output_descriptor,
               "\033[?2004l\033[0m\033[?25h");
    end_frame(state);
    tcsetattr(state->input_descriptor, TCSAFLUSH, &state->saved_terminal);
    state->raw_enabled = false;
}

static bool editor_external(struct tui_state *state,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
                  "External editor file could not be prepared");
        return false;
    }
    disable_raw_mode(state);
    child = fork();
    if (child < 0) {
        enable_raw_mode(state, error);
        unlink(path);
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
            telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
            telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                      "External editor input is too large");
            return false;
        }
    }
    close(descriptor);
    unlink(path);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_STATE, ECHILD,
                  "External editor did not complete successfully");
        return false;
    }
    state->input_size = used;
    state->input_cursor = used;
    state->input[used] = '\0';
    state->completion_active = false;
    return true;
}

static void write_header(struct tui_state *state)
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

static bool run_interactive(struct tui_state *state,
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
    interactive_state = state;
    signal(SIGWINCH, handle_resize);
    write_header(state);
    if (state->session->initial_prompt != NULL &&
        state->session->initial_prompt[0] != '\0' &&
        !start_turn(state, state->session->initial_prompt, error)) {
        goto cleanup;
    }
    {
        bool need_redraw = true;

        while (!state->exit_requested || state->worker_active) {
            int ready;
            bool did_work = false;

            if (state->resize_pending) {
                state->resize_pending = 0;
                need_redraw = true;
            }
            if (!drain_events(state, &did_work, error) ||
                (error != NULL && *error != NULL)) {
                goto cleanup;
            }
            state->spinner += 1;
            if (did_work || need_redraw) {
                if (!render_dynamic(state)) {
                    telos_error_set(error, TELOS_ERROR_DOMAIN_IO, EIO,
                              "Terminal frame could not be rendered");
                    goto cleanup;
                }
                need_redraw = false;
            } else if (state->input_size == 0 && !refresh_footer(state)) {
                /*
                 * Skip the footer spinner refresh while the user is
                 * typing: moving the cursor to the footer and back
                 * every poll would disturb IME composition.
                 */
                telos_error_set(error, TELOS_ERROR_DOMAIN_IO, EIO,
                          "Terminal footer could not be rendered");
                goto cleanup;
            }
            ready = poll(&descriptor, 1, TUI_POLL_MILLISECONDS);
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready < 0) {
            telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
                      "Terminal input polling failed");
            goto cleanup;
        }
        if (ready > 0 && (descriptor.revents & (POLLIN | POLLHUP)) != 0) {
            if (!handle_input(state, error)) {
                goto cleanup;
            }
            need_redraw = true;
        }
        }
    }
    result = true;

cleanup:
    if (state->worker_active && state->cancel != NULL) {
        telos_cancel_request(state->cancel);
        /*
         * Drain remaining events with a bounded wait.  After ~2 s
         * of no progress, detach the worker thread to avoid hanging
         * on a stuck provider.
         */
        for (int attempts = 0;
             state->worker_active && attempts < 2000; ++attempts) {
            if (!drain_events(state, NULL, NULL)) {
                struct timespec delay = {.tv_nsec = 1000000};

                nanosleep(&delay, NULL);
            }
        }
        if (state->worker_active) {
            pthread_detach(state->worker);
            state->worker_active = false;
        }
    }
    disable_raw_mode(state);
    signal(SIGWINCH, SIG_DFL);
    interactive_state = NULL;
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
        if (event->text != NULL && event->text[0] != '\0') {
            write_text(plain->output_descriptor, " · ");
            plain_write_sanitized(plain->output_descriptor, event->text);
        }
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_TOOL_COMPLETED) {
        write_text(plain->output_descriptor, "\n[done] ");
        plain_write_sanitized(plain->output_descriptor, event->name);
        if (event->text != NULL && event->text[0] != '\0') {
            write_text(plain->output_descriptor, " · ");
            plain_write_sanitized(plain->output_descriptor, event->text);
        }
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_TOOL_FAILED) {
        write_text(plain->output_descriptor, "\n[failed] ");
        plain_write_sanitized(plain->output_descriptor, event->name);
        if (event->text != NULL && event->text[0] != '\0') {
            write_text(plain->output_descriptor, " · ");
            plain_write_sanitized(plain->output_descriptor, event->text);
        }
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_NOTICE) {
        write_text(plain->output_descriptor, "\n[notice] ");
        plain_write_sanitized(plain->output_descriptor, event->text);
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_CLIPBOARD) {
        write_text(plain->output_descriptor, "\n[clipboard] ");
        plain_write_sanitized(plain->output_descriptor, event->text);
        write_text(plain->output_descriptor, "\n");
    } else if (event->kind == TELOS_FRONTEND_THINKING_DELTA) {
        write_text(plain->output_descriptor, "\n[thinking] ");
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

static bool run_plain_turn(struct tui_state *state, const char *line,
                           struct telos_error **error)
{
    struct plain_context plain = {
        .output_descriptor = state->output_descriptor,
    };
    struct telos_cancel *cancel = telos_cancel_create();
    bool result;

    if (cancel == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Terminal turn cancellation allocation failed");
        return false;
    }
    result = state->session->turn(line, cancel, plain_emit, &plain, NULL,
                                  state->session->turn_context, error);
    telos_cancel_release(cancel);
    write_text(state->output_descriptor, "\n");
    return result;
}

static bool run_plain_shell(struct tui_state *state, const char *line,
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

static bool run_plain(struct tui_state *state, struct telos_error **error)
{
    char line[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];

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
            telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
    case TELOS_FRONTEND_THINKING_DELTA:
        return "thinking_delta";
    case TELOS_FRONTEND_THINKING_COMPLETED:
        return "thinking_completed";
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "JSON event allocation failed");
        goto cleanup;
    }
    values[0] = owned[0];
    if (text != NULL) {
        keys[count] = "text";
        values[count] = owned[count] = telos_value_new_string(text);
        if (owned[count] == NULL) {
            telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "JSON event text allocation failed");
            goto cleanup;
        }
        ++count;
    }
    if (name != NULL) {
        keys[count] = "name";
        values[count] = owned[count] = telos_value_new_string(name);
        if (owned[count] == NULL) {
            telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                      "JSON event name allocation failed");
            goto cleanup;
        }
        ++count;
    }
    root = telos_value_new_object(keys, values, count);
    if (root == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
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
            telos_error_set(error, TELOS_ERROR_DOMAIN_IO, EIO,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "JSON Frontend Event is invalid");
        return false;
    }
    kind = json_event_name(event->kind);
    if (kind == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "JSON Frontend Event kind is invalid");
        return false;
    }
    tool_event = event->kind == TELOS_FRONTEND_TOOL_STARTED ||
                 event->kind == TELOS_FRONTEND_TOOL_COMPLETED ||
                 event->kind == TELOS_FRONTEND_TOOL_FAILED;
    clipboard_event = event->kind == TELOS_FRONTEND_CLIPBOARD;
    return json_write_event(json, kind, event->text,
                            tool_event || clipboard_event ? event->name : NULL,
                            error);
}

static bool run_json_turn(struct tui_state *state, const char *line,
                          struct json_context *json,
                          struct telos_error **error)
{
    struct telos_cancel *cancel = telos_cancel_create();
    struct telos_error *turn_error = NULL;
    bool result;

    if (cancel == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "JSON turn cancellation allocation failed");
        return false;
    }
    if (!json_write_event(json, "user", line, NULL, error)) {
        telos_cancel_release(cancel);
        return false;
    }
    result = state->session->turn(line, cancel, json_emit, json, NULL,
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

static bool run_json_shell(struct tui_state *state, const char *line,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
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

static bool run_json(struct tui_state *state, struct telos_error **error)
{
    struct json_context json = {
        .output_descriptor = state->output_descriptor,
    };
    char line[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];

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
            telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "RPC request storage is invalid");
        return false;
    }
    request = telos_value_parse_json(line, strlen(line), error);
    if (request == NULL || telos_value_type(request) != TELOS_VALUE_OBJECT) {
        telos_value_release(request);
        if (error != NULL && *error == NULL) {
            telos_error_set(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
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
        telos_error_set(error, TELOS_ERROR_DOMAIN_PROTOCOL, EPROTO,
                  "RPC request type is unsupported");
        return false;
    }
    if (text == NULL || text[0] == '\0' || strlen(text) >= payload_size) {
        telos_value_release(request);
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, E2BIG,
                  "RPC request payload is missing or too large");
        return false;
    }
    memcpy(payload, text, strlen(text) + 1);
    telos_value_release(request);
    return true;
}

static bool run_rpc(struct tui_state *state, struct telos_error **error)
{
    struct json_context json = {
        .output_descriptor = state->output_descriptor,
    };
    char line[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];
    char payload[TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES + 1U];

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
            telos_error_set(error, TELOS_ERROR_DOMAIN_IO, errno,
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

bool telos_tui_frontend_run(const telos_tui_frontend_config *config,
                                 struct telos_error **error)
{
    struct tui_state *state;
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
            TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_ARGUMENT, EINVAL,
                  "Terminal Frontend configuration is invalid");
        return false;
    }
    state = calloc(1, sizeof(*state));
    if (state == NULL) {
        telos_error_set(error, TELOS_ERROR_DOMAIN_MEMORY, ENOMEM,
                  "Terminal Frontend allocation failed");
        return false;
    }
    state->session = config->session;
    state->input_descriptor = config->input_descriptor;
    state->output_descriptor = config->output_descriptor;
    state->maximum_input_bytes =
        config->maximum_input_bytes == 0
            ? TELOS_TUI_DEFAULT_MAXIMUM_INPUT_BYTES
            : config->maximum_input_bytes;
    state->response_first_line = true;
    interactive = !config->force_plain && isatty(state->input_descriptor) &&
                  isatty(state->output_descriptor);
    state->color = interactive && no_color == NULL && term != NULL &&
                   strcmp(term, "dumb") != 0;

    /* Load TUI plugins from config. */
    state->tui_plugin_count = 0;
    for (size_t i = 0;
         i < config->tui_plugin_count &&
         state->tui_plugin_count < TELOS_TUI_MAXIMUM_PLUGINS;
         ++i) {
        state->tui_plugins[state->tui_plugin_count++] =
            &config->tui_plugins[i];
    }

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

bool telos_tui_frontend_run_stdio(const telos_frontend_session *session,
                                       struct telos_error **error)
{
    const struct telos_tui_frontend_config config = {
        .session = session,
        .input_descriptor = STDIN_FILENO,
        .output_descriptor = STDOUT_FILENO,
        .tui_plugins = NULL,
        .tui_plugin_count = 0,
    };

    return telos_tui_frontend_run(&config, error);
}

