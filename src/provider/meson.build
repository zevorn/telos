telos_provider = static_library(
    'telos-provider',
    'openai_responses.c',
    dependencies: telos_base_dep,
    include_directories: inc_public,
)

telos_provider_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: inc_public,
    link_with: telos_provider,
)
