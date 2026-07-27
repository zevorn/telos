telos_rpc = static_library(
    'telos-rpc',
    [
        'process.c',
        'rpc.c',
    ],
    dependencies: [
        thread_dep,
        telos_base_dep,
    ],
    include_directories: inc_public,
)

telos_rpc_dep = declare_dependency(
    dependencies: [
        thread_dep,
        telos_base_dep,
    ],
    include_directories: inc_public,
    link_with: telos_rpc,
)
