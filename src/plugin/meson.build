telos_plugin = static_library(
    'telos-plugin',
    [
        'loader.c',
        'plugin.c',
    ],
    dependencies: [
        telos_base_dep,
        telos_registry_dep,
        thread_dep,
        dl_dep,
    ],
    include_directories: inc_public,
)

telos_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_registry_dep,
        thread_dep,
        dl_dep,
    ],
    include_directories: inc_public,
    link_with: telos_plugin,
)
