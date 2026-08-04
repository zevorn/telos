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
        echo "OAuth fixture server did not start" >&2
        exit 1
    fi
    sleep 0.01
done

port=$(sed -n '1p' "$temporary/port")
base="http://127.0.0.1:$port"
printf '/login\nhello\n/login status\n/logout\n/quit\n' |
    HOME="$temporary/home" \
    TELOS_AGENT_MODEL=openai/gpt-5/luna \
    TELOS_AGENT_ENDPOINT="$base/v1" \
    TELOS_OPENAI_AUTH_ENDPOINT="$base" \
    "$telos" chat >"$temporary/chat.output"
wait "$server_pid"
server_pid=

grep -Fq "Open $base/codex/device and enter code TEST-CODE" \
    "$temporary/chat.output"
grep -Fq "OpenAI login completed" "$temporary/chat.output"
grep -Fq "Telos > OAuth works" "$temporary/chat.output"
grep -Fq "OpenAI is logged in as account acct-functional" \
    "$temporary/chat.output"
grep -Fq "OpenAI logout completed" "$temporary/chat.output"
test ! -e "$temporary/home/.telos/openai-codex-auth.json"
