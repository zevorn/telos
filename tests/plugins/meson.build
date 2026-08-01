plugin_core_tests = [
    ['store-invalid', 'test-store-invalid', 'test_store_invalid.c'],
    [
        'plugin-lifecycle',
        'test-plugin-lifecycle',
        'test_plugin_lifecycle.c',
    ],
    [
        'plugin-host-api-table',
        'test-host-api',
        'test_host_api.c',
    ],
    [
        'plugin-rolling-generation',
        'test-plugin-rolling',
        'test_plugin_rolling.c',
    ],
    ['rpc-invalid', 'test-rpc-invalid', 'test_rpc_invalid.c'],
    [
        'manifest-invalid',
        'test-manifest-invalid',
        'test_manifest_invalid.c',
    ],
]

foreach entry : plugin_core_tests
    plugin_test = executable(
        entry[1],
        entry[2],
        dependencies: telos_dep,
    )

    test(entry[0], plugin_test, suite: 'plugins')
endforeach

test_install_invalid = executable(
    'test-install-invalid',
    'test_install_invalid.c',
    dependencies: telos_dep,
)

test(
    'install-invalid',
    test_install_invalid,
    suite: 'plugins',
    timeout: 30,
)

test_manifest = executable(
    'test-manifest',
    'test_manifest.c',
    dependencies: telos_dep,
)

test(
    'manifest-lock',
    test_manifest,
    args: meson.project_source_root() / 'sdk/templates/tool/plugin.toml',
    suite: 'plugins',
)

fixture_plugin_good = shared_module(
    'fixture-plugin-good',
    'fixtures/plugin_good.c',
    include_directories: inc_public,
    name_prefix: '',
)

fixture_plugin_process = shared_module(
    'fixture-plugin-process',
    'fixtures/plugin_process_echo.c',
    include_directories: inc_public,
    name_prefix: '',
)

fixture_plugin_no_entry = shared_module(
    'fixture-plugin-no-entry',
    'fixtures/plugin_no_entry.c',
    include_directories: inc_public,
    name_prefix: '',
)

fixture_plugin_init_failure = shared_module(
    'fixture-plugin-init-failure',
    'fixtures/plugin_init_failure.c',
    include_directories: inc_public,
    name_prefix: '',
)

test_plugin_loader = executable(
    'test-plugin-loader',
    'test_plugin_loader.c',
    dependencies: telos_dep,
)

test(
    'plugin-loader',
    test_plugin_loader,
    args: fixture_plugin_good.full_path(),
    depends: fixture_plugin_good,
    suite: 'plugins',
)

test_plugin_loader_abi = executable(
    'test-plugin-loader-abi',
    'test_plugin_loader_abi.c',
    dependencies: telos_dep,
)

test(
    'plugin-loader-abi',
    test_plugin_loader_abi,
    args: fixture_plugin_good.full_path(),
    depends: fixture_plugin_good,
    suite: 'plugins',
)

test_plugin_loader_invalid = executable(
    'test-plugin-loader-invalid',
    'test_plugin_loader_invalid.c',
    dependencies: telos_dep,
)

test(
    'plugin-loader-invalid',
    test_plugin_loader_invalid,
    args: [
        fixture_plugin_good.full_path(),
        fixture_plugin_no_entry.full_path(),
        fixture_plugin_init_failure.full_path(),
    ],
    depends: [
        fixture_plugin_good,
        fixture_plugin_no_entry,
        fixture_plugin_init_failure,
    ],
    suite: 'plugins',
)

test_plugin_host = executable(
    'test-plugin-host',
    'test_plugin_host.c',
    dependencies: telos_dep,
)

test(
    'plugin-host',
    test_plugin_host,
    args: telos_plugin_host.full_path(),
    depends: telos_plugin_host,
    suite: 'plugins',
)

test_plugin_process = executable(
    'test-plugin-process',
    'test_plugin_process.c',
    dependencies: telos_dep,
)

test(
    'plugin-process-failures',
    test_plugin_process,
    args: [
        telos_plugin_host.full_path(),
        fixture_plugin_process.full_path(),
    ],
    depends: [
        telos_plugin_host,
        fixture_plugin_process,
    ],
    suite: 'plugins',
)

openai_plugin_tests = [
    ['openai-request', 'test-openai-request', 'test_openai_request.c'],
    [
        'openai-sse-invalid',
        'test-openai-sse-invalid',
        'test_openai_sse_invalid.c',
    ],
    [
        'provider-invalid',
        'test-provider-invalid',
        'test_provider_invalid.c',
    ],
]

foreach entry : openai_plugin_tests
    plugin_test = executable(
        entry[1],
        entry[2],
        dependencies: telos_openai_responses_plugin_dep,
    )

    test(entry[0], plugin_test, suite: 'plugins')
endforeach

if terminal_frontend_available
    test_terminal_frontend = executable(
        'test-terminal-frontend',
        'test_terminal_frontend.c',
        dependencies: telos_terminal_frontend_plugin_dep,
    )

    test(
        'terminal-frontend',
        test_terminal_frontend,
        suite: 'plugins',
    )

    test_terminal_tui = executable(
        'test-terminal-tui',
        'test_terminal_tui.c',
        dependencies: telos_terminal_frontend_plugin_dep,
    )

    test(
        'terminal-tui',
        test_terminal_tui,
        suite: 'plugins',
        timeout: 10,
    )
endif

if curl_transport_available
    test_curl_transport = executable(
        'test-curl-transport',
        'test_curl_transport.c',
        dependencies: telos_curl_transport_plugin_dep,
    )

    test(
        'curl-transport',
        test_curl_transport,
        suite: 'plugins',
    )
endif

test_openai_sse = executable(
    'test-openai-sse',
    'test_openai_sse.c',
    dependencies: telos_openai_responses_plugin_dep,
)

test(
    'openai-sse',
    test_openai_sse,
    args: meson.current_source_dir() / 'fixtures/responses.sse',
    suite: 'plugins',
)

test_openai_transport = executable(
    'test-openai-transport',
    'test_openai_transport.c',
    dependencies: telos_openai_responses_plugin_dep,
)

test(
    'openai-local-transport',
    test_openai_transport,
    args: meson.current_source_dir() / 'fixtures/responses.sse',
    suite: 'plugins',
)

agent_skills_tests = [
    ['resource-skill', 'test-resource-skill', 'test_resource_skill.c'],
    [
        'resource-invalid',
        'test-resource-invalid',
        'test_resource_invalid.c',
    ],
]

foreach entry : agent_skills_tests
    plugin_test = executable(
        entry[1],
        entry[2],
        dependencies: telos_agent_skills_plugin_dep,
    )

    test(entry[0], plugin_test, suite: 'plugins')
endforeach

store_plugin_tests = [
    [
        'memory-store',
        'test-memory-store',
        'test_memory_store.c',
        telos_memory_store_plugin_dep,
    ],
    [
        'ring-store',
        'test-ring-store',
        'test_ring_store.c',
        telos_ring_store_plugin_dep,
    ],
    [
        'markdown-store',
        'test-markdown-store',
        'test_markdown_store.c',
        telos_markdown_store_plugin_dep,
    ],
    [
        'markdown-store-partial',
        'test-markdown-store-partial',
        'test_markdown_store_partial.c',
        telos_markdown_store_plugin_dep,
    ],
    [
        'markdown-invalid',
        'test-markdown-invalid',
        'test_markdown_invalid.c',
        telos_markdown_store_plugin_dep,
    ],
]

foreach entry : store_plugin_tests
    plugin_test = executable(
        entry[1],
        entry[2],
        dependencies: entry[3],
    )

    test(entry[0], plugin_test, suite: 'plugins')
endforeach

if get_option('shared_plugins')
    official_plugin_modules = [
        telos_memory_store_module,
        telos_ring_store_module,
        telos_markdown_store_module,
        telos_openai_responses_module,
        telos_agent_skills_module,
        telos_project_guidance_module,
    ]
    official_plugin_module_paths = [
        telos_memory_store_module.full_path(),
        telos_ring_store_module.full_path(),
        telos_markdown_store_module.full_path(),
        telos_openai_responses_module.full_path(),
        telos_agent_skills_module.full_path(),
        telos_project_guidance_module.full_path(),
    ]
    official_plugin_source_paths = [
        meson.project_source_root() / 'plugins/stores/memory',
        meson.project_source_root() / 'plugins/stores/ring',
        meson.project_source_root() / 'plugins/stores/markdown',
        meson.project_source_root() / 'plugins/providers/openai-responses',
        meson.project_source_root() / 'plugins/resources/agent-skills',
        meson.project_source_root()
            / 'plugins/context-sources/project-guidance',
    ]
    if terminal_frontend_available
        official_plugin_modules += [telos_terminal_frontend_module]
        official_plugin_module_paths += [
            telos_terminal_frontend_module.full_path(),
        ]
        official_plugin_source_paths += [
            meson.project_source_root() / 'plugins/frontends/terminal',
        ]
    endif
    if curl_transport_available
        official_plugin_modules += [telos_curl_transport_module]
        official_plugin_module_paths += [
            telos_curl_transport_module.full_path(),
        ]
        official_plugin_source_paths += [
            meson.project_source_root() / 'plugins/transports/curl',
        ]
    endif

    test_official_plugins = executable(
        'test-official-plugins',
        'test_official_plugins.c',
        dependencies: [
            telos_core_dep,
            telos_official_plugins_dep,
        ],
    )

    test(
        'official-plugins',
        test_official_plugins,
        args: official_plugin_module_paths + official_plugin_source_paths,
        depends: official_plugin_modules,
        suite: 'plugins',
    )
endif
