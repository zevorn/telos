agent_skills_inc = include_directories('include')

telos_agent_skills_plugin = static_library(
    'telos-agent-skills',
    'src/resource.c',
    dependencies: [
        telos_resource_dep,
        thread_dep,
    ],
    include_directories: [
        inc_public,
        agent_skills_inc,
    ],
)

telos_agent_skills_plugin_dep = declare_dependency(
    dependencies: [
        telos_resource_dep,
        thread_dep,
    ],
    include_directories: agent_skills_inc,
    link_with: telos_agent_skills_plugin,
)

if get_option('shared_plugins')
    telos_agent_skills_module = shared_module(
        'telos-agent-skills',
        'src/plugin.c',
        dependencies: telos_agent_skills_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.agent-skills',
    )
endif

install_headers(
    'include/telos/plugins/agent_skills.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.agent-skills',
)
