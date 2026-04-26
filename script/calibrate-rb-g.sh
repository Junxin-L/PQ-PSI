#!/usr/bin/env bash

set -euo pipefail

usage() {
    cat <<'EOF'
Usage
  bash script/calibrate-rb-g.sh [out.md]

Defaults
  ROUNDS=5
  TARGET_LAMBDA=40
  KEEP_WORK=0

Notes
  - uses the RB-OKVS only check binary
  - uses lambda=1 inside that check to avoid the current safety gate
  - picks the smallest okvs-passing w for each size and eps
  - reports g_40 = 40 - 2.751 * eps * w_min_pass
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" || "${1:-}" == "help" ]]; then
    usage
    exit 0
fi

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BENCH_DIR="${BENCH_DIR:-$REPO_ROOT/build-docker/benchmarks/rbokvs-pqpsi}"
RUN_TAG="$(date '+%Y%m%d-%H%M%S')"
OUT_FILE="${1:-$BENCH_DIR/rb-g-okvs-only-${RUN_TAG}.md}"
WARMUPS="${WARMUPS:-1}"
ROUNDS="${ROUNDS:-5}"
TARGET_LAMBDA="${TARGET_LAMBDA:-40}"
KEEP_WORK="${KEEP_WORK:-0}"

mkdir -p "$BENCH_DIR"
mkdir -p "$(dirname "$OUT_FILE")"

if [[ -n "${WORK_DIR:-}" ]]; then
    TMP_DIR="$WORK_DIR"
    mkdir -p "$TMP_DIR"
else
    TMP_DIR="$(mktemp -d "$BENCH_DIR/.tmp-g.XXXXXX")"
fi

cleanup() {
    if [[ "$KEEP_WORK" != "1" ]]; then
        rm -rf "$TMP_DIR"
    fi
}
trap cleanup EXIT

CASES_FILE="$TMP_DIR/cases.txt"
STATUS_FILE="$TMP_DIR/status.txt"
LOG_FILE="$TMP_DIR/run.log"

: > "$CASES_FILE"

for eps in 0.14 0.15 0.16 0.17 0.18 0.19 0.20; do
    for w in 16 24 32 40 48 56 64 80 96 112 128 144; do
        echo "128 $eps $w" >> "$CASES_FILE"
    done
done

for eps in 0.07 0.08 0.09 0.10 0.11 0.12; do
    for w in 16 24 32 40 48 56 64 80 96 112 128 144 160 176 192 208 224 240 256; do
        echo "256 $eps $w" >> "$CASES_FILE"
    done
done

for eps in 0.07 0.08 0.09 0.10 0.11 0.12; do
    for w in 16 24 32 40 48 56 64 80 96 112 128 144 160 176 192 208 224 240 256; do
        echo "512 $eps $w" >> "$CASES_FILE"
    done
done

: > "$STATUS_FILE"
: > "$LOG_FILE"

echo "[g] workspace $TMP_DIR" >&2

if [[ ! -f "$REPO_ROOT/bin/rbokvs_g_check" ]]; then
    echo "Missing $REPO_ROOT/bin/rbokvs_g_check. Run script/build-docker-pqpsi-bench.sh first." >&2
    exit 1
fi

TMP_REL="${TMP_DIR#$REPO_ROOT/}"
if [[ "$TMP_REL" == "$TMP_DIR" ]]; then
    echo "TMP_DIR must stay under repo root so Docker can write results" >&2
    exit 1
fi

docker run --rm --platform linux/amd64 \
    -v "$REPO_ROOT:/work" \
    -w /work \
    ubuntu:22.04 \
    bash -lc "set -euo pipefail && \
        apt-get update >/tmp/apt-update.log 2>&1 && \
        apt-get install -y --no-install-recommends libboost-system1.74.0 libboost-thread1.74.0 libgmp10 libgf2x3 libntl44 libsodium23 >/tmp/apt-install.log 2>&1 && \
        idx=0; \
        while read -r size eps w; do \
            [ -z \"\${size:-}\" ] && continue; \
            tag=\"size-\${size}_eps-\${eps}_w-\${w}\"; \
            case_file=\"/work/${TMP_REL}/\${tag}.md\"; \
            echo \"[g] size=\${size} eps=\${eps} w=\${w}\" >&2; \
            if ./bin/rbokvs_g_check \"\$case_file\" \"\$size\" \"\$eps\" \"\$w\" \"$ROUNDS\" \$(( idx + 1 )) >>\"/work/${TMP_REL}/run.log\" 2>&1; then \
                echo \"\${size}|\${eps}|\${w}|run_ok|\$(basename \"\$case_file\")\" >>\"/work/${TMP_REL}/status.txt\"; \
            else \
                if [ -f \"\$case_file\" ]; then \
                    echo \"\${size}|\${eps}|\${w}|run_fail|\$(basename \"\$case_file\")\" >>\"/work/${TMP_REL}/status.txt\"; \
                else \
                    echo \"\${size}|\${eps}|\${w}|run_fail|\" >>\"/work/${TMP_REL}/status.txt\"; \
                fi; \
            fi; \
            idx=\$(( idx + 1 )); \
        done <\"/work/${TMP_REL}/cases.txt\""

python3 - "$STATUS_FILE" "$OUT_FILE" "$TARGET_LAMBDA" "$WARMUPS" "$ROUNDS" <<'PY'
import math
import sys
from pathlib import Path

status_path = Path(sys.argv[1])
out_path = Path(sys.argv[2])
target_lambda = float(sys.argv[3])
warm = sys.argv[4]
rounds = sys.argv[5]

def summary_value(text: str, key: str) -> str:
    for line in text.splitlines():
        if not line.startswith("|"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) >= 4 and parts[1] == key:
            return parts[2]
    return ""

def lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t

def interp(xs, ys, x: float) -> float:
    if x <= xs[0]:
        return ys[0]
    if x >= xs[-1]:
        i = len(xs) - 2
        t = (x - xs[i]) / (xs[i + 1] - xs[i])
        return lerp(ys[i], ys[i + 1], t)
    for i in range(len(xs) - 1):
        if x <= xs[i + 1]:
            t = (x - xs[i]) / (xs[i + 1] - xs[i])
            return lerp(ys[i], ys[i + 1], t)
    return ys[-1]

def paper_g(n: int, eps: float) -> float:
    eps_xs = [0.03, 0.05, 0.07, 0.10]
    log_xs = [10.0, 14.0, 16.0, 18.0, 20.0]
    g_grid = [
        [-3.4, -6.1, -7.5, -9.0, -10.7],
        [-4.3, -7.5, -9.2, -10.8, -12.9],
        [-5.3, -8.3, -10.3, -12.1, -14.0],
        [-6.3, -9.4, -11.6, -13.5, -15.3],
    ]
    log_n = max(10.0, math.log2(max(n, 1)))
    g_at_n = [interp(log_xs, row, log_n) for row in g_grid]
    if eps <= eps_xs[0]:
        return g_at_n[0]
    if eps >= eps_xs[-1]:
        i = len(eps_xs) - 2
        t = (eps - eps_xs[i]) / (eps_xs[i + 1] - eps_xs[i])
        return lerp(g_at_n[i], g_at_n[i + 1], t)
    return interp(eps_xs, g_at_n, eps)

rows = []
work_dir = status_path.parent

for line in status_path.read_text().splitlines():
    size, eps_in, w_in, run_status, report = line.split("|", 4)
    row = {
        "size": int(size),
        "eps_in": float(eps_in),
        "w_in": int(w_in),
        "run_status": run_status,
        "report": report,
        "okvs": "run_fail",
        "runtime": 0.0,
        "comm": 0.0,
        "eps_real": 0.0,
        "w_real": 0,
        "m": 0,
    }
    if run_status == "run_ok" and report:
        text = (work_dir / report).read_text()
        row["okvs"] = summary_value(text, "overall_status") or "unknown"
        row["runtime"] = float(summary_value(text, "okvs_runtime_mean_ms") or 0.0)
        row["eps_real"] = float(summary_value(text, "rb_eps") or 0.0)
        row["w_real"] = int(float(summary_value(text, "rb_w") or 0.0))
        row["m"] = int(float(summary_value(text, "rb_m") or 0.0))
    elif report:
        text = (work_dir / report).read_text() if (work_dir / report).exists() else ""
        if text:
            row["okvs"] = summary_value(text, "overall_status") or "run_fail"
            row["runtime"] = float(summary_value(text, "okvs_runtime_mean_ms") or 0.0)
            row["eps_real"] = float(summary_value(text, "rb_eps") or 0.0)
            row["w_real"] = int(float(summary_value(text, "rb_w") or 0.0))
            row["m"] = int(float(summary_value(text, "rb_m") or 0.0))
    rows.append(row)

rows.sort(key=lambda r: (r["size"], r["eps_in"], r["w_in"]))

groups = {}
for row in rows:
    key = (row["size"], row["eps_in"])
    groups.setdefault(key, []).append(row)

out = []
out.append("# RB Empirical G Summary\n\n")
out.append(f"`target_lambda={int(target_lambda)}` `okvs_check_lambda=1` `rounds={rounds}`\n\n")
out.append("Note\n")
out.append("- runs use the RB-OKVS only checker\n")
out.append("- runs use lambda=1 only to bypass the current width gate\n")
out.append("- w_min_pass is the smallest tested width with okvs=ok\n")
out.append("- g_40 is computed as 40 - 2.751 * eps * w_min_pass\n")
out.append("- this is an empirical approximation for 2^7 to 2^9\n\n")
out.append("## Empirical G\n\n")
out.append("| Size | eps | w_min_pass | m | g_40 |\n")
out.append("| --- | ---: | ---: | ---: | ---: |\n")
for key in sorted(groups):
    size, eps = key
    passed = [r for r in groups[key] if r["okvs"] == "ok"]
    label = f"2^{size.bit_length() - 1}"
    if not passed:
        out.append(f"| {label} | {eps:.2f} | - | - | - |\n")
        continue
    best = min(passed, key=lambda r: r["w_real"])
    g40 = target_lambda - 2.751 * best["eps_real"] * best["w_real"]
    out.append(f"| {label} | {best['eps_real']:.2f} | {best['w_real']} | {best['m']} | {g40:.2f} |\n")

out.append("\n## Paper Fit Reference\n\n")
out.append("| Size | eps | g_code |\n")
out.append("| --- | ---: | ---: |\n")
for size in (1024, 2048):
    label = f"2^{size.bit_length() - 1}"
    for eps in (0.07, 0.10, 0.12):
        out.append(f"| {label} | {eps:.2f} | {paper_g(size, eps):.2f} |\n")

out.append("\n## All Tried Cases\n\n")
out.append("| Size | eps | w | m | okvs | runtime_ms |\n")
out.append("| --- | ---: | ---: | ---: | --- | ---: |\n")
for row in rows:
    label = f"2^{row['size'].bit_length() - 1}"
    m_val = str(row["m"]) if row["m"] else "-"
    run_val = f"{row['runtime']:.2f}" if row["runtime"] else "-"
    out.append(
        f"| {label} | {row['eps_in']:.2f} | {row['w_in']} | {m_val} | {row['okvs']} | {run_val} |\n"
    )

out_path.write_text("".join(out))
print(out_path)
PY

echo "[g] wrote $OUT_FILE" >&2
