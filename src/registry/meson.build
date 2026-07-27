telos_registry = static_library(
    'telos-registry',
    'registry.c',
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: inc_public,
)

telos_registry_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: inc_public,
    link_with: telos_registry,
)
