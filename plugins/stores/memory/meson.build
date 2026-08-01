memory_store_inc = include_directories('include')

telos_memory_store_plugin = static_library(
    'telos-memory-store',
    'src/memory.c',
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: [
        inc_public,
        memory_store_inc,
    ],
)

telos_memory_store_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: memory_store_inc,
    link_with: telos_memory_store_plugin,
)

if get_option('shared_plugins')
    telos_memory_store_module = shared_module(
        'telos-memory-store',
        'src/plugin.c',
        dependencies: telos_memory_store_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.memory-store',
    )
endif

install_headers(
    'include/telos/plugins/memory_store.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.memory-store',
)
