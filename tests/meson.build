if get_option('tests')
    test_types = executable(
        'test-types',
        'test_types.c',
        dependencies: telos_dep,
    )

    test('types', test_types)

    test_util = executable(
        'test-util',
        'test_util.c',
        dependencies: telos_dep,
    )

    test('zero-allocation-utilities', test_util)

    test_id = executable(
        'test-id',
        'test_id.c',
        dependencies: telos_dep,
    )

    test('id', test_id)

    test_error = executable(
        'test-error',
        'test_error.c',
        dependencies: telos_dep,
    )

    test('error', test_error)

    test_cancel = executable(
        'test-cancel',
        'test_cancel.c',
        dependencies: telos_dep,
    )

    test('cancel', test_cancel)

    test_value = executable(
        'test-value',
        'test_value.c',
        dependencies: telos_dep,
    )

    test('value', test_value)

    test_value_scalar = executable(
        'test-value-scalar',
        'test_value_scalar.c',
        dependencies: telos_dep,
    )

    test('value-scalar', test_value_scalar)

    test_value_array = executable(
        'test-value-array',
        'test_value_array.c',
        dependencies: telos_dep,
    )

    test('value-array', test_value_array)

    test_value_object = executable(
        'test-value-object',
        'test_value_object.c',
        dependencies: telos_dep,
    )

    test('value-object', test_value_object)

    test_value_object_duplicate = executable(
        'test-value-object-duplicate',
        'test_value_object_duplicate.c',
        dependencies: telos_dep,
    )

    test('value-object-duplicate', test_value_object_duplicate)

    test_event = executable(
        'test-event',
        'test_event.c',
        dependencies: telos_dep,
    )

    test('event', test_event)

    test_session_no_tool = executable(
        'test-session-no-tool',
        'test_session_no_tool.c',
        dependencies: telos_dep,
    )

    test('session-no-tool', test_session_no_tool)

    test_session_cancel = executable(
        'test-session-cancel',
        'test_session_cancel.c',
        dependencies: telos_dep,
    )

    test('session-cancel', test_session_cancel)

    test_session_tool = executable(
        'test-session-tool',
        'test_session_tool.c',
        dependencies: telos_dep,
    )

    test('session-tool', test_session_tool)

    test_session_tool_join = executable(
        'test-session-tool-join',
        'test_session_tool_join.c',
        dependencies: telos_dep,
    )

    test('session-tool-join', test_session_tool_join)

    test_session_actor = executable(
        'test-session-actor',
        'test_session_actor.c',
        dependencies: telos_dep,
    )

    test('session-actor', test_session_actor)

    test_actor_invalid = executable(
        'test-actor-invalid',
        'test_actor_invalid.c',
        dependencies: telos_dep,
    )

    test('actor-invalid', test_actor_invalid)

    test_session_retry = executable(
        'test-session-retry',
        'test_session_retry.c',
        dependencies: telos_dep,
    )

    test('session-retry', test_session_retry)

    test_session_retry_exhausted = executable(
        'test-session-retry-exhausted',
        'test_session_retry_exhausted.c',
        dependencies: telos_dep,
    )

    test('session-retry-exhausted', test_session_retry_exhausted)

    test_session_failure = executable(
        'test-session-failure',
        'test_session_failure.c',
        dependencies: telos_dep,
    )

    test('session-failure', test_session_failure)

    test_session_invalid = executable(
        'test-session-invalid',
        'test_session_invalid.c',
        dependencies: telos_dep,
    )

    test('session-invalid', test_session_invalid)

    test_state_fragment = executable(
        'test-state-fragment',
        'test_state_fragment.c',
        dependencies: telos_dep,
    )

    test('state-fragment-slots', test_state_fragment)

    test_memory_store = executable(
        'test-memory-store',
        'test_memory_store.c',
        dependencies: telos_dep,
    )

    test('memory-store', test_memory_store)

    test_ring_store = executable(
        'test-ring-store',
        'test_ring_store.c',
        dependencies: telos_dep,
    )

    test('ring-store', test_ring_store)

    test_store_invalid = executable(
        'test-store-invalid',
        'test_store_invalid.c',
        dependencies: telos_dep,
    )

    test('store-invalid', test_store_invalid)

    test_value_json = executable(
        'test-value-json',
        'test_value_json.c',
        dependencies: telos_dep,
    )

    test('value-json', test_value_json)

    test_value_json_invalid = executable(
        'test-value-json-invalid',
        'test_value_json_invalid.c',
        dependencies: telos_dep,
    )

    test('value-json-invalid', test_value_json_invalid)

    test_value_sensitive = executable(
        'test-value-sensitive',
        'test_value_sensitive.c',
        dependencies: telos_dep,
    )

    test('value-sensitive', test_value_sensitive)

    test_value_equal = executable(
        'test-value-equal',
        'test_value_equal.c',
        dependencies: telos_dep,
    )

    test('value-equal', test_value_equal)

    test_clock = executable(
        'test-clock',
        'test_clock.c',
        dependencies: telos_dep,
    )

    test('clock', test_clock)

    test_trace = executable(
        'test-trace',
        'test_trace.c',
        dependencies: telos_dep,
    )

    test('trace', test_trace)

    test_base_invalid = executable(
        'test-base-invalid',
        'test_base_invalid.c',
        dependencies: telos_dep,
    )

    test('base-invalid', test_base_invalid)

    test_allocation_failures = executable(
        'test-allocation-failures',
        'test_allocation_failures.c',
        dependencies: telos_dep,
        link_args: [
            '-Wl,--wrap=malloc',
            '-Wl,--wrap=calloc',
            '-Wl,--wrap=realloc',
        ],
    )

    test(
        'allocation-failure-matrix',
        test_allocation_failures,
        args: [
            meson.project_source_root() / 'sdk/templates/tool/plugin.toml',
            meson.project_source_root() / 'sdk/templates/tool/telos.lock',
        ],
    )

    test_markdown_store = executable(
        'test-markdown-store',
        'test_markdown_store.c',
        dependencies: telos_dep,
    )

    test('markdown-store', test_markdown_store)

    test_markdown_store_partial = executable(
        'test-markdown-store-partial',
        'test_markdown_store_partial.c',
        dependencies: telos_dep,
    )

    test('markdown-store-partial', test_markdown_store_partial)

    test_markdown_invalid = executable(
        'test-markdown-invalid',
        'test_markdown_invalid.c',
        dependencies: telos_dep,
    )

    test('markdown-invalid', test_markdown_invalid)

    test_registry = executable(
        'test-registry',
        'test_registry.c',
        dependencies: telos_dep,
    )

    test('registry', test_registry)

    test_registry_atomic = executable(
        'test-registry-atomic',
        'test_registry_atomic.c',
        dependencies: telos_dep,
    )

    test('registry-atomic', test_registry_atomic)

    test_registry_invalid = executable(
        'test-registry-invalid',
        'test_registry_invalid.c',
        dependencies: telos_dep,
    )

    test('registry-invalid', test_registry_invalid)

    test_plugin_lifecycle = executable(
        'test-plugin-lifecycle',
        'test_plugin_lifecycle.c',
        dependencies: telos_dep,
    )

    test('plugin-lifecycle', test_plugin_lifecycle)

    test_host_api = executable(
        'test-host-api',
        'test_host_api.c',
        dependencies: telos_dep,
    )

    test('plugin-host-api-table', test_host_api)

    test_plugin_rolling = executable(
        'test-plugin-rolling',
        'test_plugin_rolling.c',
        dependencies: telos_dep,
    )

    test('plugin-rolling-generation', test_plugin_rolling)

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
    )

    test_rpc_invalid = executable(
        'test-rpc-invalid',
        'test_rpc_invalid.c',
        dependencies: telos_dep,
    )

    test('rpc-invalid', test_rpc_invalid)

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
    )

    test_resource_skill = executable(
        'test-resource-skill',
        'test_resource_skill.c',
        dependencies: telos_dep,
    )

    test('resource-skill', test_resource_skill)

    test_resource_invalid = executable(
        'test-resource-invalid',
        'test_resource_invalid.c',
        dependencies: telos_dep,
    )

    test('resource-invalid', test_resource_invalid)

    test_prompt = executable(
        'test-prompt',
        'test_prompt.c',
        dependencies: telos_dep,
    )

    test('prompt', test_prompt)

    test_prompt_trust = executable(
        'test-prompt-trust',
        'test_prompt_trust.c',
        dependencies: telos_dep,
    )

    test('prompt-trust', test_prompt_trust)

    test_prompt_invalid = executable(
        'test-prompt-invalid',
        'test_prompt_invalid.c',
        dependencies: telos_dep,
    )

    test('prompt-invalid', test_prompt_invalid)

    test_openai_request = executable(
        'test-openai-request',
        'test_openai_request.c',
        dependencies: telos_dep,
    )

    test('openai-request', test_openai_request)

    test_openai_sse = executable(
        'test-openai-sse',
        'test_openai_sse.c',
        dependencies: telos_dep,
    )

    test(
        'openai-sse',
        test_openai_sse,
        args: meson.current_source_dir() / 'fixtures/responses.sse',
    )

    test_openai_sse_invalid = executable(
        'test-openai-sse-invalid',
        'test_openai_sse_invalid.c',
        dependencies: telos_dep,
    )

    test('openai-sse-invalid', test_openai_sse_invalid)

    test_openai_transport = executable(
        'test-openai-transport',
        'test_openai_transport.c',
        dependencies: telos_dep,
    )

    test(
        'openai-local-transport',
        test_openai_transport,
        args: meson.current_source_dir() / 'fixtures/responses.sse',
    )

    test_provider_invalid = executable(
        'test-provider-invalid',
        'test_provider_invalid.c',
        dependencies: telos_dep,
    )

    test('provider-invalid', test_provider_invalid)

    test_tool_policy = executable(
        'test-tool-policy',
        'test_tool_policy.c',
        dependencies: telos_dep,
    )

    test('tool-policy', test_tool_policy)

    test_tool_policy_denied = executable(
        'test-tool-policy-denied',
        'test_tool_policy_denied.c',
        dependencies: telos_dep,
    )

    test('tool-policy-denied', test_tool_policy_denied)

    test_tool_invalid = executable(
        'test-tool-invalid',
        'test_tool_invalid.c',
        dependencies: telos_dep,
    )

    test('tool-invalid', test_tool_invalid)

    test_agent_loop = executable(
        'test-agent-loop',
        'test_agent_loop.c',
        dependencies: telos_dep,
    )

    test('agent-loop', test_agent_loop)

    test_agent_remote = executable(
        'test-agent-remote',
        'test_agent_remote.c',
        dependencies: telos_dep,
    )

    test('agent-remote-state', test_agent_remote)

    test_agent_failures = executable(
        'test-agent-failures',
        'test_agent_failures.c',
        dependencies: telos_dep,
    )

    test('agent-failures', test_agent_failures)

    test_agent_invalid = executable(
        'test-agent-invalid',
        'test_agent_invalid.c',
        dependencies: telos_dep,
    )

    test('agent-invalid', test_agent_invalid)

    test_secret = executable(
        'test-secret',
        'test_secret.c',
        dependencies: telos_dep,
    )

    test('secret', test_secret)

    test(
        'sdk-contract',
        find_program('sh'),
        args: [
            meson.current_source_dir() / 'test_sdk_contract.sh',
            meson.project_build_root(),
            meson.project_source_root(),
            telos_abi_check.full_path(),
        ],
        depends: telos_abi_check,
        timeout: 120,
    )

    test_manifest = executable(
        'test-manifest',
        'test_manifest.c',
        dependencies: telos_dep,
    )

    test(
        'manifest-lock',
        test_manifest,
        args: meson.project_source_root()
            / 'sdk/templates/tool/plugin.toml',
    )

    test_manifest_invalid = executable(
        'test-manifest-invalid',
        'test_manifest_invalid.c',
        dependencies: telos_dep,
    )

    test('manifest-invalid', test_manifest_invalid)

    test_installer = executable(
        'test-installer',
        'test_installer.c',
        dependencies: telos_dep,
    )

    test(
        'plugin-installer',
        find_program('sh'),
        args: [
            meson.current_source_dir() / 'test_installer.sh',
            meson.project_build_root(),
            meson.project_source_root(),
            test_installer.full_path(),
            telos_abi_check.full_path(),
            telos_plugin_host.full_path(),
        ],
        depends: [
            test_installer,
            telos_abi_check,
            telos_plugin_host,
        ],
        timeout: 180,
    )

    test_install_invalid = executable(
        'test-install-invalid',
        'test_install_invalid.c',
        dependencies: telos_dep,
    )

    test('install-invalid', test_install_invalid, timeout: 30)

    test_config = executable(
        'test-config',
        'test_config.c',
        dependencies: telos_dep,
    )

    test('config-precedence', test_config)

    test_config_invalid = executable(
        'test-config-invalid',
        'test_config_invalid.c',
        dependencies: telos_dep,
    )

    test('config-invalid', test_config_invalid)

    test(
        'cli-functional',
        find_program('sh'),
        args: [
            meson.current_source_dir() / 'test_cli.sh',
            meson.project_build_root(),
            meson.project_source_root(),
            telos_cli.full_path(),
            telos_abi_check.full_path(),
            telos_plugin_host.full_path(),
        ],
        depends: [
            telos_cli,
            telos_abi_check,
            telos_plugin_host,
        ],
        timeout: 180,
    )

    zephyr_source_generator = find_program(
        meson.project_source_root() / 'tools/generate_zephyr_sources.py',
    )

    test(
        'build-contract',
        find_program('sh'),
        args: [
            meson.current_source_dir() / 'test_build_contract.sh',
            meson.project_source_root(),
            zephyr_source_generator.full_path(),
        ],
        timeout: 60,
    )

    test_linux_scenario = executable(
        'test-linux-scenario',
        'test_linux_scenario.c',
        dependencies: telos_dep,
    )

    test(
        'linux-agent-process-scenario',
        test_linux_scenario,
        args: [
            telos_plugin_host.full_path(),
            fixture_plugin_process.full_path(),
        ],
        depends: [
            telos_plugin_host,
            fixture_plugin_process,
        ],
        timeout: 30,
    )

    test(
        'telos-build-help',
        telos_build,
        args: '--help',
    )
endif
