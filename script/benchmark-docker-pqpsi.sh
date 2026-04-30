#!/usr/bin/env bash

set -euo pipefail

# Default PQ-PSI benchmark entry.
#
# This intentionally delegates to the MiniPSI/VOLE-PSI aligned loopback
# benchmark. The default setting is:
#   - Linux Docker
#   - one container per benchmark round
#   - receiver and sender as two separate processes
#   - tc netem on lo
#   - reported time = max(receiver protocol time, sender protocol time)
#
# Positional compatibility:
#   script/benchmark-docker-pqpsi.sh [out.md] [rounds] [warmups]

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    exec bash "$(dirname "$0")/benchmark-docker-pqpsi-loopback.sh" "$@"
fi

if [[ -n "${2:-}" ]]; then
    export ROUNDS="$2"
fi
if [[ -n "${3:-}" ]]; then
    export WARMUPS="$3"
fi

exec bash "$(dirname "$0")/benchmark-docker-pqpsi-loopback.sh" "${1:-}"
