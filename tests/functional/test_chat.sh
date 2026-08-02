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
