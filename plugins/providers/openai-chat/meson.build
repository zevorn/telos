openai_chat_inc = include_directories('include')

telos_openai_chat_plugin = static_library(
    'telos-openai-chat',
    'src/openai_chat.c',
    dependencies: [
        telos_base_dep,
        telos_security_dep,
    ],
    include_directories: [
        inc_public,
        openai_chat_inc,
    ],
)

telos_openai_chat_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_security_dep,
    ],
    include_directories: openai_chat_inc,
    link_with: telos_openai_chat_plugin,
)

if get_option('shared_plugins')
    telos_openai_chat_module = shared_module(
        'telos-openai-chat',
        'src/plugin.c',
        dependencies: telos_openai_chat_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.openai-chat',
    )
endif

install_headers(
    'include/telos/plugins/openai_chat.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.openai-chat',
)
