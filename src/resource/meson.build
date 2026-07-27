telos_resource = static_library(
    'telos-resource',
    'resource.c',
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: inc_public,
)

telos_resource_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        thread_dep,
    ],
    include_directories: inc_public,
    link_with: telos_resource,
)
