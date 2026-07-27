telos_plugin_host = executable(
    'telos-plugin-host',
    'telos-plugin-host/main.c',
    dependencies: telos_rpc_dep,
    install: true,
)

telos_abi_check = executable(
    'telos-abi-check',
    'telos-abi-check/main.c',
    dependencies: dl_dep,
    install: true,
)

telos_cli = executable(
    'telos',
    'telos/main.c',
    dependencies: telos_dep,
    install: true,
)

telos_build = executable(
    'telos-build',
    'telos-build/main.c',
    install: true,
)
