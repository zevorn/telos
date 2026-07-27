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

inc_public = include_directories('include')
thread_dep = dependency('threads')
cc = meson.get_compiler('c')
dl_dep = cc.find_library('dl', required: false)

subdir('src')
subdir('tools')
subdir('tests')
