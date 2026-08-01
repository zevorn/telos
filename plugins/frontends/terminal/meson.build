terminal_frontend_inc = include_directories('include')

telos_terminal_frontend_plugin = static_library(
    'telos-terminal-frontend',
    'src/terminal.c',
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: [
        inc_public,
        terminal_frontend_inc,
    ],
)

telos_terminal_frontend_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: terminal_frontend_inc,
    link_with: telos_terminal_frontend_plugin,
)

if get_option('shared_plugins')
    telos_terminal_frontend_module = shared_module(
        'telos-terminal-frontend',
        'src/plugin.c',
        dependencies: telos_terminal_frontend_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.terminal-frontend',
    )
endif

install_headers(
    'include/telos/plugins/terminal_frontend.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.terminal-frontend',
)
