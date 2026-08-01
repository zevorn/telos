curl_transport_inc = include_directories('include')

telos_curl_transport_plugin = static_library(
    'telos-curl-transport',
    'src/curl_transport.c',
    dependencies: [
        telos_base_dep,
        curl_transport_dep,
        thread_dep,
    ],
    include_directories: [
        inc_public,
        curl_transport_inc,
    ],
)

telos_curl_transport_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: curl_transport_inc,
    link_with: telos_curl_transport_plugin,
)

if get_option('shared_plugins')
    telos_curl_transport_module = shared_module(
        'telos-curl-transport',
        'src/plugin.c',
        dependencies: telos_curl_transport_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.curl-transport',
    )
endif

install_headers(
    'include/telos/plugins/curl_transport.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.curl-transport',
)
