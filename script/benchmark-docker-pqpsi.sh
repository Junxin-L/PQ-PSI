#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C
export LANG=C
export PATH="/opt/homebrew/bin:/usr/local/bin:/Applications/Docker.app/Contents/Resources/bin:$PATH"

usage() {
    cat <<'EOF'
Usage
  bash script/benchmark-docker-pqpsi.sh [out.md] [rounds] [warmups]

Examples
  bash script/benchmark-docker-pqpsi.sh
  bash script/benchmark-docker-pqpsi.sh my-summary.md
  bash script/benchmark-docker-pqpsi.sh my-summary.md 10 2
  ROUNDS=10 WARMUPS=2 bash script/benchmark-docker-pqpsi.sh
  RB_EPS=0.25 bash script/benchmark-docker-pqpsi.sh
  THREADS=4 bash script/benchmark-docker-pqpsi.sh
  THREAD_MODE=single bash script/benchmark-docker-pqpsi.sh

Defaults
  SIZES="128 256 512 1024 2048"
  WARMUPS=1
  ROUNDS=5
  SIM_DELAY_MS=0
  SIM_BW_MBPS=1250
  RB_LAMBDA=40
  RB_EPS=
  RB_COLS=
  RB_W=
  THREADS=4
  PI=keccak1600
  PI_LAMBDA=128
  THREAD_MODE=multi
  DETAIL_MODE=none

Tune one size
  SIZES="1024" RB_EPS=0.10 RB_W=160 bash script/benchmark-docker-pqpsi.sh
  SIZES="1024" THREADS=4 bash script/benchmark-docker-pqpsi.sh

Keep detail reports
  DETAIL_MODE=dir bash script/benchmark-docker-pqpsi.sh
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO_ROOT/build-docker}"
BENCH_DIR="${BENCH_DIR:-$BUILD_DIR/benchmarks/rbokvs-pqpsi}"
DETAIL_MODE="${DETAIL_MODE:-none}"
DETAIL_DIR_NAME="${DETAIL_DIR_NAME:-details}"

# main knobs
SIZES="${SIZES:-128 256 512 1024 2048}"
WARMUPS="${WARMUPS:-${3:-1}}"
ROUNDS="${ROUNDS:-${2:-5}}"
PORT_BASE_START="${PORT_BASE_START:-43000}"

# net knobs
SIM_DELAY_MS="${SIM_DELAY_MS:-0}"
SIM_BW_MBPS="${SIM_BW_MBPS:-1250}"
BENCH_STEM="${BENCH_STEM:-rbokvs-pqpsi}"
NETWORK_LABEL="${NETWORK_LABEL:-delay=${SIM_DELAY_MS} ms, bandwidth=${SIM_BW_MBPS} MB/s}"

# rb knobs
RB_LAMBDA="${RB_LAMBDA:-40}"
RB_EPS="${RB_EPS:-}"
RB_COLS="${RB_COLS:-}"
RB_W="${RB_W:-}"
PI="${PI:-keccak1600}"
PI_LAMBDA="${PI_LAMBDA:-128}"
THREAD_MODE="${THREAD_MODE:-multi}"
THREADS="${THREADS:-4}"
if [[ "$THREADS" == "1" ]]; then
    THREAD_MODE="single"
fi
SAFE_DELAY="${SIM_DELAY_MS//./p}"
SAFE_BW="${SIM_BW_MBPS//./p}"
SAFE_RB_EPS="${RB_EPS//./p}"
RB_TAG="rb-auto"
if [[ -n "$RB_EPS" || -n "$RB_COLS" || -n "$RB_W" || "$RB_LAMBDA" != "40" ]]; then
    RB_TAG="lam-${RB_LAMBDA}"
    if [[ -n "$RB_EPS" ]]; then
        RB_TAG="${RB_TAG}_eps-${SAFE_RB_EPS}"
    fi
    if [[ -n "$RB_COLS" ]]; then
        RB_TAG="${RB_TAG}_m-${RB_COLS}"
    fi
    if [[ -n "$RB_W" ]]; then
        RB_TAG="${RB_TAG}_w-${RB_W}"
    fi
fi
if [[ "$THREAD_MODE" == "single" ]]; then
    THREADS=1
    RB_TAG="${RB_TAG}_thr-single"
else
    THREAD_MODE="multi"
fi
if [[ "$THREADS" != "auto" && "$THREADS" != "0" ]]; then
    RB_TAG="${RB_TAG}_workers-${THREADS}"
fi
if [[ "$PI" != "keccak1600" || "$PI_LAMBDA" != "128" ]]; then
    RB_TAG="${RB_TAG}_pi-${PI}_pilam-${PI_LAMBDA}"
fi
SAFE_SIZES="$(printf '%s' "$SIZES" | tr ' ' '-' | tr '^' 'p')"
DEFAULT_SUMMARY="summary_warm-${WARMUPS}_rounds-${ROUNDS}_delay-${SAFE_DELAY}_bw-${SAFE_BW}_${RB_TAG}_sizes-${SAFE_SIZES}.md"
OUTPUT_FILE="${1:-$BENCH_DIR/$DEFAULT_SUMMARY}"

if [[ "$OUTPUT_FILE" != */* && "$OUTPUT_FILE" != "" ]]; then
    OUTPUT_FILE="$BENCH_DIR/$OUTPUT_FILE"
fi

read -r -a sizes <<< "$SIZES"
powers=()
for size in "${sizes[@]}"; do
    if [[ "$size" =~ ^[0-9]+$ ]] && (( size > 0 )) && (( (size & (size - 1)) == 0 )); then
        power=0
        tmp=$size
        while (( tmp > 1 )); do
            tmp=$(( tmp / 2 ))
            power=$(( power + 1 ))
        done
        powers+=("$power")
    else
        powers+=("?")
    fi
done

summary_value() {
    local report_file="$1"
    local key="$2"
    awk -F'|' -v key="$key" '
        $0 ~ /^\|/ {
            raw_key = $2
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", raw_key)
            raw_val = $3
            gsub(/^[[:space:]]+|[[:space:]]+$/, "", raw_val)
            if (raw_key == key) {
                print raw_val
                exit
            }
        }
    ' "$report_file"
}

append_round_rows() {
    local report_file="$1"
    awk '
        /^## Per-Round Results$/ { in_table = 1; next }
        in_table && /^\|---:/ { next }
        in_table && /^\| [0-9]+ / { print; next }
        in_table && /^## / { exit }
    ' "$report_file"
}

append_round_rows_protocol() {
    local report_file="$1"
    awk '
        /^## Per-Round Results$/ { in_table = 1; next }
        in_table && /^\|---:/ { next }
        in_table && /^\| [0-9]+ / { print; next }
        in_table && /^## / { exit }
    ' "$report_file"
}

if [[ ! -x "$REPO_ROOT/bin/pqpsi_rbokvs_bench" ]]; then
    echo "Missing $REPO_ROOT/bin/pqpsi_rbokvs_bench. Run script/build-docker-pqpsi-bench.sh first." >&2
    exit 1
fi

RB_ARGS=()
if [[ -n "$RB_EPS" ]]; then
    RB_ARGS+=(--rb-eps "$RB_EPS")
fi
if [[ -n "$RB_COLS" ]]; then
    RB_ARGS+=(--rb-cols "$RB_COLS")
fi
if [[ -n "$RB_W" ]]; then
    RB_ARGS+=(--rb-w "$RB_W")
fi
if [[ -n "$RB_EPS" || -n "$RB_COLS" || -n "$RB_W" || "$RB_LAMBDA" != "40" ]]; then
    RB_ARGS+=(--rb-lambda "$RB_LAMBDA")
fi
if [[ "$THREAD_MODE" == "single" ]]; then
    RB_ARGS+=(--single-thread)
else
    RB_ARGS+=(--multi-thread)
fi
if [[ "$THREADS" != "auto" && "$THREADS" != "0" ]]; then
    RB_ARGS+=(--threads "$THREADS")
fi
RB_ARGS+=(--pi "$PI" --pi-lambda "$PI_LAMBDA")
RB_ARGS_STR="${RB_ARGS[*]:-}"
SIZES_STR="${sizes[*]}"

mkdir -p "$BENCH_DIR"
mkdir -p "$(dirname "$OUTPUT_FILE")"

DETAIL_ROOT="$BENCH_DIR"
if [[ "$DETAIL_MODE" == "dir" ]]; then
    DETAIL_ROOT="$BENCH_DIR/$DETAIL_DIR_NAME"
elif [[ "$DETAIL_MODE" == "none" ]]; then
    DETAIL_ROOT="$BENCH_DIR/.tmp-details"
fi
mkdir -p "$DETAIL_ROOT"

if [[ "${SKIP_RUN:-0}" != "1" ]]; then
    docker run --rm --platform linux/amd64 \
        -v "$REPO_ROOT:/work" \
        -w /work \
        ubuntu:22.04 \
        bash -lc "set -euo pipefail && \
            apt-get update >/tmp/apt-update.log 2>&1 && \
            apt-get install -y --no-install-recommends libboost-system1.74.0 libboost-thread1.74.0 libgmp10 libgf2x3 libntl44 libsodium23 >/tmp/apt-install.log 2>&1 && \
            idx=0; \
            port_step=\$(( (${WARMUPS} + ${ROUNDS} + 2) * 64 + 256 )); \
            for size in ${SIZES_STR}; do \
                out=\"/work/${DETAIL_ROOT#$REPO_ROOT/}/${BENCH_STEM}_set-\${size}_warm-${WARMUPS}_rounds-${ROUNDS}_delay-${SAFE_DELAY}_bw-${SAFE_BW}_${RB_TAG}.md\"; \
                port=\$(( 20000 + idx * port_step )); \
                ./bin/pqpsi_rbokvs_bench \"\$out\" \"\$size\" \"$WARMUPS\" \"$ROUNDS\" \"\$port\" \"$SIM_DELAY_MS\" \"$SIM_BW_MBPS\" ${RB_ARGS_STR}; \
                idx=\$(( idx + 1 )); \
            done"
fi

for size in "${sizes[@]}"; do
    report_file="$DETAIL_ROOT/${BENCH_STEM}_set-${size}_warm-${WARMUPS}_rounds-${ROUNDS}_delay-${SAFE_DELAY}_bw-${SAFE_BW}_${RB_TAG}.md"
    if [[ ! -f "$report_file" ]]; then
        echo "Missing detail report $report_file" >&2
        exit 1
    fi
done

{
    printf "# PQ-PSI RB-OKVS PSI Timing Results\n\n"
    printf "Benchmark date: %s\n\n" "$(date '+%Y-%m-%d %H:%M:%S %Z')"
    printf "Binary: \`./bin/pqpsi_rbokvs_bench\`\n\n"
    printf "Command template: \`./bin/pqpsi_rbokvs_bench <out.md> <setSize> %s %s <portBase> %s %s\`\n\n" "$WARMUPS" "$ROUNDS" "$SIM_DELAY_MS" "$SIM_BW_MBPS"
    printf "RB config: \`lambda=%s eps=%s m=%s w=%s\`\n\n" "${RB_LAMBDA}" "${RB_EPS:-auto}" "${RB_COLS:-auto}" "${RB_W:-auto}"
    printf "Pi config: \`pi=%s lambda=%s\`\n\n" "$PI" "$PI_LAMBDA"
    printf "Thread mode: \`%s\`\n\n" "$THREAD_MODE"
    printf "Worker threads: \`%s\`\n\n" "$THREADS"
    printf "Simulated network: \`%s\`\n\n" "$NETWORK_LABEL"
    printf "Metric definition:\n"
    printf -- "- \`Protocol Avg (ms)\`: \`max(party0_protocol_ms, party1_protocol_ms)\`, excluding connection setup and teardown.\n"
    printf -- "- \`Party0 Protocol Avg (ms)\`: party0 protocol-side time, excluding connection setup and teardown.\n"
    printf -- "- \`Total Comm Avg (KB)\`: mean total over-wire communication from \`total_communication_kb_mean\`\n\n"
    printf "Notes:\n"
    printf -- "- This report uses PQ-PSI's built-in RB-OKVS benchmark with %s warmup and %s measured rounds per size.\n" "$WARMUPS" "$ROUNDS"
    printf -- "- This run uses the current default PQPSI path.\n"
    printf -- "- Simulated network setting for this run: \`%s\`.\n" "$NETWORK_LABEL"
    printf -- "- Set sizes here are per party: \`%s\`.\n" "$SIZES"
    if [[ "$DETAIL_MODE" == "none" ]]; then
        printf -- "- Detailed per-size benchmark reports are not kept after summary generation.\n\n"
    else
        printf -- "- Detailed per-size benchmark reports are saved under \`%s\`.\n\n" "$DETAIL_ROOT"
    fi
    printf "## RB Params\n\n"
    printf "| Set Size | eps | w | m | lambda_real | workers |\n"
    printf "| --- | ---: | ---: | ---: | ---: | ---: |\n"
} > "$OUTPUT_FILE"

param_rows_file="$(mktemp)"
pi_rows_file="$(mktemp)"
perf_rows_file="$(mktemp)"
cleanup_rows() {
    rm -f "$param_rows_file" "$pi_rows_file" "$perf_rows_file"
}
trap cleanup_rows EXIT

for i in "${!sizes[@]}"; do
    size="${sizes[$i]}"
    power="${powers[$i]}"
    report_file="$DETAIL_ROOT/${BENCH_STEM}_set-${size}_warm-${WARMUPS}_rounds-${ROUNDS}_delay-${SAFE_DELAY}_bw-${SAFE_BW}_${RB_TAG}.md"
    total_comm_avg="$(summary_value "$report_file" "total_communication_kb_mean")"
    protocol_avg="$(summary_value "$report_file" "protocol_runtime_mean_ms")"
    party0_avg="$(summary_value "$report_file" "party0_protocol_mean_ms")"
    rb_eps="$(summary_value "$report_file" "rb_eps")"
    rb_m="$(summary_value "$report_file" "rb_m")"
    rb_w="$(summary_value "$report_file" "rb_w")"
    rb_lambda_real="$(summary_value "$report_file" "rb_lambda_real")"
    worker_threads="$(summary_value "$report_file" "worker_threads")"
    pi_kind="$(summary_value "$report_file" "pi")"
    pi_detail="$(summary_value "$report_file" "pi_detail")"
    pi_n="$(summary_value "$report_file" "pi_n")"
    pi_s="$(summary_value "$report_file" "pi_s")"
    pi_lambda="$(summary_value "$report_file" "pi_lambda")"
    pi_rounds="$(summary_value "$report_file" "pi_rounds")"

    {
        printf "| 2^%s | %s | %s | %s | %s | %s |\n" "$power" "$rb_eps" "$rb_w" "$rb_m" "$rb_lambda_real" "$worker_threads"
    } >> "$param_rows_file"
    {
        printf "| 2^%s | %s | %s | %s | %s | %s | %s |\n" "$power" "$pi_kind" "$pi_detail" "$pi_n" "$pi_s" "$pi_lambda" "$pi_rounds"
    } >> "$pi_rows_file"
    {
        printf "| 2^%s | %s | %s | %s |\n" "$power" "$protocol_avg" "$party0_avg" "$total_comm_avg"
    } >> "$perf_rows_file"
done

cat "$param_rows_file" >> "$OUTPUT_FILE"

printf "\n" >> "$OUTPUT_FILE"
printf "## Pi Params\n\n" >> "$OUTPUT_FILE"
printf "| Set Size | pi | detail | n | s | lambda | rounds |\n" >> "$OUTPUT_FILE"
printf "| --- | --- | --- | ---: | ---: | ---: | ---: |\n" >> "$OUTPUT_FILE"
cat "$pi_rows_file" >> "$OUTPUT_FILE"

printf "\n" >> "$OUTPUT_FILE"
printf "## Performance\n\n" >> "$OUTPUT_FILE"
printf "| Set Size | Protocol Avg (ms) | Party0 Protocol Avg (ms) | Total Comm Avg (KB) |\n" >> "$OUTPUT_FILE"
printf "| --- | ---: | ---: | ---: |\n" >> "$OUTPUT_FILE"
cat "$perf_rows_file" >> "$OUTPUT_FILE"
printf "\n" >> "$OUTPUT_FILE"
for i in "${!sizes[@]}"; do
    size="${sizes[$i]}"
    power="${powers[$i]}"
    report_file="$DETAIL_ROOT/${BENCH_STEM}_set-${size}_warm-${WARMUPS}_rounds-${ROUNDS}_delay-${SAFE_DELAY}_bw-${SAFE_BW}_${RB_TAG}.md"
    protocol_avg="$(summary_value "$report_file" "protocol_runtime_mean_ms")"
    total_comm_avg="$(summary_value "$report_file" "total_communication_kb_mean")"
    party0_avg="$(summary_value "$report_file" "party0_protocol_mean_ms")"
    party1_avg="$(summary_value "$report_file" "party1_protocol_mean_ms")"

    {
        printf "## Set Size 2^%s\n\n" "$power"
        printf "| Run | Protocol Runtime (ms) | Party0 Protocol (ms) | Party1 Protocol (ms) | Total Comm (KB) |\n"
        printf "| --- | ---: | ---: | ---: | ---: |\n"
        printf "| Avg | %s | %s | %s | %s |\n" "$protocol_avg" "$party0_avg" "$party1_avg" "$total_comm_avg"
        append_round_rows_protocol "$report_file" | awk -F'|' '
            {
                round = $2
                protocol = $3
                party0 = $4
                party1 = $5
                comm = $6
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", round)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", protocol)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", party0)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", party1)
                gsub(/^[[:space:]]+|[[:space:]]+$/, "", comm)
                printf "| %s | %s | %s | %s | %s |\n", round, protocol, party0, party1, comm
            }
        '
        printf "\n"
        if [[ "$DETAIL_MODE" != "none" ]]; then
            printf "Detailed report: [%s](%s)\n\n" "$(basename "$report_file")" "$report_file"
        fi
    } >> "$OUTPUT_FILE"
done

if [[ "$DETAIL_MODE" == "none" ]]; then
    rm -f "$DETAIL_ROOT"/${BENCH_STEM}_set-*_warm-${WARMUPS}_rounds-${ROUNDS}_delay-${SAFE_DELAY}_bw-${SAFE_BW}_${RB_TAG}.md
fi

echo "Wrote benchmark report to $OUTPUT_FILE"
