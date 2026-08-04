telos_model_selector_plugin = static_library(
    'telos-model-selector',
    'src/plugin.c',
    dependencies: [
        telos_base_dep,
    ],
    include_directories: [
        inc_public,
    ],
)

telos_model_selector_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
    ],
    link_with: telos_model_selector_plugin,
)

if get_option('shared_plugins')
    telos_model_selector_module = shared_module(
        'telos-model-selector',
        'src/plugin.c',
        dependencies: telos_model_selector_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.model-selector',
    )
endif

install_data(
    'plugin.toml',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.model-selector',
)
