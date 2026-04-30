#!/usr/bin/env bash

set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:/Applications/Docker.app/Contents/Resources/bin:$PATH"

usage() {
    cat <<'EOF'
Usage
  bash script/test-rbokvs-pqpsi.sh <setSize> [hits] [warmups] [rounds] [portBase] [extra rb flags...]
  SET_SIZE=128 KEM=eckem PI=xoodoo THREADS=4 CHANNELS=4 bash script/test-rbokvs-pqpsi.sh

Preferred wrapper
  bash script/pqpsi.sh test thread 128 127 5 --kem obf-mlkem --pi hctr --threads 4
  bash script/pqpsi.sh test process 128 5 --kem obf-mlkem --pi hctr --threads 4

Examples
  bash script/test-rbokvs-pqpsi.sh 256
  bash script/test-rbokvs-pqpsi.sh 256 128
  bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --rb-eps 0.10 --rb-w 96
  bash script/test-rbokvs-pqpsi.sh 256 128 1 5 43000 --threads 4
  KEM=obf-mlkem PI=keccak1600-12 BOB_PI=0 bash script/test-rbokvs-pqpsi.sh 128

Defaults
  This is the in-process/thread test path. For the two-process loopback path,
  use: bash script/pqpsi.sh test process ...
  SET_SIZE first positional arg, or SET_SIZE env, or first SIZES value, or 128
  HITS=n-1
  WARMUPS=1
  ROUNDS=5
  PORT_BASE=43000
  THREAD_MODE=multi
  THREADS=4
  CHANNELS=THREADS
  KEM=obf-mlkem
  PI=hctr
  BOB_PI=0
  RB_LAMBDA=40
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
fi

first_size="${SIZES:-128}"
first_size="${first_size%% *}"

SET_SIZE="${1:-${SET_SIZE:-$first_size}}"
HITS="${2:-${HITS:-}}"
WARMUPS="${3:-${WARMUPS:-1}}"
ROUNDS="${4:-${ROUNDS:-5}}"
PORT_BASE="${5:-${PORT_BASE:-43000}}"

shift_count=$#
if (( shift_count > 5 )); then
    shift_count=5
fi
shift "$shift_count"

THREAD_MODE="${THREAD_MODE:-multi}"
THREADS="${THREADS:-4}"
CHANNELS="${CHANNELS:-}"
KEM="${KEM:-obf-mlkem}"
PI="${PI:-hctr}"
PI_LAMBDA="${PI_LAMBDA:-128}"
BOB_PI="${BOB_PI:-0}"
RB_LAMBDA="${RB_LAMBDA:-40}"
RB_EPS="${RB_EPS:-}"
RB_W="${RB_W:-}"
RB_COLS="${RB_COLS:-}"

if [[ "$THREAD_MODE" == "single" ]]; then
    THREADS=1
    CHANNELS=1
elif [[ -z "$CHANNELS" ]]; then
    CHANNELS="$THREADS"
fi

EXTRA_ARGS=()
if [[ $# -gt 0 ]]; then
    EXTRA_ARGS=("$@")
fi

if [[ -z "$HITS" ]]; then
    HITS=$(( SET_SIZE - 1 ))
fi

RUN_ARGS=()
if [[ -n "$RB_EPS" ]]; then
    RUN_ARGS+=(--rb-eps "$RB_EPS")
fi
if [[ -n "$RB_W" ]]; then
    RUN_ARGS+=(--rb-w "$RB_W")
fi
if [[ -n "$RB_COLS" ]]; then
    RUN_ARGS+=(--rb-cols "$RB_COLS")
fi
RUN_ARGS+=(--rb-lambda "$RB_LAMBDA")
RUN_ARGS+=(--kem "$KEM" --pi "$PI" --pi-lambda "$PI_LAMBDA")
if [[ "$BOB_PI" == "0" || "$BOB_PI" == "false" || "$BOB_PI" == "off" || "$BOB_PI" == "no" ]]; then
    RUN_ARGS+=(--no-bob-pi)
fi
if [[ "$THREAD_MODE" == "single" ]]; then
    RUN_ARGS+=(--single-thread)
else
    RUN_ARGS+=(--multi-thread --threads "$THREADS" --channels "$CHANNELS")
fi
if (( ${#EXTRA_ARGS[@]} > 0 )); then
    RUN_ARGS+=("${EXTRA_ARGS[@]}")
fi

docker run --rm --platform linux/amd64 \
  -v "$PWD:/work" \
  -w /work \
  ubuntu:22.04 \
  bash -lc "apt-get update >/tmp/apt-update.log 2>&1 && \
    apt-get install -y --no-install-recommends \
      libboost-system1.74.0 libboost-thread1.74.0 \
      libgmp10 libsodium23 >/tmp/apt-install.log 2>&1 && \
    ./bin/rbokvs_pqpsi_test $SET_SIZE $WARMUPS $ROUNDS $PORT_BASE --hits $HITS ${RUN_ARGS[*]-}"
