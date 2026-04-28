#!/usr/bin/env bash

set -euo pipefail

export PATH="/opt/homebrew/bin:/usr/local/bin:/Applications/Docker.app/Contents/Resources/bin:$PATH"

usage() {
    cat <<'EOF'
Usage
  bash script/calibrate-rb-g.sh [out.md] [options]

Options
  --sizes "128 256 512"
  --eps "0.07 0.08 0.09"
  --coarse-rounds 100
  --focus-rounds 1000
  --seed 1
  --lambda 40

Defaults
  COARSE_ROUNDS=100
  FOCUS_ROUNDS=1000
  TARGET_LAMBDA=40
  SEED_BASE=1
  SIZES="128 256 512 1024 2048"
  EPS=""

Notes
  - runs the RB-OKVS-only checker
  - writes one markdown summary file only
  - first does a coarse scan, then rechecks the boundary region more heavily
  - lambda is fixed to 40 for the reported g_lambda value
  - set EPS to override the default per-size eps grid
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="${BENCH_DIR:-$REPO_ROOT/build-docker/benchmarks/rbokvs-pqpsi}"
RUN_TAG="$(date '+%Y%m%d-%H%M%S')"
OUT_FILE="$BENCH_DIR/rb-g-grid-${RUN_TAG}.md"
if [[ "${1:-}" != "" && "${1:-}" != --* ]]; then
    OUT_FILE="$1"
    shift
fi
if [[ "$OUT_FILE" != /* ]]; then
    OUT_FILE="$REPO_ROOT/$OUT_FILE"
fi

COARSE_ROUNDS="${COARSE_ROUNDS:-100}"
FOCUS_ROUNDS="${FOCUS_ROUNDS:-1000}"
TARGET_LAMBDA="${TARGET_LAMBDA:-40}"
SEED_BASE="${SEED_BASE:-1}"
SIZES="${SIZES:-128 256 512 1024 2048}"

while [[ $# -gt 0 ]]; do
    case "$1" in
        --sizes)
            SIZES="$2"
            shift 2
            ;;
        --eps)
            EPS="$2"
            shift 2
            ;;
        --w)
            echo "--w is not used by smart calibration; tune code uses coarse step=8 and focus step=4." >&2
            shift 2
            ;;
        --rounds)
            FOCUS_ROUNDS="$2"
            shift 2
            ;;
        --coarse-rounds)
            COARSE_ROUNDS="$2"
            shift 2
            ;;
        --focus-rounds)
            FOCUS_ROUNDS="$2"
            shift 2
            ;;
        --seed)
            SEED_BASE="$2"
            shift 2
            ;;
        --lambda)
            TARGET_LAMBDA="$2"
            shift 2
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage
            exit 1
            ;;
    esac
done

mkdir -p "$BENCH_DIR"
mkdir -p "$(dirname "$OUT_FILE")"

if [[ ! -f "$REPO_ROOT/bin/rbokvs_g_check" ]]; then
    echo "Missing $REPO_ROOT/bin/rbokvs_g_check. Run script/build-docker-pqpsi-bench.sh first." >&2
    exit 1
fi

OUT_REL="${OUT_FILE#$REPO_ROOT/}"
if [[ "$OUT_REL" == "$OUT_FILE" ]]; then
    echo "OUT_FILE must be under the repo root so Docker can write it." >&2
    exit 1
fi

echo "[g] output: $OUT_REL" >&2
echo "[g] sizes: $SIZES" >&2
echo "[g] coarse rounds/case: $COARSE_ROUNDS" >&2
echo "[g] focus rounds/case: $FOCUS_ROUNDS" >&2
echo "[g] target lambda: $TARGET_LAMBDA" >&2
if [[ -n "${EPS:-}" ]]; then
    echo "[g] eps override: $EPS" >&2
else
    echo "[g] eps grid: default per size from rbokvs_g_check" >&2
fi

docker run --rm --platform linux/amd64 \
    -v "$REPO_ROOT:/work" \
    -w /work \
    ubuntu:22.04 \
    bash -lc "set -euo pipefail && \
        apt-get update >/tmp/apt-update.log 2>&1 && \
        apt-get install -y --no-install-recommends libboost-system1.74.0 libboost-thread1.74.0 libgmp10 libgf2x3 libntl44 libsodium23 >/tmp/apt-install.log 2>&1 && \
        RB_GRID_SIZES='$SIZES' RB_GRID_EPS='${EPS:-}' \
        ./bin/rbokvs_g_check smart '/work/$OUT_REL' '$COARSE_ROUNDS' '$FOCUS_ROUNDS' '$SEED_BASE' '$TARGET_LAMBDA'"

echo "[g] wrote $OUT_FILE" >&2
