#!/usr/bin/env python3

import argparse
import re
import statistics
from pathlib import Path


def parse_args():
    p = argparse.ArgumentParser(description="Analyze convergence of VOLE-PSI loopback benchmark rounds.")
    p.add_argument("report", type=Path, help="Path to benchmark markdown report.")
    p.add_argument("--window", type=int, default=5, help="Require this many consecutive running means to stay within tolerance.")
    p.add_argument("--tol-pct", type=float, default=2.0, help="Relative tolerance against the final mean, in percent.")
    p.add_argument("--min-rounds", type=int, default=10, help="Do not declare convergence before this many measured rounds.")
    return p.parse_args()


def parse_trials(text: str):
    pattern = re.compile(r"## Set Size `2\^(\d+) = (\d+)`.*?\| Measured trials \(ms\) \| `([^`]+)` \|", re.S)
    rows = []
    for power, size, trials_blob in pattern.findall(text):
        vals = [float(x.strip()) for x in trials_blob.split(",") if x.strip()]
        rows.append((int(power), int(size), vals))
    return rows


def convergence_round(vals, tol_pct: float, window: int, min_rounds: int):
    if len(vals) < min_rounds:
        final_mean = statistics.mean(vals) if vals else float("nan")
        return None, final_mean, final_mean
    final_mean = statistics.mean(vals)
    tol = abs(final_mean) * (tol_pct / 100.0)
    running = []
    for i in range(1, len(vals) + 1):
        running.append(statistics.mean(vals[:i]))
    for i in range(min_rounds - 1, len(vals)):
        if i + 1 < window:
            continue
        recent = running[i - window + 1:i + 1]
        if all(abs(x - final_mean) <= tol for x in recent):
            return i + 1, final_mean, running[i]
    return None, final_mean, running[-1]


def main():
    args = parse_args()
    text = args.report.read_text(encoding="utf-8")
    rows = parse_trials(text)
    if not rows:
        raise SystemExit(f"No measured trial series found in {args.report}")

    print(f"# Convergence Analysis")
    print()
    print(f"Report: `{args.report}`")
    print()
    print(f"Criterion: running mean stays within `{args.tol_pct:.2f}%` of final mean for the last `{args.window}` consecutive measured rounds, with at least `{args.min_rounds}` measured rounds available.")
    print()
    print("| Set Size | Measured Rounds Available | Final Mean (ms) | Converged By Round | |running mean - final mean| at decision (ms) |")
    print("| --- | ---: | ---: | ---: | ---: |")

    suggested = 0
    for power, size, vals in rows:
        conv, final_mean, decision_mean = convergence_round(vals, args.tol_pct, args.window, args.min_rounds)
        if conv is None:
            conv_label = "not reached"
            delta = abs(decision_mean - final_mean)
        else:
            conv_label = str(conv)
            delta = abs(decision_mean - final_mean)
            suggested = max(suggested, conv)
        print(f"| `2^{power} = {size}` | `{len(vals)}` | `{final_mean:.3f}` | `{conv_label}` | `{delta:.3f}` |")

    print()
    if suggested:
        print(f"Suggested measured rounds for this report: `{suggested}`")
    else:
        print("Suggested measured rounds for this report: `more data needed`")


if __name__ == "__main__":
    main()
