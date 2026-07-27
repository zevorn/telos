telos_prompt = static_library(
    'telos-prompt',
    'prompt.c',
    dependencies: telos_base_dep,
    include_directories: inc_public,
)

telos_prompt_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: inc_public,
    link_with: telos_prompt,
)
