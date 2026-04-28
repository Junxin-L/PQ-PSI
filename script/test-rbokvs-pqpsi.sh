#!/usr/bin/env bash

set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:/Applications/Docker.app/Contents/Resources/bin:$PATH"

usage() {
    cat <<'EOF'
Usage
  bash script/test-rbokvs-pqpsi.sh <setSize> [hits] [warmups] [rounds] [portBase] [extra rb flags...]

Examples
  bash script/test-rbokvs-pqpsi.sh 256
  bash script/test-rbokvs-pqpsi.sh 256 128
  bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --rb-eps 0.10 --rb-w 96
  bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --threads 4

Defaults
  hits=n-1
  warmups=1
  rounds=5
  portBase=43000
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || $# -lt 1 ]]; then
    usage
    exit 0
fi

SET_SIZE="$1"
HITS="${2:-}"
WARMUPS="${3:-1}"
ROUNDS="${4:-5}"
PORT_BASE="${5:-43000}"
shift $(( $# >= 5 ? 5 : $# ))

EXTRA_ARGS=()
if [[ $# -gt 0 ]]; then
    EXTRA_ARGS=("$@")
fi

if [[ -z "$HITS" ]]; then
    HITS=$(( SET_SIZE - 1 ))
fi

docker run --rm --platform linux/amd64 \
  -v "$PWD:/work" \
  -w /work \
  ubuntu:22.04 \
  bash -lc "apt-get update >/tmp/apt-update.log 2>&1 && \
    apt-get install -y --no-install-recommends \
      libboost-system1.74.0 libboost-thread1.74.0 \
      libgmp10 libgf2x3 libntl44 libsodium23 >/tmp/apt-install.log 2>&1 && \
    ./bin/rbokvs_pqpsi_test $SET_SIZE $WARMUPS $ROUNDS $PORT_BASE --hits $HITS ${EXTRA_ARGS[*]:-}"
