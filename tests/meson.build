if get_option('openai_smoke')
    curl_dep = dependency('libcurl', required: true)

    executable(
        'telos-openai-smoke',
        'plugins/openai_smoke.c',
        dependencies: [
            telos_core_dep,
            telos_openai_responses_plugin_dep,
            curl_dep,
        ],
        include_directories: inc_public,
        install: true,
    )
endif

if get_option('tests')
    subdir('unit')
    subdir('plugins')
    subdir('functional')
endif
