ring_store_inc = include_directories('include')

telos_ring_store_plugin = static_library(
    'telos-ring-store',
    'src/ring.c',
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: [
        inc_public,
        ring_store_inc,
    ],
)

telos_ring_store_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_store_dep,
    ],
    include_directories: ring_store_inc,
    link_with: telos_ring_store_plugin,
)

if get_option('shared_plugins')
    telos_ring_store_module = shared_module(
        'telos-ring-store',
        'src/plugin.c',
        dependencies: telos_ring_store_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.ring-store',
    )
endif

install_headers(
    'include/telos/plugins/ring_store.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.ring-store',
)
