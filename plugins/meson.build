subdir('stores')
subdir('providers')
subdir('resources')
subdir('context-sources')

telos_official_plugins_dep = declare_dependency(
    dependencies: [
        telos_memory_store_plugin_dep,
        telos_ring_store_plugin_dep,
        telos_markdown_store_plugin_dep,
        telos_openai_responses_plugin_dep,
        telos_agent_skills_plugin_dep,
        telos_project_guidance_plugin_dep,
    ],
)
