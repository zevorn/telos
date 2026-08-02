telos_model_catalog_inc = include_directories('include')

telos_model_catalog_plugin = static_library(
    'telos-model-catalog',
    'src/model_catalog.c',
    dependencies: telos_base_dep,
    include_directories: [
        inc_public,
        telos_model_catalog_inc,
    ],
)

telos_model_catalog_plugin_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: telos_model_catalog_inc,
    link_with: telos_model_catalog_plugin,
)

if get_option('shared_plugins')
    telos_model_catalog_module = shared_module(
        'telos-model-catalog',
        'src/plugin.c',
        dependencies: telos_model_catalog_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.model-catalog',
    )
endif

install_headers(
    'include/telos/plugins/model_catalog.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.model-catalog',
)
