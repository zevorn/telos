openai_responses_inc = include_directories('include')

telos_openai_responses_plugin = static_library(
    'telos-openai-responses',
    'src/openai_responses.c',
    dependencies: [
        telos_base_dep,
        telos_security_dep,
    ],
    include_directories: [
        inc_public,
        openai_responses_inc,
    ],
)

telos_openai_responses_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_security_dep,
    ],
    include_directories: openai_responses_inc,
    link_with: telos_openai_responses_plugin,
)

if get_option('shared_plugins')
    telos_openai_responses_module = shared_module(
        'telos-openai-responses',
        'src/plugin.c',
        dependencies: telos_openai_responses_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.openai-responses',
    )
endif

install_headers(
    'include/telos/plugins/openai_responses.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.openai-responses',
)
