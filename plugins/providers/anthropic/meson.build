anthropic_inc = include_directories('include')

telos_anthropic_plugin = static_library(
    'telos-anthropic',
    'src/anthropic.c',
    dependencies: [
        telos_base_dep,
        telos_security_dep,
    ],
    include_directories: [
        inc_public,
        anthropic_inc,
    ],
)

telos_anthropic_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_security_dep,
    ],
    include_directories: anthropic_inc,
    link_with: telos_anthropic_plugin,
)

if get_option('shared_plugins')
    telos_anthropic_module = shared_module(
        'telos-anthropic',
        'src/plugin.c',
        dependencies: telos_anthropic_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.anthropic',
    )
endif

install_headers(
    'include/telos/plugins/anthropic.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.anthropic',
)
