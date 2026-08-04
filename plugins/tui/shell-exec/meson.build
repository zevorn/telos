telos_shell_exec_plugin = static_library(
    'telos-shell-exec',
    'src/plugin.c',
    dependencies: [
        telos_base_dep,
    ],
    include_directories: [
        inc_public,
    ],
)

telos_shell_exec_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
    ],
    link_with: telos_shell_exec_plugin,
)

if get_option('shared_plugins')
    telos_shell_exec_module = shared_module(
        'telos-shell-exec',
        'src/plugin.c',
        dependencies: telos_shell_exec_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.shell-exec',
    )
endif

install_data(
    'plugin.toml',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.shell-exec',
)
