#!/bin/sh
#
# Record a live session into an event-sequence fixture.
#
# The telos CLI persists every session event to
# ~/.telos/sessions/<id>.jsonl; this script runs one session, copies
# the freshest recording to the target path and validates it
# (strictly increasing sequence, required fields), so the result is
# safe to keep as a replay fixture under tests/fixtures/events/.
#
# Usage: record_session.sh <cli> <store-path> [prompt ...]
set -eu

cli="$1"
store="$2"
shift 2

sessions_dir="${HOME}/.telos/sessions"
mkdir -p "$(dirname "$store")"

"$cli" "$@"

recording=$(ls -t "$sessions_dir"/*.jsonl 2>/dev/null | head -1)
if [ -z "${recording:-}" ]; then
    echo "record_session: no recording found in $sessions_dir" >&2
    exit 1
fi
cp "$recording" "$store"

python3 - "$store" <<'EOF'
import json
import sys

path = sys.argv[1]
sequences = []
with open(path, encoding="utf-8") as handle:
    for line in handle:
        record = json.loads(line)
        sequences.append(record["sequence"])
        if record["sequence"] <= 0:
            sys.exit(f"record {record['sequence']}: sequence must be positive")
        if len(sequences) > 1 and record["sequence"] != sequences[-2] + 1:
            sys.exit(
                f"record {record['sequence']}: gap after {sequences[-2]}")
        for key in ("event_id", "session_id", "correlation_id",
                    "causation_id", "type", "source"):
            if not record.get(key):
                sys.exit(f"record {record['sequence']}: missing {key}")

print(f"recorded {len(sequences)} events, sequences 1..{sequences[-1]}")
EOF
