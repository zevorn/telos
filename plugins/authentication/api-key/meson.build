api_key_auth_inc = include_directories('include')

telos_api_key_auth_plugin = static_library(
    'telos-api-key-auth',
    'src/api_key_auth.c',
    dependencies: telos_base_dep,
    include_directories: [
        inc_public,
        api_key_auth_inc,
    ],
)

telos_api_key_auth_plugin_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: api_key_auth_inc,
    link_with: telos_api_key_auth_plugin,
)

if get_option('shared_plugins')
    telos_api_key_auth_module = shared_module(
        'telos-api-key-auth',
        'src/plugin.c',
        dependencies: telos_api_key_auth_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.api-key-auth',
    )
endif

install_headers(
    'include/telos/plugins/api_key_auth.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.api-key-auth',
)
