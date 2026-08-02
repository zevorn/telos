openai_codex_auth_inc = include_directories('include')

telos_openai_codex_auth_plugin = static_library(
    'telos-openai-codex-auth',
    'src/openai_codex_auth.c',
    dependencies: telos_base_dep,
    include_directories: [
        inc_public,
        openai_codex_auth_inc,
    ],
)

telos_openai_codex_auth_plugin_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: openai_codex_auth_inc,
    link_with: telos_openai_codex_auth_plugin,
)

if get_option('shared_plugins')
    telos_openai_codex_auth_module = shared_module(
        'telos-openai-codex-auth',
        'src/plugin.c',
        dependencies: telos_openai_codex_auth_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.openai-codex-auth',
    )
endif

install_headers(
    'include/telos/plugins/openai_codex_auth.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.openai-codex-auth',
)
