#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage
  bash script/tune-rb-pqpsi.sh [out.md]

Defaults
  WARMUPS=0
  ROUNDS=2
  TARGET_LAMBDA=40
  PORT_BASE_START=43000
  KEEP_WORK=0

Notes
  - keeps only one final summary by default
  - temp reports and temp log are deleted on exit
  - set KEEP_WORK=1 if you want to keep the temp workspace
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="${BENCH_DIR:-$REPO_ROOT/build-docker/benchmarks/rbokvs-pqpsi}"
RUN_TAG="$(date '+%Y%m%d-%H%M%S')"
OUT_FILE="${1:-$BENCH_DIR/rb-tune-summary-${RUN_TAG}.md}"
WARMUPS="${WARMUPS:-0}"
ROUNDS="${ROUNDS:-2}"
TARGET_LAMBDA="${TARGET_LAMBDA:-${RB_LAMBDA:-40}}"
PORT_BASE_START="${PORT_BASE_START:-43000}"
KEEP_WORK="${KEEP_WORK:-0}"

mkdir -p "$BENCH_DIR"
mkdir -p "$(dirname "$OUT_FILE")"

if [[ -n "${WORK_DIR:-}" ]]; then
    TMP_DIR="$WORK_DIR"
    mkdir -p "$TMP_DIR"
else
    TMP_DIR="$(mktemp -d "$BENCH_DIR/.tmp-tune.XXXXXX")"
fi

cleanup() {
    if [[ "$KEEP_WORK" != "1" ]]; then
        rm -rf "$TMP_DIR"
    fi
}
trap cleanup EXIT

CASES_FILE="$TMP_DIR/cases.txt"
LOG_FILE="$TMP_DIR/tune.log"
STATUS_FILE="$TMP_DIR/status.txt"

cat > "$CASES_FILE" <<'EOF'
128 0.14 48
128 0.14 64
128 0.15 32
128 0.15 48
128 0.16 32
128 0.16 48
128 0.17 40
128 0.17 48
128 0.18 32
128 0.18 48
128 0.19 32
128 0.19 48
128 0.20 32
128 0.20 48
256 0.07 64
256 0.07 80
256 0.08 56
256 0.08 64
256 0.09 64
256 0.09 80
256 0.10 48
256 0.10 64
256 0.11 40
256 0.11 56
256 0.12 40
256 0.12 56
512 0.07 48
512 0.07 64
512 0.08 56
512 0.08 64
512 0.09 48
512 0.09 64
512 0.10 48
512 0.10 64
512 0.11 40
512 0.11 56
512 0.12 40
512 0.12 56
1024 0.12 144
1024 0.07 240
1024 0.10 176
2048 0.12 144
2048 0.07 240
2048 0.10 176
EOF

: > "$LOG_FILE"
: > "$STATUS_FILE"

echo "[tune] workspace $TMP_DIR" >&2
echo "[tune] running shortlisted cases" >&2

run_case() {
    local size="$1"
    local eps="$2"
    local w="$3"
    local idx="$4"
    local tag="size-${size}_eps-${eps}_w-${w}"
    local sum_file="$TMP_DIR/${tag}.md"
    local det_dir="detail-${tag}"
    local msg="[tune] size=${size} eps=${eps} w=${w}"

    echo "$msg" >&2
    echo "$msg" >> "$LOG_FILE"

    local run_lambda="$TARGET_LAMBDA"
    if (( size <= 512 )); then
        run_lambda=1
    fi

    if SIZES="$size" \
        RB_EPS="$eps" \
        RB_W="$w" \
        RB_LAMBDA="$run_lambda" \
        WARMUPS="$WARMUPS" \
        ROUNDS="$ROUNDS" \
        PORT_BASE_START="$(( PORT_BASE_START + idx * 500 ))" \
        DETAIL_MODE=dir \
        DETAIL_DIR_NAME="$det_dir" \
        BENCH_DIR="$TMP_DIR" \
        bash "$REPO_ROOT/script/benchmark-docker-pqpsi.sh" "$(basename "$sum_file")" "$ROUNDS" "$WARMUPS" >> "$LOG_FILE" 2>&1; then
        local det_file
        det_file="$(find "$TMP_DIR/$det_dir" -type f -name "*.md" | head -n 1)"
        if [[ -n "$det_file" && -f "$det_file" ]]; then
            echo "${size}|${eps}|${w}|run_ok|$(basename "$det_file")|$det_dir" >> "$STATUS_FILE"
            rm -f "$sum_file"
            return 0
        fi
    fi

    rm -f "$sum_file"
    rm -rf "$TMP_DIR/$det_dir"
    echo "${size}|${eps}|${w}|run_fail||" >> "$STATUS_FILE"
    return 0
}

idx=0
while read -r size eps w; do
    [[ -z "${size:-}" ]] && continue
    run_case "$size" "$eps" "$w" "$idx"
    idx=$(( idx + 1 ))
done < "$CASES_FILE"

python3 - "$STATUS_FILE" "$OUT_FILE" "$TARGET_LAMBDA" "$WARMUPS" "$ROUNDS" <<'PY'
import sys
from pathlib import Path

status_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])
lam = float(sys.argv[3])
warm = sys.argv[4]
rounds = sys.argv[5]

EMP_G = {
    (128, 0.14): 21.51,
    (128, 0.15): 26.80,
    (128, 0.16): 25.91,
    (128, 0.17): 21.29,
    (128, 0.18): 24.15,
    (128, 0.19): 23.27,
    (128, 0.20): 22.39,
    (256, 0.07): 27.68,
    (256, 0.08): 27.68,
    (256, 0.09): 24.15,
    (256, 0.10): 26.80,
    (256, 0.11): 27.90,
    (256, 0.12): 26.80,
    (512, 0.07): 30.76,
    (512, 0.08): 27.68,
    (512, 0.09): 28.12,
    (512, 0.10): 26.80,
    (512, 0.11): 27.90,
    (512, 0.12): 26.80,
}

def summary_value(text: str, key: str) -> str:
    for line in text.splitlines():
        if not line.startswith("|"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) >= 4 and parts[1] == key:
            return parts[2]
    return ""

def party_mean(text: str, party: str) -> float:
    prefix = f"### {party} (mean total "
    for line in text.splitlines():
        if line.startswith(prefix):
            tail = line[len(prefix):]
            return float(tail.split(" ms", 1)[0])
    return 0.0

def empirical_lambda(size: int, eps: float, w: int):
    key = (size, round(eps, 2))
    g = EMP_G.get(key)
    if g is None:
        return None
    return 2.751 * eps * w + g

rows = []
work_dir = status_path.parent

for line in status_path.read_text().splitlines():
    size, eps_in, w_in, run_status, report, det_dir = line.split("|", 5)
    row = {
        "size": int(size),
        "eps_in": float(eps_in),
        "w_in": int(w_in),
        "run_status": run_status,
        "report": report,
        "det_dir": det_dir,
        "status": "run_fail",
        "safe": False,
        "m": 0,
        "eps_real": 0.0,
        "w_real": 0,
        "lam_real": 0.0,
        "lam_mode": "paper",
        "runtime": 0.0,
        "comm": 0.0,
        "p0": 0.0,
        "p1": 0.0,
    }
    if run_status == "run_ok" and report:
        text = (work_dir / det_dir / report).read_text()
        row["status"] = summary_value(text, "overall_status") or "unknown"
        row["m"] = int(float(summary_value(text, "rb_m") or 0))
        row["eps_real"] = float(summary_value(text, "rb_eps") or 0.0)
        row["w_real"] = int(float(summary_value(text, "rb_w") or 0))
        row["lam_real"] = float(summary_value(text, "rb_lambda_real") or 0.0)
        row["runtime"] = float(summary_value(text, "runtime_mean_ms") or 0.0)
        row["comm"] = float(summary_value(text, "total_communication_kb_mean") or 0.0)
        row["p0"] = party_mean(text, "party0")
        row["p1"] = party_mean(text, "party1")
        if row["size"] <= 512:
            lam_emp = empirical_lambda(row["size"], row["eps_real"], row["w_real"])
            if lam_emp is not None:
                row["lam_real"] = lam_emp
                row["lam_mode"] = "empirical"
        row["safe"] = row["status"] == "ok" and row["lam_real"] >= lam
    rows.append(row)

rows.sort(key=lambda r: (r["size"], r["runtime"] if r["runtime"] else 10**18, r["comm"] if r["comm"] else 10**18))

by_size = {}
for row in rows:
    by_size.setdefault(row["size"], []).append(row)

def fastest_safe(items):
    safe = [r for r in items if r["safe"]]
    if not safe:
        return None
    return min(safe, key=lambda r: (r["runtime"], r["comm"], r["eps_real"]))

def lowest_comm_safe(items):
    safe = [r for r in items if r["safe"]]
    if not safe:
        return None
    return min(safe, key=lambda r: (r["comm"], r["runtime"], r["eps_real"]))

out = []
out.append("# RB Tune Summary\n\n")
out.append(f"`lambda={int(lam)}` `warmups={warm}` `rounds={rounds}`\n\n")
out.append("Note\n")
out.append("- `2^7` to `2^9` use empirical g in the safe check\n")
out.append("- `2^10` and `2^11` use the paper-fit gate from code\n\n")
out.append("## Params\n\n")
out.append("### speed\n\n")
out.append("| Size | eps | w | m | lambda_real |\n")
out.append("| --- | ---: | ---: | ---: | ---: |\n")
for size in sorted(by_size):
    fast = fastest_safe(by_size[size])
    label = f"2^{size.bit_length() - 1}"
    if fast is None:
        out.append(f"| {label} | - | - | - | - |\n")
        continue
    out.append(f"| {label} | {fast['eps_real']:.2f} | {fast['w_real']} | {fast['m']} | {fast['lam_real']:.2f} |\n")

out.append("\n### comm\n\n")
out.append("| Size | eps | w | m | lambda_real |\n")
out.append("| --- | ---: | ---: | ---: | ---: |\n")
for size in sorted(by_size):
    low = lowest_comm_safe(by_size[size])
    label = f"2^{size.bit_length() - 1}"
    if low is None:
        out.append(f"| {label} | - | - | - | - |\n")
        continue
    out.append(f"| {label} | {low['eps_real']:.2f} | {low['w_real']} | {low['m']} | {low['lam_real']:.2f} |\n")

out.append("\n## Performance\n\n")
out.append("### speed\n\n")
out.append("| Size | runtime_ms | comm_kb |\n")
out.append("| --- | ---: | ---: |\n")
for size in sorted(by_size):
    fast = fastest_safe(by_size[size])
    label = f"2^{size.bit_length() - 1}"
    if fast is None:
        out.append(f"| {label} | - | - |\n")
        continue
    out.append(f"| {label} | {fast['runtime']:.2f} | {fast['comm']:.2f} |\n")

out.append("\n### comm\n\n")
out.append("| Size | runtime_ms | comm_kb |\n")
out.append("| --- | ---: | ---: |\n")
for size in sorted(by_size):
    low = lowest_comm_safe(by_size[size])
    label = f"2^{size.bit_length() - 1}"
    if low is None:
        out.append(f"| {label} | - | - |\n")
        continue
    out.append(f"| {label} | {low['runtime']:.2f} | {low['comm']:.2f} |\n")

out.append("\n## All Tried Cases\n\n")
out.append("| Size | eps_in | w_in | m | eps | w | lambda_real | lambda_mode | psi | safe | runtime_ms | comm_kb | party0_ms | party1_ms |\n")
out.append("| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- | --- | ---: | ---: | ---: | ---: |\n")
for row in rows:
    label = f"2^{row['size'].bit_length() - 1}"
    m_val = str(row["m"]) if row["m"] else "-"
    eps_val = f"{row['eps_real']:.2f}" if row["m"] else "-"
    w_val = str(row["w_real"]) if row["m"] else "-"
    lam_val = f"{row['lam_real']:.2f}" if row["m"] else "-"
    run_val = f"{row['runtime']:.2f}" if row["runtime"] else "-"
    comm_val = f"{row['comm']:.2f}" if row["comm"] else "-"
    p0_val = f"{row['p0']:.2f}" if row["p0"] else "-"
    p1_val = f"{row['p1']:.2f}" if row["p1"] else "-"
    out.append(
        f"| {label} | {row['eps_in']:.2f} | {row['w_in']} | "
        f"{m_val} | "
        f"{eps_val} | "
        f"{w_val} | "
        f"{lam_val} | "
        f"{row['lam_mode']} | "
        f"{row['status']} | "
        f"{'yes' if row['safe'] else 'no'} | "
        f"{run_val} | "
        f"{comm_val} | "
        f"{p0_val} | "
        f"{p1_val} |\n"
    )

out_path.write_text("".join(out))
print(out_path)
PY

echo "[tune] wrote $OUT_FILE" >&2
if [[ "$KEEP_WORK" == "1" ]]; then
    echo "[tune] kept $TMP_DIR" >&2
fi
