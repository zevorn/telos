telos_posix_tools_inc = include_directories('include')

telos_posix_tools_plugin = static_library(
    'telos-posix-tools',
    'src/posix_tools.c',
    dependencies: [
        telos_base_dep,
        telos_plugin_dep,
        telos_tool_dep,
    ],
    include_directories: [
        inc_public,
        telos_posix_tools_inc,
    ],
)

telos_posix_tools_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_plugin_dep,
        telos_tool_dep,
    ],
    include_directories: telos_posix_tools_inc,
    link_with: telos_posix_tools_plugin,
)

if get_option('shared_plugins')
    telos_posix_tools_module = shared_module(
        'telos-posix-tools',
        'src/plugin.c',
        dependencies: telos_posix_tools_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.posix-tools',
    )
endif

install_headers(
    'include/telos/plugins/posix_tools.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.posix-tools',
)
