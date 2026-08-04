telos_keybinding_help_plugin = static_library(
    'telos-keybinding-help',
    'src/plugin.c',
    dependencies: [
        telos_base_dep,
    ],
    include_directories: [
        inc_public,
    ],
)

telos_keybinding_help_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
    ],
    link_with: telos_keybinding_help_plugin,
)

if get_option('shared_plugins')
    telos_keybinding_help_module = shared_module(
        'telos-keybinding-help',
        'src/plugin.c',
        dependencies: telos_keybinding_help_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.keybinding-help',
    )
endif

install_data(
    'plugin.toml',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.keybinding-help',
)
