project_guidance_inc = include_directories('include')

telos_project_guidance_plugin = static_library(
    'telos-project-guidance',
    'src/guidance.c',
    dependencies: telos_base_dep,
    include_directories: [
        inc_public,
        project_guidance_inc,
    ],
)

telos_project_guidance_plugin_dep = declare_dependency(
    dependencies: telos_base_dep,
    include_directories: project_guidance_inc,
    link_with: telos_project_guidance_plugin,
)

if get_option('shared_plugins')
    telos_project_guidance_module = shared_module(
        'telos-project-guidance',
        'src/plugin.c',
        dependencies: telos_project_guidance_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.project-guidance',
    )
endif

install_headers(
    'include/telos/plugins/project_guidance.h',
    subdir: 'telos/plugins',
)

install_data(
    'plugin.toml',
    'telos.lock',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.project-guidance',
)
