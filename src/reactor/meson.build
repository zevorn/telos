telos_reactor = static_library(
    'telos-reactor',
    'reactor.c',
    dependencies: telos_base_dep,
    include_directories: inc_public,
)

telos_reactor_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: inc_public,
    link_with: telos_reactor,
)
