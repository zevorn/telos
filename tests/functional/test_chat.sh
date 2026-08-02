#!/bin/sh
set -eu

telos=$1
server=$2
temporary=$(mktemp -d)
server_pid=

cleanup()
{
    if test -n "$server_pid"; then
        kill "$server_pid" 2>/dev/null || true
    fi
    rm -rf "$temporary"
}
trap cleanup EXIT HUP INT TERM

mkdir -p "$temporary/home"
"$server" >"$temporary/port" &
server_pid=$!

attempt=0
while ! test -s "$temporary/port"; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 100; then
        echo "Agent fixture server did not start" >&2
        exit 1
    fi
    sleep 0.01
done

port=$(sed -n '1p' "$temporary/port")
endpoint="http://127.0.0.1:$port/v1"
HOME="$temporary/home" "$telos" \
    --model local-model \
    --endpoint "$endpoint" \
    run hello >"$temporary/run.output"
wait "$server_pid"
server_pid=
grep -Fq "You > hello" "$temporary/run.output"
grep -Fq "Telos > hello from functional test" "$temporary/run.output"

printf '/quit\n' | HOME="$temporary/home" \
    TELOS_AGENT_MODEL=local-model \
    TELOS_AGENT_ENDPOINT="$endpoint" \
    "$telos" chat >"$temporary/chat.output"
grep -Fq "Telos 0.1.0" "$temporary/chat.output"

printf '/quit\n' | HOME="$temporary/home" \
    TELOS_AGENT_MODEL=local-model \
    TELOS_AGENT_ENDPOINT="$endpoint" \
    "$telos" >"$temporary/default.output"
grep -Fq "Telos 0.1.0" "$temporary/default.output"

printf '/login status\n/quit\n' | HOME="$temporary/home" \
    TELOS_AGENT_MODEL=unconfigured \
    "$telos" chat >"$temporary/login-ready.output"
grep -Fq "OpenAI is logged out" "$temporary/login-ready.output"

printf '[{"role":"user","content":"saved"}]\n' \
    >"$temporary/import.json"
{
    printf '/import %s\n' "$temporary/import.json"
    printf '%s\n' \
        '/name command-test' \
        '/session' \
        '/tree' \
        '/fork child' \
        '/clone' \
        '/new' \
        '/resume' \
        '/compact' \
        '/scoped-models' \
        '/settings' \
        '/reload' \
        '/hotkeys' \
        '/changelog' \
        '/trust'
    printf '/export %s\n/share %s\n/quit\n' \
        "$temporary/export.json" "$temporary/share.json"
} | HOME="$temporary/home" \
    TELOS_AGENT_MODEL=unconfigured "$telos" chat >"$temporary/commands.output"
grep -Fq "session imported" "$temporary/commands.output"
grep -Fq "session named: command-test" "$temporary/commands.output"
grep -Fq "session command-test" "$temporary/commands.output"
grep -Fq "0: user saved" "$temporary/commands.output"
grep -Fq "session fork checkpoint saved" "$temporary/commands.output"
grep -Fq "new session started" "$temporary/commands.output"
grep -Fq "session checkpoint resumed" "$temporary/commands.output"
grep -Fq "conversation compacted" "$temporary/commands.output"
grep -Fq "provider=openai model=not configured" \
    "$temporary/commands.output"
grep -Fq "runtime guidance is refreshed per turn" \
    "$temporary/commands.output"
grep -Fq "Ctrl+J or Alt+Enter" "$temporary/commands.output"
grep -Fq "Plugin-backed Pi-compatible terminal agent" \
    "$temporary/commands.output"
grep -Fq "current trusted root" "$temporary/commands.output"
grep -Fq "session exported" "$temporary/commands.output"
test -s "$temporary/export.json"
test -s "$temporary/share.json"

"$server" >"$temporary/chat-port" &
server_pid=$!
attempt=0
while ! test -s "$temporary/chat-port"; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 100; then
        echo "Chat Provider fixture server did not start" >&2
        exit 1
    fi
    sleep 0.01
done

chat_port=$(sed -n '1p' "$temporary/chat-port")
chat_endpoint="http://127.0.0.1:$chat_port/v1"
HOME="$temporary/home" "$telos" \
    --provider deepseek \
    --model deepseek-chat \
    --endpoint "$chat_endpoint" \
    run hello >"$temporary/chat-provider.output"
wait "$server_pid"
server_pid=
grep -Fq "Telos > hello from chat functional test" \
    "$temporary/chat-provider.output"

"$server" >"$temporary/anthropic-port" &
server_pid=$!
attempt=0
while ! test -s "$temporary/anthropic-port"; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 100; then
        echo "Anthropic fixture server did not start" >&2
        exit 1
    fi
    sleep 0.01
done

anthropic_port=$(sed -n '1p' "$temporary/anthropic-port")
anthropic_endpoint="http://127.0.0.1:$anthropic_port/v1"
HOME="$temporary/home" "$telos" \
    --provider anthropic \
    --model claude-sonnet-4-5 \
    --endpoint "$anthropic_endpoint" \
    run hello >"$temporary/anthropic.output"
wait "$server_pid"
server_pid=
grep -Fq "Telos > hello from anthropic functional test" \
    "$temporary/anthropic.output"
