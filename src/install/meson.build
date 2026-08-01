telos_install_c_args = [
    '-DTELOS_INSTALL_TARGET="'
        + host_machine.system() + '-' + host_machine.cpu_family() + '"',
]

if host_machine.system() == 'darwin'
    telos_install_c_args += '-DTELOS_HAVE_NSGETENVIRON=1'
endif

telos_install = static_library(
    'telos-install',
    [
        'config.c',
        'install.c',
        'manifest.c',
        'sha256.c',
    ],
    c_args: telos_install_c_args,
    include_directories: inc_public,
    dependencies: telos_base_dep,
)

telos_install_dep = declare_dependency(
    include_directories: inc_public,
    link_with: telos_install,
)
