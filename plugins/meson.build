subdir('stores')
subdir('providers')
subdir('models')
subdir('resources')
subdir('context-sources')

posix_host_plugins_available = host_machine.system() == 'linux' or \
    host_machine.system() == 'darwin'

if posix_host_plugins_available
    subdir('authentication')
    subdir('tools')
endif

tui_frontend_available = posix_host_plugins_available
if tui_frontend_available
    subdir('frontends')
    subdir('tui')
endif

curl_transport_available = false
if posix_host_plugins_available
    subdir('transports')
endif

telos_official_plugin_deps = [
    telos_memory_store_plugin_dep,
    telos_ring_store_plugin_dep,
    telos_markdown_store_plugin_dep,
    telos_jsonl_store_plugin_dep,
    telos_openai_responses_plugin_dep,
    telos_openai_chat_plugin_dep,
    telos_anthropic_plugin_dep,
    telos_model_catalog_plugin_dep,
    telos_agent_skills_plugin_dep,
    telos_project_guidance_plugin_dep,
    telos_api_key_auth_plugin_dep,
]

if posix_host_plugins_available
    telos_official_plugin_deps += telos_posix_tools_plugin_dep
endif

if tui_frontend_available
    telos_official_plugin_deps += telos_tui_frontend_plugin_dep
    telos_official_plugin_deps += telos_model_selector_plugin_dep
endif

if posix_host_plugins_available
    telos_official_plugin_deps += telos_openai_codex_auth_plugin_dep
endif

if curl_transport_available
    telos_official_plugin_deps += telos_curl_transport_plugin_dep
endif

telos_official_plugins_dep = declare_dependency(
    dependencies: telos_official_plugin_deps,
)
