test_shell = find_program('sh')

test(
    'sdk-contract',
    test_shell,
    args: [
        meson.current_source_dir() / 'test_sdk_contract.sh',
        meson.project_build_root(),
        meson.project_source_root(),
        telos_abi_check.full_path(),
    ],
    depends: telos_abi_check,
    suite: 'functional',
    timeout: 120,
)

test_installer = executable(
    'test-installer',
    'test_installer.c',
    dependencies: telos_dep,
)

test(
    'plugin-installer',
    test_shell,
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
    suite: 'functional',
    timeout: 180,
)

test(
    'cli-functional',
    test_shell,
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
    suite: 'functional',
    timeout: 180,
)

zephyr_source_generator = find_program(
    meson.project_source_root() / 'tools/generate_zephyr_sources.py',
)

test(
    'build-contract',
    test_shell,
    args: [
        meson.current_source_dir() / 'test_build_contract.sh',
        meson.project_source_root(),
        zephyr_source_generator.full_path(),
    ],
    suite: 'functional',
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
    suite: 'functional',
    timeout: 30,
)

test(
    'telos-build-help',
    telos_build,
    args: '--help',
    suite: 'functional',
)

if tui_frontend_available and curl_transport_available
    test_agent_server = executable(
        'test-agent-server',
        'test_agent_server.c',
    )

    test(
        'agent-chat',
        test_shell,
        args: [
            meson.current_source_dir() / 'test_chat.sh',
            telos_cli.full_path(),
            test_agent_server.full_path(),
        ],
        depends: [
            telos_cli,
            test_agent_server,
        ],
        suite: 'functional',
        timeout: 30,
    )

    test_oauth_server = executable(
        'test-oauth-server',
        'test_oauth_server.c',
    )

    test(
        'agent-chat-oauth',
        test_shell,
        args: [
            meson.current_source_dir() / 'test_chat_oauth.sh',
            telos_cli.full_path(),
            test_oauth_server.full_path(),
        ],
        depends: [
            telos_cli,
            test_oauth_server,
        ],
        suite: 'functional',
        timeout: 30,
    )
endif
