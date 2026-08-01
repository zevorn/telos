telos_plugin_host = executable(
    'telos-plugin-host',
    'telos-plugin-host/main.c',
    dependencies: telos_dep,
    install: true,
)

telos_abi_check = executable(
    'telos-abi-check',
    'telos-abi-check/main.c',
    dependencies: dl_dep,
    install: true,
)

telos_cli_sources = ['telos/main.c']
telos_cli_c_args = []

if terminal_frontend_available and curl_transport_available
    telos_cli_sources += 'telos/chat.c'
    telos_cli_c_args += '-DTELOS_HAVE_CHAT=1'
endif

telos_cli = executable(
    'telos',
    telos_cli_sources,
    c_args: telos_cli_c_args,
    dependencies: telos_dep,
    install: true,
)

telos_build = executable(
    'telos-build',
    'telos-build/main.c',
    install: true,
)
