telos_agent = static_library(
    'telos-agent',
    'agent.c',
    include_directories: inc_public,
    dependencies: [
        thread_dep,
        telos_base_dep,
        telos_provider_dep,
        telos_registry_dep,
        telos_tool_dep,
    ],
)

telos_agent_dep = declare_dependency(
    include_directories: inc_public,
    link_with: telos_agent,
)
