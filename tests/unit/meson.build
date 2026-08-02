unit_tests = [
    ['command', 'test-command', 'test_command.c'],
    ['model', 'test-model', 'test_model.c'],
    ['types', 'test-types', 'test_types.c'],
    ['id', 'test-id', 'test_id.c'],
    ['zero-allocation-utilities', 'test-util', 'test_util.c'],
    ['error', 'test-error', 'test_error.c'],
    ['cancel', 'test-cancel', 'test_cancel.c'],
    ['value', 'test-value', 'test_value.c'],
    ['value-scalar', 'test-value-scalar', 'test_value_scalar.c'],
    ['value-array', 'test-value-array', 'test_value_array.c'],
    ['value-object', 'test-value-object', 'test_value_object.c'],
    [
        'value-object-duplicate',
        'test-value-object-duplicate',
        'test_value_object_duplicate.c',
    ],
    ['event', 'test-event', 'test_event.c'],
    ['session-no-tool', 'test-session-no-tool', 'test_session_no_tool.c'],
    ['session-cancel', 'test-session-cancel', 'test_session_cancel.c'],
    ['session-tool', 'test-session-tool', 'test_session_tool.c'],
    [
        'session-tool-join',
        'test-session-tool-join',
        'test_session_tool_join.c',
    ],
    ['session-actor', 'test-session-actor', 'test_session_actor.c'],
    ['actor-invalid', 'test-actor-invalid', 'test_actor_invalid.c'],
    ['session-retry', 'test-session-retry', 'test_session_retry.c'],
    [
        'session-retry-exhausted',
        'test-session-retry-exhausted',
        'test_session_retry_exhausted.c',
    ],
    ['session-failure', 'test-session-failure', 'test_session_failure.c'],
    ['session-invalid', 'test-session-invalid', 'test_session_invalid.c'],
    [
        'state-fragment-slots',
        'test-state-fragment',
        'test_state_fragment.c',
    ],
    ['value-json', 'test-value-json', 'test_value_json.c'],
    [
        'value-json-invalid',
        'test-value-json-invalid',
        'test_value_json_invalid.c',
    ],
    ['value-sensitive', 'test-value-sensitive', 'test_value_sensitive.c'],
    ['value-equal', 'test-value-equal', 'test_value_equal.c'],
    ['clock', 'test-clock', 'test_clock.c'],
    ['trace', 'test-trace', 'test_trace.c'],
    ['base-invalid', 'test-base-invalid', 'test_base_invalid.c'],
    ['registry', 'test-registry', 'test_registry.c'],
    ['registry-atomic', 'test-registry-atomic', 'test_registry_atomic.c'],
    [
        'registry-invalid',
        'test-registry-invalid',
        'test_registry_invalid.c',
    ],
    ['prompt', 'test-prompt', 'test_prompt.c'],
    ['prompt-trust', 'test-prompt-trust', 'test_prompt_trust.c'],
    ['prompt-invalid', 'test-prompt-invalid', 'test_prompt_invalid.c'],
    ['tool-policy', 'test-tool-policy', 'test_tool_policy.c'],
    [
        'tool-policy-denied',
        'test-tool-policy-denied',
        'test_tool_policy_denied.c',
    ],
    ['tool-invalid', 'test-tool-invalid', 'test_tool_invalid.c'],
    ['agent-loop', 'test-agent-loop', 'test_agent_loop.c'],
    ['agent-remote-state', 'test-agent-remote', 'test_agent_remote.c'],
    ['agent-failures', 'test-agent-failures', 'test_agent_failures.c'],
    ['agent-invalid', 'test-agent-invalid', 'test_agent_invalid.c'],
    ['secret', 'test-secret', 'test_secret.c'],
    ['config-precedence', 'test-config', 'test_config.c'],
    ['config-invalid', 'test-config-invalid', 'test_config_invalid.c'],
]

foreach entry : unit_tests
    unit_test = executable(
        entry[1],
        entry[2],
        dependencies: telos_dep,
    )

    test(entry[0], unit_test, suite: 'unit')
endforeach

allocation_failure_link_args = []
allocation_failure_c_args = []
if host_machine.system() == 'linux'
    allocation_failure_link_args += [
        '-Wl,--wrap=malloc',
        '-Wl,--wrap=calloc',
        '-Wl,--wrap=realloc',
    ]
elif host_machine.system() == 'darwin'
    allocation_failure_c_args += '-DTELOS_TEST_MALLOC_ZONES=1'
endif

test_allocation_failures = executable(
    'test-allocation-failures',
    'test_allocation_failures.c',
    c_args: allocation_failure_c_args,
    dependencies: telos_dep,
    link_args: allocation_failure_link_args,
)

test(
    'allocation-failure-matrix',
    test_allocation_failures,
    args: [
        meson.project_source_root() / 'sdk/templates/tool/plugin.toml',
        meson.project_source_root() / 'sdk/templates/tool/telos.lock',
    ],
    suite: 'unit',
)

if host_machine.system() == 'linux'
    test_linux_platform = executable(
        'test-linux-platform',
        'test_linux_platform.c',
        c_args: '-DTELOS_TEST_EXPECTED_ARCHITECTURE='
            + linux_platform_architecture,
        dependencies: telos_linux_platform_dep,
        link_args: '-Wl,--wrap=sysconf',
    )

    test(
        'linux-platform',
        test_linux_platform,
        args: linux_platform_target,
        suite: 'unit',
    )
endif
