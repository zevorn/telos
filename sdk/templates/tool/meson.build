project(
    'telos-echo-tool',
    'c',
    version: '0.1.0',
    default_options: [
        'c_std=c17',
        'warning_level=3',
        'werror=true',
    ],
)

telos_sdk = dependency('telos-plugin-sdk', version: '>=0.1.0')

shared_module(
    'telos-echo-tool',
    'src/plugin.c',
    dependencies: telos_sdk,
    name_prefix: '',
    install: true,
    install_dir: get_option('libdir') / 'telos/plugins',
)
