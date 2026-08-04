telos_help_display_plugin = static_library(
    'telos-help-display',
    'src/plugin.c',
    dependencies: [
        telos_base_dep,
    ],
    include_directories: [
        inc_public,
    ],
)

telos_help_display_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
    ],
    link_with: telos_help_display_plugin,
)

if get_option('shared_plugins')
    telos_help_display_module = shared_module(
        'telos-help-display',
        'src/plugin.c',
        dependencies: telos_help_display_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.help-display',
    )
endif

install_data(
    'plugin.toml',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.help-display',
)
