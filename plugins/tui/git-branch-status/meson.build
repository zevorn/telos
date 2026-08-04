telos_git_branch_status_plugin = static_library(
    'telos-git-branch-status',
    'src/plugin.c',
    dependencies: [
        telos_base_dep,
    ],
    include_directories: [
        inc_public,
    ],
)

telos_git_branch_status_plugin_dep = declare_dependency(
    dependencies: [
        telos_base_dep,
    ],
    link_with: telos_git_branch_status_plugin,
)

if get_option('shared_plugins')
    telos_git_branch_status_module = shared_module(
        'telos-git-branch-status',
        'src/plugin.c',
        dependencies: telos_git_branch_status_plugin_dep,
        include_directories: inc_public,
        name_prefix: '',
        install: true,
        install_dir: get_option('libdir')
            / 'telos/plugins/dev.zevorn.git-branch-status',
    )
endif

install_data(
    'plugin.toml',
    install_dir: get_option('libdir')
        / 'telos/plugins/dev.zevorn.git-branch-status',
)
