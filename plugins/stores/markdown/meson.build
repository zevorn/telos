markdown_store_inc = include_directories('include')

telos_markdown_store_plugin = static_library(
    'telos-markdown-store',
    'src/markdown.c',
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: [
        inc_public,
        markdown_store_inc,
    ],
)

telos_markdown_store_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: markdown_store_inc,
    link_with: telos_markdown_store_plugin,
)

if get_option('shared_plugins')
    telos_markdown_store_module = shared_module(
        'telos-markdown-store',
        'src/plugin.c',
        dependencies: telos_markdown_store_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.markdown-store',
    )
endif

install_headers(
    'include/telos/plugins/markdown_store.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.markdown-store',
)
