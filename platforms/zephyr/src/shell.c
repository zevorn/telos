#include <zephyr/shell/shell.h>

#include <telos/zephyr.h>

static int command_chat(
    const struct shell *shell,
    size_t argument_count,
    char **arguments
)
{
    (void)argument_count;
    (void)arguments;
    shell_print(shell, "chat: Provider Plugin required");
    return 0;
}

static int command_status(
    const struct shell *shell,
    size_t argument_count,
    char **arguments
)
{
    (void)argument_count;
    (void)arguments;
    shell_print(shell, "status: %s", telos_zephyr_status());
    return 0;
}

static int command_plugins(
    const struct shell *shell,
    size_t argument_count,
    char **arguments
)
{
    (void)argument_count;
    (void)arguments;
    shell_print(shell, "plugins: static");
    return 0;
}

static int command_resources(
    const struct shell *shell,
    size_t argument_count,
    char **arguments
)
{
    (void)argument_count;
    (void)arguments;
    shell_print(shell, "resources: compiled generation 1");
    return 0;
}

static int command_trace(
    const struct shell *shell,
    size_t argument_count,
    char **arguments
)
{
    (void)argument_count;
    (void)arguments;
    shell_print(shell, "%s", telos_zephyr_trace());
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(
    telos_commands,
    SHELL_CMD(chat, NULL, "Start or continue a chat", command_chat),
    SHELL_CMD(status, NULL, "Show runtime status", command_status),
    SHELL_CMD(plugins, NULL, "List static Plugins", command_plugins),
    SHELL_CMD(resources, NULL, "List Resources", command_resources),
    SHELL_CMD(trace, NULL, "Show Event trace", command_trace),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(
    telos,
    &telos_commands,
    "Telos Agentic Framework",
    NULL
);
