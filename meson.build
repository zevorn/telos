project(
    'telos',
    'c',
    version: '0.1.0',
    meson_version: '>=1.4.0',
    default_options: [
        'c_std=c17',
        'warning_level=3',
        'werror=true',
    ],
)

if get_option('c_std') != 'c17'
    error('Telos requires the C17 language standard')
endif

if get_option('plugin_abi') != 1
    error('Telos v0.1 supports Plugin ABI version 1 only')
endif

if host_machine.system() == 'darwin'
    add_project_arguments('-D_DARWIN_C_SOURCE=1', language: 'c')
endif

inc_public = include_directories('include')
thread_dep = dependency('threads')
cc = meson.get_compiler('c')
dl_dep = cc.find_library('dl', required: false)

subdir('src')
subdir('plugins')

host_platform_deps = []
if host_machine.system() == 'linux'
    subdir('platforms/linux')
    host_platform_deps += telos_linux_platform_dep
endif

telos_dep = declare_dependency(
    dependencies: [
        telos_core_dep,
        telos_official_plugins_dep,
    ] + host_platform_deps,
)

subdir('tools')
subdir('sdk')
subdir('tests')

install_subdir(
    'include/telos',
    install_dir: get_option('includedir'),
)
