pkgconfig = import('pkgconfig')

pkgconfig.generate(
    name: 'Telos Plugin SDK',
    filebase: 'telos-plugin-sdk',
    description: 'Stable C ABI headers for Telos Plugins',
    version: meson.project_version(),
    subdirs: '.',
)

install_subdir(
    'schemas',
    install_dir: get_option('datadir') / 'telos',
)

install_subdir(
    'templates',
    install_dir: get_option('datadir') / 'telos',
)
