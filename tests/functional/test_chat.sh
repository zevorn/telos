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
"$server" >"$temporary/json-port" &
server_pid=$!
attempt=0
while ! test -s "$temporary/json-port"; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 100; then
        echo "JSON Agent fixture server did not start" >&2
        exit 1
    fi
    sleep 0.01
done
json_port=$(sed -n '1p' "$temporary/json-port")
json_endpoint="http://127.0.0.1:$json_port/v1"
HOME="$temporary/home" "$telos" --json \
    --model local-model \
    --endpoint "$json_endpoint" \
    run hello >"$temporary/json.output"
wait "$server_pid"
server_pid=
grep -Fq "You > hello" "$temporary/run.output"
grep -Fq "Telos > hello from functional test" "$temporary/run.output"
grep -Fq '{"event":"user","text":"hello"}' "$temporary/json.output"
grep -Fq '{"event":"turn_completed"}' "$temporary/json.output"

saved_session=$(find "$temporary/home/.telos/sessions" -type f \
    -name '*.jsonl' -size +0c | sort | sed -n '1p')
test -n "$saved_session"
printf '/resume %s\n/session\n/quit\n' "$saved_session" |
    HOME="$temporary/home" TELOS_AGENT_MODEL=unconfigured \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/resume-existing.output"
grep -Fq "session resumed" "$temporary/resume-existing.output"
grep -Fq "$saved_session" "$temporary/resume-existing.output"
grep -Fq "2 messages" "$temporary/resume-existing.output"
printf '/resume %s\n/session\n/rename renamed-session\n/session\n/quit\n' \
    "$saved_session" |
    HOME="$temporary/home" TELOS_AGENT_MODEL=unconfigured \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/session-name.output"
grep -Fq "session hello" "$temporary/session-name.output"
grep -Fq "session renamed-session" "$temporary/session-name.output"
grep -Fq '"type":"session.name"' "$saved_session"
printf '/resume %s\n/session\n/quit\n' "$saved_session" |
    HOME="$temporary/home" TELOS_AGENT_MODEL=unconfigured \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/session-name-persisted.output"
grep -Fq "session renamed-session" "$temporary/session-name-persisted.output"

"$server" >"$temporary/rpc-port" &
server_pid=$!
attempt=0
while ! test -s "$temporary/rpc-port"; do
    attempt=$((attempt + 1))
    if test "$attempt" -ge 100; then
        echo "RPC Agent fixture server did not start" >&2
        exit 1
    fi
    sleep 0.01
done
rpc_port=$(sed -n '1p' "$temporary/rpc-port")
rpc_endpoint="http://127.0.0.1:$rpc_port/v1"
printf '%s\n' \
    '{"type":"prompt","message":"hello"}' \
    '{"type":"quit"}' |
    HOME="$temporary/home" \
    TELOS_AGENT_MODEL=local-model \
    TELOS_AGENT_ENDPOINT="$rpc_endpoint" \
    "$telos" --mode rpc chat >"$temporary/rpc.output"
wait "$server_pid"
server_pid=
grep -Fq '{"event":"text_delta","text":"hello from functional test"}' \
    "$temporary/rpc.output"

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

printf 'login status\n/quit\n' | HOME="$temporary/home" \
    TELOS_AGENT_MODEL=unconfigured \
    "$telos" chat >"$temporary/login-ready.output"
grep -Fq "OpenAI is logged out" "$temporary/login-ready.output"

printf '%s\n' 'login deepseek' 'login-status deepseek' \
    'logout deepseek' 'login-status deepseek' '/quit' |
    HOME="$temporary/home" TELOS_AGENT_PROVIDER=openai \
    TELOS_AGENT_MODEL=unconfigured DEEPSEEK_API_KEY=functional-key \
    "$telos" chat >"$temporary/provider-login.output"
grep -Fq "DeepSeek login completed" "$temporary/provider-login.output"
grep -Fq "DeepSeek is logged in" "$temporary/provider-login.output"
grep -Fq "DeepSeek logout completed" "$temporary/provider-login.output"
grep -Fq "DeepSeek is logged out" "$temporary/provider-login.output"

printf '/login status\n/login\n/login status\n/logout\n/login status\n/quit\n' \
    | HOME="$temporary/home" TELOS_AGENT_PROVIDER=deepseek \
    TELOS_AGENT_MODEL=unconfigured DEEPSEEK_API_KEY=functional-key \
    "$telos" chat >"$temporary/api-key-login.output"
grep -Fq "DeepSeek is logged out" "$temporary/api-key-login.output"
grep -Fq "DeepSeek login completed" "$temporary/api-key-login.output"
grep -Fq "DeepSeek is logged in" "$temporary/api-key-login.output"
grep -Fq "DeepSeek logout completed" "$temporary/api-key-login.output"

printf '[{"role":"user","content":"saved"}]\n' \
    >"$temporary/import.json"
{
    printf '/import %s\n' "$temporary/import.json"
    printf '%s\n' \
        '/name command-test' \
        '/session' \
        '/sessions' \
        '/tree' \
        '/fork child' \
        '/clone' \
        '/new' \
        '/resume' \
        '/compact' \
        '/scoped-models' \
        '/model openai/gpt-5/luna' \
        '/thinking high' \
        '/thinking' \
        '/status' \
        '/status model thinking' \
        '/setting status all' \
        '/setting thinking low' \
        '/setting' \
        '/setting model openai/gpt-5.4' \
        '/model custom-local-model' \
        '/settings' \
        '/reload' \
        '/hotkeys' \
        '/changelog' \
        '/trust'
    printf '/export %s\n/resume %s\n/share %s\n/quit\n' \
        "$temporary/export.json" "$temporary/export.json" \
        "$temporary/share.json"
} | HOME="$temporary/home" \
    TELOS_AGENT_MODEL=unconfigured \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/commands.output"
grep -Fq "session imported" "$temporary/commands.output"
grep -Fq "session named: command-test" "$temporary/commands.output"
grep -Fq "session command-test" "$temporary/commands.output"
grep -Fq "saved sessions:" "$temporary/commands.output"
grep -Fq "0: user saved" "$temporary/commands.output"
grep -Fq "session fork checkpoint saved" "$temporary/commands.output"
grep -Fq "new session started" "$temporary/commands.output"
grep -Fq "session checkpoint resumed" "$temporary/commands.output"
grep -Fq "conversation compacted" "$temporary/commands.output"
grep -Fq "provider=openai model=custom-local-model thinking=off endpoint=http://127.0.0.1:1/v1" \
    "$temporary/commands.output"
grep -Fq "Model set to openai/gpt-5.6-luna" "$temporary/commands.output"
grep -Fq "Thinking level set to high" "$temporary/commands.output"
grep -Fq "thinking=high" "$temporary/commands.output"
grep -Fq "status=context" \
    "$temporary/commands.output"
grep -Fq "Status fields set to model,thinking" "$temporary/commands.output"
grep -Fq "Status fields set to model,thinking,path,branch,context" \
    "$temporary/commands.output"
grep -Fq "Thinking level set to low" "$temporary/commands.output"
grep -Fq "provider=openai model=gpt-5.6-luna thinking=low" \
    "$temporary/commands.output"
grep -Fq "Model set to openai/gpt-5.4" "$temporary/commands.output"
grep -Fq "Model set to openai/custom-local-model" "$temporary/commands.output"
grep -Fq 'model = "openai/custom-local-model"' \
    "$temporary/home/.telos/config.toml"
grep -Fq 'thinking = "off"' "$temporary/home/.telos/config.toml"
grep -Fq 'status = "model,thinking,path,branch,context"' \
    "$temporary/home/.telos/config.toml"
printf '/settings\n/quit\n' | HOME="$temporary/home" \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/persisted.output"
grep -Fq "provider=openai model=custom-local-model thinking=off" \
    "$temporary/persisted.output"

mkdir -p "$temporary/thinking-home"
printf '/model openai/gpt-5.5\n/thinking max\n/quit\n' |
    HOME="$temporary/thinking-home" TELOS_AGENT_MODEL=unconfigured \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/thinking.output"
grep -Fq 'thinking = "max"' \
    "$temporary/thinking-home/.telos/config.toml"
printf '/thinking\n/quit\n' |
    HOME="$temporary/thinking-home" TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/thinking-persisted.output"
grep -Fq "thinking=max" "$temporary/thinking-persisted.output"
grep -Fq "runtime guidance reloaded" \
    "$temporary/commands.output"
grep -Fq "Ctrl+J or Alt+Enter" "$temporary/commands.output"
grep -Fq "Plugin-backed Pi-compatible terminal agent" \
    "$temporary/commands.output"
grep -Fq "trusted project root:" "$temporary/commands.output"
grep -Fq "session exported" "$temporary/commands.output"
grep -Fq "session resumed" "$temporary/commands.output"
test -s "$temporary/export.json"
test -s "$temporary/share.json"
printf '/model openai/gpt-5.5\n/model\n/quit\n' | \
    HOME="$temporary/home" TELOS_AGENT_MODEL=unconfigured \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" chat >"$temporary/filtered.output"
grep -Fq "Model set to openai/gpt-5.5" "$temporary/filtered.output"
grep -Fq "openai/gpt-5.5" "$temporary/filtered.output"
if grep -Fq "deepseek/" "$temporary/filtered.output"; then
    echo "current provider model list leaked another provider" >&2
    exit 1
fi
printf '/session\n/quit\n' | HOME="$temporary/home" \
    TELOS_AGENT_MODEL=unconfigured \
    TELOS_AGENT_ENDPOINT=http://127.0.0.1:1/v1 \
    "$telos" --continue chat >"$temporary/continue.output"
grep -Fq "session " "$temporary/continue.output"
grep -Fq "messages" "$temporary/continue.output"
grep -Fq "1 messages" "$temporary/continue.output"
session_file=$(find "$temporary/home/.telos/sessions" -type f \
    -name '*.jsonl' | sed -n '1p')
test -n "$session_file"
grep -Fq '"type":"message"' "$session_file"

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
