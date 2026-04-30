#!/usr/bin/env bash

set -euo pipefail

export LC_ALL=C
export LANG=C

usage() {
    cat <<'EOF'
Usage
  bash script/benchmark-docker-perf-psi.sh [out.md]

Examples
  bash script/benchmark-docker-perf-psi.sh
  RUN_MODE=independent THREAD_MODE=single WARMUPS=2 ROUNDS=8 bash script/benchmark-docker-perf-psi.sh
  RUN_MODE=independent THREAD_MODE=multi THREADS=4 WARMUPS=2 ROUNDS=8 bash script/benchmark-docker-perf-psi.sh
  RUN_MODE=looped THREAD_MODE=single WARMUPS=2 ROUNDS=8 bash script/benchmark-docker-perf-psi.sh
  POWERS="7 8 10" THREAD_MODE=multi THREADS=8 bash script/benchmark-docker-perf-psi.sh my-report.md

Defaults
  POWERS="7 8 10"
  WARMUPS=2
  ROUNDS=8
  RUN_MODE=independent
  THREAD_MODE=single
  THREADS=4
  EXTRA_ARGS=""
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
POWERS="${POWERS:-7 8 10}"
WARMUPS="${WARMUPS:-2}"
ROUNDS="${ROUNDS:-8}"
RUN_MODE="${RUN_MODE:-independent}"
THREAD_MODE="${THREAD_MODE:-single}"
THREADS="${THREADS:-4}"
EXTRA_ARGS="${EXTRA_ARGS:-}"
SAFE_POWERS="$(printf '%s' "$POWERS" | tr ' ' '-')"

if [[ "$THREAD_MODE" == "single" ]]; then
    NT=1
elif [[ "$THREAD_MODE" == "multi" ]]; then
    NT="$THREADS"
else
    echo "THREAD_MODE must be single or multi, got: $THREAD_MODE" >&2
    exit 1
fi

if [[ "$RUN_MODE" != "independent" && "$RUN_MODE" != "looped" ]]; then
    echo "RUN_MODE must be independent or looped, got: $RUN_MODE" >&2
    exit 1
fi

if ! [[ "$WARMUPS" =~ ^[0-9]+$ && "$ROUNDS" =~ ^[0-9]+$ && "$NT" =~ ^[0-9]+$ ]]; then
    echo "WARMUPS, ROUNDS, and NT must be non-negative integers" >&2
    exit 1
fi

if (( NT < 1 )); then
    echo "NT must be at least 1" >&2
    exit 1
fi

TOTAL_TRIALS=$(( WARMUPS + ROUNDS ))
OUTPUT_FILE="${1:-$REPO_ROOT/benchmark-results-perf-psi-${RUN_MODE}-${THREAD_MODE}-nt${NT}-warm${WARMUPS}-rounds${ROUNDS}-${SAFE_POWERS}-$(date +%Y-%m-%d).md}"

if [[ "$OUTPUT_FILE" != */* && "$OUTPUT_FILE" != "" ]]; then
    OUTPUT_FILE="$REPO_ROOT/$OUTPUT_FILE"
fi

read -r -a powers <<< "$POWERS"
mkdir -p "$(dirname "$OUTPUT_FILE")"

extract_trial_times() {
    local text="$1"
    printf '%s\n' "$text" | awk '/^end[[:space:]]+[0-9.]+[[:space:]]+[0-9.]+/ { print $3 }'
}

extract_total_comm() {
    local text="$1"
    printf '%s\n' "$text" | awk '
        /^[0-9]+[[:space:]][0-9]+$/ { a = $1; b = $2 }
        END {
            if (a == "" || b == "") exit 1
            print a + b
        }
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

{
    printf "# VOLE-PSI perfPSI Benchmark Results\n\n"
    printf "Benchmark date: %s\n\n" "$(date '+%Y-%m-%d %H:%M:%S %Z')"
    if [[ "$RUN_MODE" == "independent" ]]; then
        printf "Command pattern: \`bash script/docker-unit-test.sh -perf -psi -nn <k> -t 1 -v -nt %s %s\`\n\n" "$NT" "$EXTRA_ARGS"
    else
        printf "Command pattern: \`bash script/docker-unit-test.sh -perf -psi -nn <k> -t %s -v -nt %s %s\`\n\n" "$TOTAL_TRIALS" "$NT" "$EXTRA_ARGS"
    fi
    printf "Run mode: \`%s\`\n\n" "$RUN_MODE"
    printf "Thread mode: \`%s\`\n\n" "$THREAD_MODE"
    printf "Threads passed to \`-nt\`: \`%s\`\n\n" "$NT"
    printf "Warmups dropped: \`%s\`\n\n" "$WARMUPS"
    printf "Measured rounds kept: \`%s\`\n\n" "$ROUNDS"
    printf "Notes:\n"
    printf -- "- This benchmark uses \`frontend -perf -psi\`, which runs both parties in one process over \`LocalAsyncSocket\`.\n"
    if [[ "$RUN_MODE" == "independent" ]]; then
        printf -- "- Each trial is a fresh process invocation with \`-t 1\`, so sender/receiver objects are not reused across measured runs.\n"
    else
        printf -- "- In \`looped\` mode, each size is executed once with \`-t = warmups + rounds\`, so multiple trials reuse the same process and protocol objects.\n"
    fi
    printf -- "- The first \`%s\` trial(s) are treated as warmup and excluded from the summary statistics.\n" "$WARMUPS"
    printf -- "- \`Comm Avg (bytes)\` is the mean of the parsed \`bytesSent(left) + bytesSent(right)\` totals across measured runs.\n\n"
    printf "## Summary\n\n"
    printf "| Set Size | Run Mode | Thread Mode | \`-nt\` | Measured Trials (ms) | Mean (ms) | Median (ms) | Comm Avg (bytes) |\n"
    printf "| --- | --- | --- | ---: | --- | ---: | ---: | ---: |\n"
} > "$OUTPUT_FILE"

for power in "${powers[@]}"; do
    if ! [[ "$power" =~ ^[0-9]+$ ]]; then
        echo "Power must be an integer, got: $power" >&2
        exit 1
    fi

    all_trial_times_file="$(mktemp)"
    measured_trial_times_file="$(mktemp)"
    all_comm_file="$(mktemp)"
    measured_comm_file="$(mktemp)"
    raw_log_file="$(mktemp)"

    if [[ "$RUN_MODE" == "independent" ]]; then
        for run_idx in $(seq 1 "$TOTAL_TRIALS"); do
            run_cmd=(bash script/docker-unit-test.sh -perf -psi -nn "$power" -t 1 -v -nt "$NT")
            if [[ -n "$EXTRA_ARGS" ]]; then
                # Intentionally split EXTRA_ARGS on shell words so callers can pass flags.
                # shellcheck disable=SC2206
                extra_parts=($EXTRA_ARGS)
                run_cmd+=("${extra_parts[@]}")
            fi

            output="$(
                cd "$REPO_ROOT"
                "${run_cmd[@]}"
            )"

            trial_times="$(extract_trial_times "$output")"
            trial_count="$(printf '%s\n' "$trial_times" | awk 'NF { count += 1 } END { print count + 0 }')"
            if (( trial_count != 1 )); then
                echo "Expected 1 trial time for independent run 2^$power / run $run_idx, got $trial_count" >&2
                printf '%s\n' "$output" >&2
                exit 1
            fi

            run_time="$(printf '%s\n' "$trial_times" | awk 'NF { print; exit }')"
            comm_bytes="$(extract_total_comm "$output")"
            phase="measure"
            if (( run_idx <= WARMUPS )); then
                phase="warmup"
            else
                printf '%s\n' "$run_time" >> "$measured_trial_times_file"
                printf '%s\n' "$comm_bytes" >> "$measured_comm_file"
            fi
            printf '%s\n' "$run_time" >> "$all_trial_times_file"
            printf '%s\n' "$comm_bytes" >> "$all_comm_file"

            {
                printf "### Run %s (%s)\n\n" "$run_idx" "$phase"
                printf "| Field | Value |\n"
                printf "| --- | --- |\n"
                printf "| Trial time (ms) | \`%s\` |\n" "$run_time"
                printf "| Comm (bytes) | \`%s\` |\n\n" "$comm_bytes"
                printf '```text\n%s\n```\n\n' "$output"
            } >> "$raw_log_file"
        done
    else
        run_cmd=(bash script/docker-unit-test.sh -perf -psi -nn "$power" -t "$TOTAL_TRIALS" -v -nt "$NT")
        if [[ -n "$EXTRA_ARGS" ]]; then
            # Intentionally split EXTRA_ARGS on shell words so callers can pass flags.
            # shellcheck disable=SC2206
            extra_parts=($EXTRA_ARGS)
            run_cmd+=("${extra_parts[@]}")
        fi

        output="$(
            cd "$REPO_ROOT"
            "${run_cmd[@]}"
        )"

        trial_times="$(extract_trial_times "$output")"
        trial_count="$(printf '%s\n' "$trial_times" | awk 'NF { count += 1 } END { print count + 0 }')"
        if (( trial_count != TOTAL_TRIALS )); then
            echo "Expected $TOTAL_TRIALS trial times for 2^$power, got $trial_count" >&2
            printf '%s\n' "$output" >&2
            exit 1
        fi

        printf '%s\n' "$trial_times" >> "$all_trial_times_file"
        printf '%s\n' "$trial_times" | awk -v warmups="$WARMUPS" 'NF { if (++count > warmups) print }' >> "$measured_trial_times_file"
        comm_bytes="$(extract_total_comm "$output")"
        printf '%s\n' "$comm_bytes" >> "$all_comm_file"
        printf '%s\n' "$comm_bytes" >> "$measured_comm_file"

        {
            printf "### Raw Output\n\n"
            printf '```text\n%s\n```\n\n' "$output"
        } >> "$raw_log_file"
    fi

    stats_text="$(cat "$all_trial_times_file" | summarize_trials "$WARMUPS")"
    measured_trials="$(printf '%s\n' "$stats_text" | sed -n '1p')"
    mean_ms="$(printf '%s\n' "$stats_text" | sed -n '2p')"
    median_ms="$(printf '%s\n' "$stats_text" | sed -n '3p')"
    comm_avg="$(cat "$measured_comm_file" | avg_values)"
    set_size=$(( 1 << power ))

    {
        printf "| \`2^%s = %s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` | \`%s\` |\n" \
            "$power" "$set_size" "$RUN_MODE" "$THREAD_MODE" "$NT" "$measured_trials" "$mean_ms" "$median_ms" "$comm_avg"
        printf "\n## Set Size \`2^%s = %s\`\n\n" "$power" "$set_size"
        printf "| Field | Value |\n"
        printf "| --- | --- |\n"
        printf "| Run mode | \`%s\` |\n" "$RUN_MODE"
        printf "| Thread mode | \`%s\` |\n" "$THREAD_MODE"
        printf "| \`-nt\` | \`%s\` |\n" "$NT"
        printf "| Warmups dropped | \`%s\` |\n" "$WARMUPS"
        printf "| Measured rounds | \`%s\` |\n" "$ROUNDS"
        printf "| Measured trials (ms) | \`%s\` |\n" "$measured_trials"
        printf "| Mean (ms) | \`%s\` |\n" "$mean_ms"
        printf "| Median (ms) | \`%s\` |\n" "$median_ms"
        printf "| Comm Avg (bytes) | \`%s\` |\n\n" "$comm_avg"
        cat "$raw_log_file"
    } >> "$OUTPUT_FILE"

    rm -f "$all_trial_times_file" "$measured_trial_times_file" "$all_comm_file" "$measured_comm_file" "$raw_log_file"
done

printf "Wrote benchmark report to %s\n" "$OUTPUT_FILE"
