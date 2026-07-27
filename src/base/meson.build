telos_base = static_library(
    'telos-base',
    [
        'cancel.c',
        'clock.c',
        'error.c',
        'event.c',
        'id.c',
        'trace.c',
        'value.c',
        'value_json.c',
    ],
    include_directories: inc_public,
)

telos_base_dep = declare_dependency(
    include_directories: inc_public,
    link_with: telos_base,
)
