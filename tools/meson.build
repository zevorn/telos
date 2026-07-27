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

if get_option('openai_smoke')
    curl_dep = dependency('libcurl', required: true)

    executable(
        'telos-openai-smoke',
        'telos-openai-smoke/main.c',
        dependencies: [telos_dep, curl_dep],
        install: true,
    )
endif
