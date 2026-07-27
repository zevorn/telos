telos_tool = static_library(
    'telos-tool',
    'tool.c',
    dependencies: [
        telos_base_dep,
        telos_registry_dep,
    ],
    include_directories: inc_public,
)

telos_tool_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
        telos_registry_dep,
    ],
    include_directories: inc_public,
    link_with: telos_tool,
)
