telos_install = static_library(
    'telos-install',
    [
        'config.c',
        'install.c',
        'manifest.c',
        'sha256.c',
    ],
    include_directories: inc_public,
    dependencies: telos_base_dep,
)

telos_install_dep = declare_dependency(
    include_directories: inc_public,
    link_with: telos_install,
)
