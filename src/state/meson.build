telos_state = static_library(
    'telos-state',
    [
        'fragment.c',
        'session.c',
    ],
    dependencies: telos_base_dep,
    include_directories: inc_public,
)

telos_state_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: inc_public,
    link_with: telos_state,
)
