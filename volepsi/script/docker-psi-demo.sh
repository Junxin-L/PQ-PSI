#!/usr/bin/env bash

set -euo pipefail

IMAGE_NAME="${IMAGE_NAME:-vole-psi-linux}"
DOCKER_PLATFORM="${DOCKER_PLATFORM:-}"
SENDER_SIZE="${1:-128}"
RECEIVER_SIZE="${2:-64}"

if [ -n "${DOCKER_PLATFORM}" ]; then
    DOCKER_PLATFORM_ARGS=(--platform "${DOCKER_PLATFORM}")
else
    DOCKER_PLATFORM_ARGS=()
fi

docker run --rm "${DOCKER_PLATFORM_ARGS[@]}" "${IMAGE_NAME}" bash -lc "
set -euo pipefail
tmpdir=\$(mktemp -d)

python3 - \"\$tmpdir\" \"${SENDER_SIZE}\" \"${RECEIVER_SIZE}\" <<'PY'
import pathlib
import sys

tmpdir = pathlib.Path(sys.argv[1])
ns = int(sys.argv[2])
nr = int(sys.argv[3])

if nr > ns:
    raise SystemExit('receiver size must be <= sender size for this demo')

def to_hex(v: int) -> str:
    return f'{v:032x}'

(tmpdir / 'sender.csv').write_text('\\n'.join(to_hex(i) for i in range(ns)) + '\\n', encoding='ascii')
(tmpdir / 'receiver.csv').write_text('\\n'.join(to_hex(i) for i in range(nr)) + '\\n', encoding='ascii')
PY

./out/build/linux/frontend/frontend -r 1 -server 1 -csv -quiet -indexSet -senderSize ${SENDER_SIZE} -receiverSize ${RECEIVER_SIZE} -in \"\$tmpdir/receiver.csv\" -out \"\$tmpdir/out.txt\" &
pid=\$!
sleep 1
./out/build/linux/frontend/frontend -r 0 -server 0 -csv -quiet -indexSet -senderSize ${SENDER_SIZE} -receiverSize ${RECEIVER_SIZE} -in \"\$tmpdir/sender.csv\"
wait \$pid

python3 - \"\$tmpdir/out.txt\" \"${RECEIVER_SIZE}\" <<'PY'
import pathlib
import sys

out_path = pathlib.Path(sys.argv[1])
expected = int(sys.argv[2])
lines = [line.strip() for line in out_path.read_text(encoding='ascii').splitlines() if line.strip()]

print(f'matches={len(lines)} expected={expected}')
print('first_indexes=' + ','.join(lines[:10]))

if len(lines) != expected:
    raise SystemExit(f'unexpected match count: {len(lines)} != {expected}')
PY
"
