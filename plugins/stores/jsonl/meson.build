jsonl_store_inc = include_directories('include')

telos_jsonl_store_plugin = static_library(
    'telos-jsonl-store',
    'src/jsonl.c',
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: [
        inc_public,
        jsonl_store_inc,
    ],
)

telos_jsonl_store_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: jsonl_store_inc,
    link_with: telos_jsonl_store_plugin,
)

if get_option('shared_plugins')
    telos_jsonl_store_module = shared_module(
        'telos-jsonl-store',
        'src/plugin.c',
        dependencies: telos_jsonl_store_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.jsonl-store',
    )
endif

install_headers(
    'include/telos/plugins/jsonl_store.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.jsonl-store',
)
