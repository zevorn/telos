subdir('stores')
subdir('providers')
subdir('resources')
subdir('context-sources')

posix_host_plugins_available = host_machine.system() == 'linux' or \
    host_machine.system() == 'darwin'

if posix_host_plugins_available
    subdir('authentication')
endif

terminal_frontend_available = posix_host_plugins_available
if terminal_frontend_available
    subdir('frontends')
endif

curl_transport_available = false
if posix_host_plugins_available
    subdir('transports')
endif

telos_official_plugin_deps = [
    telos_memory_store_plugin_dep,
    telos_ring_store_plugin_dep,
    telos_markdown_store_plugin_dep,
    telos_openai_responses_plugin_dep,
    telos_agent_skills_plugin_dep,
    telos_project_guidance_plugin_dep,
]

if terminal_frontend_available
    telos_official_plugin_deps += telos_terminal_frontend_plugin_dep
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
