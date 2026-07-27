telos_actor = static_library(
    'telos-actor',
    'actor.c',
    dependencies: [
        telos_state_dep,
        telos_store_dep,
        thread_dep,
    ],
    include_directories: inc_public,
)

telos_actor_dep = declare_dependency(
    dependencies: [
        telos_state_dep,
        telos_store_dep,
        thread_dep,
    ],
    include_directories: inc_public,
    link_with: telos_actor,
)
