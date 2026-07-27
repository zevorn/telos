telos_rpc = static_library(
    'telos-rpc',
    'rpc.c',
    dependencies: telos_base_dep,
    include_directories: inc_public,
)

telos_rpc_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: inc_public,
    link_with: telos_rpc,
)
