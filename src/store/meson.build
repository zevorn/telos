telos_store = static_library(
    'telos-store',
    'store.c',
    dependencies: telos_base_dep,
    include_directories: inc_public,
)

telos_store_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: inc_public,
    link_with: telos_store,
)
