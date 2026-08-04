tui_frontend_inc = include_directories('include')

telos_tui_frontend_plugin = static_library(
    'telos-tui-frontend',
    'src/tui.c',
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: [
        inc_public,
        tui_frontend_inc,
    ],
)

telos_tui_frontend_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: tui_frontend_inc,
    link_with: telos_tui_frontend_plugin,
)

if get_option('shared_plugins')
    telos_tui_frontend_module = shared_module(
        'telos-tui-frontend',
        'src/plugin.c',
        dependencies: telos_tui_frontend_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.tui-frontend',
    )
endif

install_headers(
    'include/telos/plugins/tui_frontend.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.tui-frontend',
)
