#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C
export LANG=C

usage() {
    cat <<'EOF'
Usage
  bash script/benchmark-docker-net-psi.sh [out.md]

Examples
  bash script/benchmark-docker-net-psi.sh
  RATE=50mbit DELAY=20ms WARMUPS=0 ROUNDS=1 POWERS="7" bash script/benchmark-docker-net-psi.sh
  RATE=10gbit DELAY=1ms THREAD_MODE=multi THREADS=4 POWERS="10 12" bash script/benchmark-docker-net-psi.sh my-report.md

Defaults
  POWERS="7 8 10"
  WARMUPS=0
  ROUNDS=3
  THREAD_MODE=single
  THREADS=4
  RATE=50mbit
  DELAY=20ms
  JITTER=""
  LOSS=""
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
POWERS="${POWERS:-7 8 10}"
WARMUPS="${WARMUPS:-0}"
ROUNDS="${ROUNDS:-3}"
THREAD_MODE="${THREAD_MODE:-single}"
THREADS="${THREADS:-4}"
RATE="${RATE:-50mbit}"
DELAY="${DELAY:-20ms}"
JITTER="${JITTER:-}"
LOSS="${LOSS:-}"
EXTRA_ARGS="${EXTRA_ARGS:-}"
PORT_BASE_START="${PORT_BASE_START:-42000}"
SAFE_POWERS="$(printf '%s' "$POWERS" | tr ' ' '-')"

if [[ "$THREAD_MODE" == "single" ]]; then
    NT=1
elif [[ "$THREAD_MODE" == "multi" ]]; then
    NT="$THREADS"
else
    echo "THREAD_MODE must be single or multi, got: $THREAD_MODE" >&2
    exit 1
fi

if ! [[ "$WARMUPS" =~ ^[0-9]+$ && "$ROUNDS" =~ ^[0-9]+$ && "$NT" =~ ^[0-9]+$ && "$PORT_BASE_START" =~ ^[0-9]+$ ]]; then
    echo "WARMUPS, ROUNDS, NT, and PORT_BASE_START must be non-negative integers" >&2
    exit 1
fi

if (( NT < 1 )); then
    echo "NT must be at least 1" >&2
    exit 1
fi

TOTAL_TRIALS=$(( WARMUPS + ROUNDS ))
OUTPUT_FILE="${1:-$REPO_ROOT/benchmark-results-net-psi-${THREAD_MODE}-nt${NT}-${RATE}-${DELAY}-${SAFE_POWERS}-$(date +%Y-%m-%d).md}"
if [[ "$OUTPUT_FILE" != */* && "$OUTPUT_FILE" != "" ]]; then
    OUTPUT_FILE="$REPO_ROOT/$OUTPUT_FILE"
fi
mkdir -p "$(dirname "$OUTPUT_FILE")"

read -r -a powers <<< "$POWERS"

platform_args=()
if [[ -n "$DOCKER_PLATFORM" ]]; then
    platform_args=(--platform "$DOCKER_PLATFORM")
fi

netem_cmd="tc qdisc replace dev eth0 root netem rate ${RATE} delay ${DELAY}"
if [[ -n "$JITTER" ]]; then
    netem_cmd="${netem_cmd} ${JITTER} distribution normal"
fi
if [[ -n "$LOSS" ]]; then
    netem_cmd="${netem_cmd} loss ${LOSS}"
fi
ensure_tc_cmd='command -v tc >/dev/null 2>&1 || (apt-get update >/dev/null && apt-get install -y --no-install-recommends iproute2 >/dev/null)'

extract_metric() {
    local key="$1"
    local file="$2"
    awk -v key="$key" '$1 == key { print $2 }' "$file" | tail -n 1
}

avg_values() {
    python3 -c '
import statistics
import sys

vals = [float(line.strip()) for line in sys.stdin if line.strip()]
if not vals:
    raise SystemExit("no values provided")
print(f"{statistics.mean(vals):.3f}")
'
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

cleanup_run() {
    local recv_name="${1:-}"
    local send_name="${2:-}"
    local net_name="${3:-}"

    if [[ -n "$send_name" ]]; then
        docker rm -f "$send_name" >/dev/null 2>&1 || true
    fi
    if [[ -n "$recv_name" ]]; then
        docker rm -f "$recv_name" >/dev/null 2>&1 || true
    fi
    if [[ -n "$net_name" ]]; then
        docker network rm "$net_name" >/dev/null 2>&1 || true
    fi
}

{
    printf "# VOLE-PSI Network-Simulated Benchmark Results\n\n"
    printf "Benchmark date: %s\n\n" "$(date '+%Y-%m-%d %H:%M:%S %Z')"
    printf "Command pattern: \`frontend -net -sender/-recver -senderSize <n> -receiverSize <n> -nt %s %s\`\n\n" "$NT" "$EXTRA_ARGS"
    printf "Thread mode: \`%s\`\n\n" "$THREAD_MODE"
    printf "Threads passed to \`-nt\`: \`%s\`\n\n" "$NT"
    printf "Warmups dropped: \`%s\`\n\n" "$WARMUPS"
    printf "Measured rounds kept: \`%s\`\n\n" "$ROUNDS"
    printf "Rate: \`%s\`\n\n" "$RATE"
    printf "Delay: \`%s\`\n\n" "$DELAY"
    if [[ -n "$JITTER" ]]; then
        printf "Jitter: \`%s\`\n\n" "$JITTER"
    fi
    if [[ -n "$LOSS" ]]; then
        printf "Loss: \`%s\`\n\n" "$LOSS"
    fi
    printf "Notes:\n"
    printf -- "- Each round launches two separate containers on a dedicated Docker bridge network.\n"
    printf -- "- Network shaping is applied symmetrically on both containers with \`tc netem\` on \`eth0\`.\n"
    printf -- "- Reported round time is \`max(sender_time_ms, receiver_time_ms)\` from the protocol-side logs, not container startup time.\n"
    printf -- "- \`Comm Avg (bytes)\` is the mean of \`sender bytes_sent + receiver bytes_sent\` across measured runs.\n\n"
    printf "## Summary\n\n"
    printf "| Set Size | Thread Mode | \`-nt\` | Rate | Delay | Measured Trials (ms) | Mean (ms) | Median (ms) | Comm Avg (bytes) |\n"
    printf "| --- | --- | ---: | --- | --- | --- | ---: | ---: | ---: |\n"
} > "$OUTPUT_FILE"

for power in "${powers[@]}"; do
    if ! [[ "$power" =~ ^[0-9]+$ ]]; then
        echo "Power must be an integer, got: $power" >&2
        exit 1
    fi

    set_size=$(( 1 << power ))
    all_trial_times_file="$(mktemp)"
    measured_trial_times_file="$(mktemp)"
    measured_comm_file="$(mktemp)"
    raw_log_file="$(mktemp)"

    for run_idx in $(seq 1 "$TOTAL_TRIALS"); do
        port=$(( PORT_BASE_START + power * 100 + run_idx ))
        net_name="volepsi-net-$power-$run_idx-$$"
        recv_name="volepsi-recv-$power-$run_idx-$$"
        send_name="volepsi-send-$power-$run_idx-$$"
        recv_log="$(mktemp)"
        send_log="$(mktemp)"

        cleanup_run "" "" "$net_name"
        docker network create "$net_name" >/dev/null

        recv_cmd="set -euo pipefail; ${ensure_tc_cmd}; ${netem_cmd}; ./out/build/linux/frontend/frontend -net -recver -r 1 -server 1 -ip 0.0.0.0:${port} -senderSize ${set_size} -receiverSize ${set_size} -nt ${NT}"
        send_cmd="set -euo pipefail; ${ensure_tc_cmd}; ${netem_cmd}; sleep 1; ./out/build/linux/frontend/frontend -net -sender -r 0 -server 0 -ip ${recv_name}:${port} -senderSize ${set_size} -receiverSize ${set_size} -nt ${NT}"

        if [[ -n "$EXTRA_ARGS" ]]; then
            recv_cmd="${recv_cmd} ${EXTRA_ARGS}"
            send_cmd="${send_cmd} ${EXTRA_ARGS}"
        fi

        recv_id="$(docker run -d --name "$recv_name" --hostname "$recv_name" --network "$net_name" --cap-add NET_ADMIN "${platform_args[@]}" "$IMAGE_NAME" bash -lc "$recv_cmd")"
        send_id=""
        recv_status=""
        send_status=""

        set +e
        send_id="$(docker run -d --name "$send_name" --hostname "$send_name" --network "$net_name" --cap-add NET_ADMIN "${platform_args[@]}" "$IMAGE_NAME" bash -lc "$send_cmd")"
        send_status="$(docker wait "$send_id" 2>/dev/null | tail -n 1)"
        recv_status="$(docker wait "$recv_id" 2>/dev/null | tail -n 1)"
        docker logs "$send_id" > "$send_log" 2>&1 || true
        docker logs "$recv_id" > "$recv_log" 2>&1 || true
        set -e

        phase="measure"
        if (( run_idx <= WARMUPS )); then
            phase="warmup"
        fi

        if [[ "$send_status" != "0" || "$recv_status" != "0" ]]; then
            {
                printf "### Run %s (%s)\n\n" "$run_idx" "$phase"
                printf "Sender status: \`%s\`\n\n" "${send_status:-unknown}"
                printf "Receiver status: \`%s\`\n\n" "${recv_status:-unknown}"
                printf "#### Sender Log\n\n"
                printf '```text\n%s\n```\n\n' "$(cat "$send_log")"
                printf "#### Receiver Log\n\n"
                printf '```text\n%s\n```\n\n' "$(cat "$recv_log")"
            } >> "$raw_log_file"
            cleanup_run "$recv_name" "$send_name" "$net_name"
            echo "Network benchmark run failed for 2^$power / run $run_idx" >&2
            exit 1
        fi

        sender_time_ms="$(extract_metric result_time_ms "$send_log")"
        receiver_time_ms="$(extract_metric result_time_ms "$recv_log")"
        sender_bytes_sent="$(extract_metric result_bytes_sent "$send_log")"
        receiver_bytes_sent="$(extract_metric result_bytes_sent "$recv_log")"

        if [[ -z "$sender_time_ms" || -z "$receiver_time_ms" || -z "$sender_bytes_sent" || -z "$receiver_bytes_sent" ]]; then
            {
                printf "### Run %s (%s)\n\n" "$run_idx" "$phase"
                printf "#### Sender Log\n\n"
                printf '```text\n%s\n```\n\n' "$(cat "$send_log")"
                printf "#### Receiver Log\n\n"
                printf '```text\n%s\n```\n\n' "$(cat "$recv_log")"
            } >> "$raw_log_file"
            cleanup_run "$recv_name" "$send_name" "$net_name"
            echo "Failed to parse run metrics for 2^$power / run $run_idx" >&2
            exit 1
        fi

        round_time_ms="$(python3 -c 'import sys; a=float(sys.argv[1]); b=float(sys.argv[2]); print(f"{max(a,b):.3f}")' "$sender_time_ms" "$receiver_time_ms")"
        round_comm_bytes="$(python3 -c 'import sys; a=int(sys.argv[1]); b=int(sys.argv[2]); print(a + b)' "$sender_bytes_sent" "$receiver_bytes_sent")"

        printf '%s\n' "$round_time_ms" >> "$all_trial_times_file"
        if (( run_idx > WARMUPS )); then
            printf '%s\n' "$round_time_ms" >> "$measured_trial_times_file"
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
            printf '```text\n%s\n```\n\n' "$(cat "$send_log")"
            printf "#### Receiver Log\n\n"
            printf '```text\n%s\n```\n\n' "$(cat "$recv_log")"
        } >> "$raw_log_file"

        cleanup_run "$recv_name" "$send_name" "$net_name"
        rm -f "$recv_log" "$send_log"
    done

    stats_text="$(cat "$all_trial_times_file" | summarize_trials "$WARMUPS")"
    measured_trials="$(printf '%s\n' "$stats_text" | sed -n '1p')"
    mean_ms="$(printf '%s\n' "$stats_text" | sed -n '2p')"
    median_ms="$(printf '%s\n' "$stats_text" | sed -n '3p')"
    comm_avg="$(cat "$measured_comm_file" | avg_values)"

    {
        printf "| \`2^%s = %s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` |\n" \
            "$power" "$set_size" "$THREAD_MODE" "$NT" "$RATE" "$DELAY" "$measured_trials" "$mean_ms" "$median_ms" "$comm_avg"
        printf "\n## Set Size \`2^%s = %s\`\n\n" "$power" "$set_size"
        printf "| Field | Value |\n"
        printf "| --- | --- |\n"
        printf "| Thread mode | \`%s\` |\n" "$THREAD_MODE"
        printf "| \`-nt\` | \`%s\` |\n" "$NT"
        printf "| Rate | \`%s\` |\n" "$RATE"
        printf "| Delay | \`%s\` |\n" "$DELAY"
        if [[ -n "$JITTER" ]]; then
            printf "| Jitter | \`%s\` |\n" "$JITTER"
        fi
        if [[ -n "$LOSS" ]]; then
            printf "| Loss | \`%s\` |\n" "$LOSS"
        fi
        printf "| Warmups dropped | \`%s\` |\n" "$WARMUPS"
        printf "| Measured rounds | \`%s\` |\n" "$ROUNDS"
        printf "| Measured trials (ms) | \`%s\` |\n" "$measured_trials"
        printf "| Mean (ms) | \`%s\` |\n" "$mean_ms"
        printf "| Median (ms) | \`%s\` |\n" "$median_ms"
        printf "| Comm Avg (bytes) | \`%s\` |\n\n" "$comm_avg"
        cat "$raw_log_file"
    } >> "$OUTPUT_FILE"

    rm -f "$all_trial_times_file" "$measured_trial_times_file" "$measured_comm_file" "$raw_log_file"
done

printf "Wrote %s\n" "$OUTPUT_FILE"
