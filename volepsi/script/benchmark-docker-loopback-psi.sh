#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C
export LANG=C

usage() {
    cat <<'EOF'
Usage
  bash script/benchmark-docker-loopback-psi.sh [out.md]

Examples
  bash script/benchmark-docker-loopback-psi.sh
  DOCKER_PLATFORM=linux/amd64 POWERS="7 8 9 10 11" WARMUPS=2 ROUNDS=10 bash script/benchmark-docker-loopback-psi.sh
  DOCKER_PLATFORM=linux/amd64 RATE=200mbit RTT=80ms POWERS="7 8 9 10 11" bash script/benchmark-docker-loopback-psi.sh my-report.md

Defaults
  POWERS="7 8 9 10 11"
  WARMUPS=2
  ROUNDS=10
  THREAD_MODE=single
  THREADS=4
  MAX_ATTEMPTS=3
  RATE=""
  DELAY=""
  RTT=""
  EXTRA_ARGS=""
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE_NAME="${IMAGE_NAME:-vole-psi-linux}"
DOCKER_PLATFORM="${DOCKER_PLATFORM:-}"
POWERS="${POWERS:-7 8 9 10 11}"
WARMUPS="${WARMUPS:-2}"
ROUNDS="${ROUNDS:-10}"
THREAD_MODE="${THREAD_MODE:-single}"
THREADS="${THREADS:-4}"
MAX_ATTEMPTS="${MAX_ATTEMPTS:-3}"
RATE="${RATE:-}"
RTT="${RTT:-}"
DELAY="${DELAY:-}"
EXTRA_ARGS="${EXTRA_ARGS:-}"
PORT_BASE_START="${PORT_BASE_START:-43000}"
RUN_TIMEOUT="${RUN_TIMEOUT:-600}"
SAFE_POWERS="$(printf '%s' "$POWERS" | tr ' ' '-')"

if [[ "$THREAD_MODE" == "single" ]]; then
    NT=1
elif [[ "$THREAD_MODE" == "multi" ]]; then
    NT="$THREADS"
else
    echo "THREAD_MODE must be single or multi, got: $THREAD_MODE" >&2
    exit 1
fi

if ! [[ "$WARMUPS" =~ ^[0-9]+$ && "$ROUNDS" =~ ^[0-9]+$ && "$NT" =~ ^[0-9]+$ && "$PORT_BASE_START" =~ ^[0-9]+$ && "$RUN_TIMEOUT" =~ ^[0-9]+$ && "$MAX_ATTEMPTS" =~ ^[0-9]+$ ]]; then
    echo "WARMUPS, ROUNDS, NT, PORT_BASE_START, RUN_TIMEOUT, and MAX_ATTEMPTS must be non-negative integers" >&2
    exit 1
fi

if (( NT < 1 )); then
    echo "NT must be at least 1" >&2
    exit 1
fi

TOTAL_TRIALS=$(( WARMUPS + ROUNDS ))
PROFILE_LABEL="lan"
if [[ -n "$RATE" || -n "$DELAY" ]]; then
    PROFILE_LABEL="simulated"
fi

OUTPUT_FILE="${1:-$REPO_ROOT/benchmark-results-loopback-psi-${THREAD_MODE}-nt${NT}-${PROFILE_LABEL}-${SAFE_POWERS}-$(date +%Y-%m-%d).md}"
if [[ "$OUTPUT_FILE" != */* && "$OUTPUT_FILE" != "" ]]; then
    OUTPUT_FILE="$REPO_ROOT/$OUTPUT_FILE"
fi
mkdir -p "$(dirname "$OUTPUT_FILE")"

read -r -a powers <<< "$POWERS"

platform_args=()
if [[ -n "$DOCKER_PLATFORM" ]]; then
    platform_args=(--platform "$DOCKER_PLATFORM")
fi

parse_delay_ms() {
    python3 - <<'PY' "$1"
import re
import sys

value = sys.argv[1].strip()
m = re.fullmatch(r'([0-9]+(?:\.[0-9]+)?)(ms|s)', value)
if not m:
    raise SystemExit(f"unsupported delay format: {value}")
num = float(m.group(1))
unit = m.group(2)
if unit == "s":
    num *= 1000.0
print(f"{num:.3f}ms")
PY
}

applied_delay=""
target_rtt=""
if [[ -n "$RTT" && -n "$DELAY" ]]; then
    echo "Specify at most one of RTT or DELAY" >&2
    exit 1
fi

if [[ -n "$RTT" ]]; then
    target_rtt="$(parse_delay_ms "$RTT")"
    applied_delay="$(python3 - <<'PY' "$RTT"
import re
import sys

value = sys.argv[1].strip()
m = re.fullmatch(r'([0-9]+(?:\.[0-9]+)?)(ms|s)', value)
if not m:
    raise SystemExit(f"unsupported RTT format: {value}")
num = float(m.group(1))
unit = m.group(2)
if unit == "s":
    num *= 1000.0
print(f"{num / 2.0:.3f}ms")
PY
)"
elif [[ -n "$DELAY" ]]; then
    applied_delay="$(parse_delay_ms "$DELAY")"
fi

tc_shape_cmd=""
if [[ -n "$RATE" || -n "$applied_delay" ]]; then
    tc_shape_cmd="tc qdisc replace dev lo root netem"
    if [[ -n "$applied_delay" ]]; then
        tc_shape_cmd="${tc_shape_cmd} delay ${applied_delay}"
    fi
    if [[ -n "$RATE" ]]; then
        tc_shape_cmd="${tc_shape_cmd} rate ${RATE}"
    fi
fi

tc_apply_cmd=":"
if [[ -n "$tc_shape_cmd" ]]; then
    tc_apply_cmd="$tc_shape_cmd"
fi

extract_metric() {
    local key="$1"
    local file="$2"
    awk -v key="$key" '$1 == key { print $2 }' "$file" | tail -n 1
}

extract_section() {
    local begin="$1"
    local end="$2"
    local file="$3"
    awk -v begin="$begin" -v end="$end" '
        $0 == begin { flag = 1; next }
        $0 == end { flag = 0; exit }
        flag { print }
    ' "$file"
}

summarize_trials() {
    local warmups="$1"
    python3 -c '
import statistics
import sys

warmups = int(sys.argv[1])
vals = [float(line.strip()) for line in sys.stdin if line.strip()]
if len(vals) <= warmups:
    raise SystemExit("not enough trial points after warmup split")
measured = vals[warmups:]
print(",".join(f"{v:.3f}" for v in measured))
print(f"{statistics.mean(measured):.3f}")
print(f"{statistics.median(measured):.3f}")
' "$warmups"
}

{
    printf "# VOLE-PSI Loopback Benchmark Results\n\n"
    printf "Benchmark date: %s\n\n" "$(date '+%Y-%m-%d %H:%M:%S %Z')"
    printf "Command pattern: \`frontend -net -sender/-recver -senderSize <n> -receiverSize <n> -nt %s %s\`\n\n" "$NT" "$EXTRA_ARGS"
    printf "Thread mode: \`%s\`\n\n" "$THREAD_MODE"
    printf "Threads passed to \`-nt\`: \`%s\`\n\n" "$NT"
    printf "Warmups dropped: \`%s\`\n\n" "$WARMUPS"
    printf "Measured rounds kept: \`%s\`\n\n" "$ROUNDS"
    printf "Profile: \`%s\`\n\n" "$PROFILE_LABEL"
    if [[ -n "$RATE" ]]; then
        printf "Bandwidth cap: \`%s\`\n\n" "$RATE"
    else
        printf "Bandwidth cap: \`none\`\n\n"
    fi
    if [[ -n "$target_rtt" ]]; then
        printf "Target RTT on \`lo\`: \`%s\`\n\n" "$target_rtt"
    else
        printf "Target RTT on \`lo\`: \`none\`\n\n"
    fi
    if [[ -n "$applied_delay" ]]; then
        printf "Applied one-way delay on \`lo\`: \`%s\`\n\n" "$applied_delay"
    else
        printf "Applied one-way delay on \`lo\`: \`none\`\n\n"
    fi
    printf "Notes:\n"
    printf -- "- Each round launches one Linux Docker container and runs receiver and sender as two separate frontend processes inside it.\n"
    printf -- "- This matches the single-container benchmarking style used by the current MiniPSI docker benchmark more closely than the earlier two-container netem setup.\n"
    if [[ -n "$tc_shape_cmd" ]]; then
        printf -- "- Loopback traffic is shaped on \`lo\` with \`tc netem\` using \`%s\`.\n" "$tc_shape_cmd"
    else
        printf -- "- No network shaping is applied; this serves as the LAN-style docker baseline.\n"
    fi
    printf -- "- Reported round time is \`max(sender_time_ms, receiver_time_ms)\` from the protocol-side logs, not Docker startup time.\n"
    printf -- "- \`Comm Avg (bytes)\` is the mean of \`sender bytes_sent + receiver bytes_sent\` across measured runs.\n\n"
    printf "## Summary\n\n"
    printf "| Set Size | Thread Mode | Mean (ms) | Comm Avg (bytes) |\n"
    printf "| --- | --- | ---: | ---: |\n"
} > "$OUTPUT_FILE"

summary_rows_file="$(mktemp)"
details_file="$(mktemp)"

for power in "${powers[@]}"; do
    if ! [[ "$power" =~ ^[0-9]+$ ]]; then
        echo "Power must be an integer, got: $power" >&2
        exit 1
    fi

    set_size=$(( 1 << power ))
    all_trial_times_file="$(mktemp)"
    measured_comm_file="$(mktemp)"
    raw_log_file="$(mktemp)"

    for run_idx in $(seq 1 "$TOTAL_TRIALS"); do
        port=$(( PORT_BASE_START + power * 100 + run_idx ))
        sender_time_ms=""
        receiver_time_ms=""
        sender_bytes_sent=""
        receiver_bytes_sent=""
        run_status=1
        sender_status=""
        receiver_status=""
        attempt_combined_log=""

        for attempt_idx in $(seq 1 "$MAX_ATTEMPTS"); do
            combined_log="$(mktemp)"
            sender_log="$(mktemp)"
            recv_log="$(mktemp)"

            container_script="$(cat <<EOF
set -euo pipefail
sender_log=\$(mktemp)
recv_log=\$(mktemp)
cleanup() {
    rm -f "\$sender_log" "\$recv_log"
}
trap cleanup EXIT
if ! command -v tc >/dev/null 2>&1; then
    apt-get update >/dev/null
    apt-get install -y --no-install-recommends iproute2 >/dev/null
fi
${tc_apply_cmd}
recv_rc=0
./out/build/linux/frontend/frontend -net -recver -r 1 -server 1 -ip 127.0.0.1:${port} -senderSize ${set_size} -receiverSize ${set_size} -nt ${NT} ${EXTRA_ARGS} >"\$recv_log" 2>&1 &
recv_pid=\$!
sleep 1
sender_rc=0
timeout ${RUN_TIMEOUT} ./out/build/linux/frontend/frontend -net -sender -r 0 -server 0 -ip 127.0.0.1:${port} -senderSize ${set_size} -receiverSize ${set_size} -nt ${NT} ${EXTRA_ARGS} >"\$sender_log" 2>&1 || sender_rc=\$?
wait "\$recv_pid" || recv_rc=\$?
printf 'sender_status %s\n' "\$sender_rc"
printf 'receiver_status %s\n' "\$recv_rc"
printf '__SENDER_LOG_BEGIN__\n'
cat "\$sender_log"
printf '__SENDER_LOG_END__\n'
printf '__RECV_LOG_BEGIN__\n'
cat "\$recv_log"
printf '__RECV_LOG_END__\n'
if [[ "\$sender_rc" != "0" || "\$recv_rc" != "0" ]]; then
    exit 1
fi
EOF
)"

            set +e
            docker run --rm --cap-add NET_ADMIN "${platform_args[@]}" "$IMAGE_NAME" bash -lc "$container_script" > "$combined_log" 2>&1
            run_status=$?
            set -e

            extract_section "__SENDER_LOG_BEGIN__" "__SENDER_LOG_END__" "$combined_log" > "$sender_log"
            extract_section "__RECV_LOG_BEGIN__" "__RECV_LOG_END__" "$combined_log" > "$recv_log"
            sender_status="$(extract_metric sender_status "$combined_log")"
            receiver_status="$(extract_metric receiver_status "$combined_log")"
            sender_time_ms="$(extract_metric result_time_ms "$sender_log")"
            receiver_time_ms="$(extract_metric result_time_ms "$recv_log")"
            sender_bytes_sent="$(extract_metric result_bytes_sent "$sender_log")"
            receiver_bytes_sent="$(extract_metric result_bytes_sent "$recv_log")"

            if [[ "$run_status" == "0" && "${sender_status:-1}" == "0" && "${receiver_status:-1}" == "0" \
                && -n "$sender_time_ms" && -n "$receiver_time_ms" && -n "$sender_bytes_sent" && -n "$receiver_bytes_sent" ]]; then
                attempt_combined_log="$combined_log"
                break
            fi

            attempt_combined_log="$combined_log"
            rm -f "$sender_log" "$recv_log"
        done

        phase="measure"
        if (( run_idx <= WARMUPS )); then
            phase="warmup"
        fi

        if [[ "$run_status" != "0" || "${sender_status:-1}" != "0" || "${receiver_status:-1}" != "0" \
            || -z "$sender_time_ms" || -z "$receiver_time_ms" || -z "$sender_bytes_sent" || -z "$receiver_bytes_sent" ]]; then
            {
                printf "### Run %s (%s)\n\n" "$run_idx" "$phase"
                printf "Attempts used: \`%s\`\n\n" "$MAX_ATTEMPTS"
                printf "Docker exit status: \`%s\`\n\n" "$run_status"
                printf "Sender status: \`%s\`\n\n" "${sender_status:-unknown}"
                printf "Receiver status: \`%s\`\n\n" "${receiver_status:-unknown}"
                printf "#### Combined Log\n\n"
                printf '```text\n%s\n```\n\n' "$(cat "$attempt_combined_log")"
            } >> "$raw_log_file"
            echo "Loopback benchmark run failed for 2^$power / run $run_idx" >&2
            exit 1
        fi

        round_time_ms="$(python3 -c 'import sys; a=float(sys.argv[1]); b=float(sys.argv[2]); print(f"{max(a,b):.3f}")' "$sender_time_ms" "$receiver_time_ms")"
        round_comm_bytes="$(python3 -c 'import sys; a=int(sys.argv[1]); b=int(sys.argv[2]); print(a + b)' "$sender_bytes_sent" "$receiver_bytes_sent")"

        printf '%s\n' "$round_time_ms" >> "$all_trial_times_file"
        if (( run_idx > WARMUPS )); then
            printf '%s\n' "$round_comm_bytes" >> "$measured_comm_file"
        fi

        {
            printf "### Run %s (%s)\n\n" "$run_idx" "$phase"
            printf "| Field | Value |\n"
            printf "| --- | --- |\n"
            printf "| Round time (ms) | \`%s\` |\n" "$round_time_ms"
            printf "| Comm (bytes) | \`%s\` |\n" "$round_comm_bytes"
            printf "| Sender time (ms) | \`%s\` |\n" "$sender_time_ms"
            printf "| Receiver time (ms) | \`%s\` |\n" "$receiver_time_ms"
            printf "| Sender bytes sent | \`%s\` |\n" "$sender_bytes_sent"
            printf "| Receiver bytes sent | \`%s\` |\n\n" "$receiver_bytes_sent"
            printf "#### Sender Log\n\n"
            printf '```text\n%s\n```\n\n' "$(cat "$sender_log")"
            printf "#### Receiver Log\n\n"
            printf '```text\n%s\n```\n\n' "$(cat "$recv_log")"
        } >> "$raw_log_file"

        rm -f "$attempt_combined_log" "$sender_log" "$recv_log"
    done

    summary_tmp="$(mktemp)"
    summarize_trials "$WARMUPS" < "$all_trial_times_file" > "$summary_tmp"
    measured_trials="$(sed -n '1p' "$summary_tmp")"
    mean_time_ms="$(sed -n '2p' "$summary_tmp")"
    median_time_ms="$(sed -n '3p' "$summary_tmp")"
    mean_comm_bytes="$(python3 -c 'import statistics,sys; vals=[float(line.strip()) for line in sys.stdin if line.strip()]; print(f"{statistics.mean(vals):.3f}")' < "$measured_comm_file")"

    {
        printf "| \`2^%s = %s\` | \`%s\` | \`%s\` | \`%s\` |\n" \
            "$power" "$set_size" "$THREAD_MODE" "$mean_time_ms" "$mean_comm_bytes"
    } >> "$summary_rows_file"

    {
        printf "\n## Set Size \`2^%s = %s\`\n\n" "$power" "$set_size"
        printf "| Field | Value |\n"
        printf "| --- | --- |\n"
        printf "| Thread mode | \`%s\` |\n" "$THREAD_MODE"
        printf "| \`-nt\` | \`%s\` |\n" "$NT"
        printf "| Warmups dropped | \`%s\` |\n" "$WARMUPS"
        printf "| Measured rounds | \`%s\` |\n" "$ROUNDS"
        printf "| Measured trials (ms) | \`%s\` |\n" "$measured_trials"
        printf "| Mean (ms) | \`%s\` |\n" "$mean_time_ms"
        printf "| Median (ms) | \`%s\` |\n" "$median_time_ms"
        printf "| Comm Avg (bytes) | \`%s\` |\n\n" "$mean_comm_bytes"
        cat "$raw_log_file"
    } >> "$details_file"

    rm -f "$all_trial_times_file" "$measured_comm_file" "$raw_log_file" "$summary_tmp"
done

cat "$summary_rows_file" >> "$OUTPUT_FILE"
cat "$details_file" >> "$OUTPUT_FILE"

rm -f "$summary_rows_file" "$details_file"

printf "Wrote %s\n" "$OUTPUT_FILE"
