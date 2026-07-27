if get_option('tests')
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

    test_session_retry = executable(
        'test-session-retry',
        'test_session_retry.c',
        dependencies: telos_dep,
    )

    test('session-retry', test_session_retry)

    test_session_failure = executable(
        'test-session-failure',
        'test_session_failure.c',
        dependencies: telos_dep,
    )

    test('session-failure', test_session_failure)

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

    test_plugin_lifecycle = executable(
        'test-plugin-lifecycle',
        'test_plugin_lifecycle.c',
        dependencies: telos_dep,
    )

    test('plugin-lifecycle', test_plugin_lifecycle)

    fixture_plugin_good = shared_module(
        'fixture-plugin-good',
        'fixtures/plugin_good.c',
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

    test_resource_skill = executable(
        'test-resource-skill',
        'test_resource_skill.c',
        dependencies: telos_dep,
    )

    test('resource-skill', test_resource_skill)

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

    test_secret = executable(
        'test-secret',
        'test_secret.c',
        dependencies: telos_dep,
    )

    test('secret', test_secret)
endif
