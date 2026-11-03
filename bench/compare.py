#!/usr/bin/env python3
"""
Compares the current ./lunar against a saved baseline (bench/baseline.txt).

Runs both Lunar suites: the bench.py workloads (tak, mandelbrot, sieve, matmul,
binary trees) and the bench/*.lunar programs. A benchmark more than 5% slower
than the baseline is re-run once; it is reported as a regression only if it
is still more than 5% slower.

Usage (from the repo root, after `make`):
    python3 bench/compare.py            # compare against bench/baseline.txt
    python3 bench/compare.py --save     # measure and overwrite the baseline
"""

import argparse
import datetime
import glob
import importlib.util
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASELINE = os.path.join(ROOT, "bench", "baseline.txt")
LIMIT = 0.05


def load_bench_py():
    spec = importlib.util.spec_from_file_location("bench_py", os.path.join(ROOT, "bench.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def programs(tmp):
    """Returns {name: path} for every Lunar benchmark program."""
    bp = load_bench_py()
    out = {}
    for name, progs in bp.BENCHMARKS.items():
        path = os.path.join(tmp, f"bench_py_{len(out)}.lunar")
        with open(path, "w") as f:
            f.write(progs["Lunar"].lstrip("\n"))
        out[name] = path
    for path in sorted(glob.glob(os.path.join(ROOT, "bench", "*.lunar"))):
        out[os.path.relpath(path, ROOT)] = path
    return out, bp


def measure(bp, lunar, path, runs):
    res = bp.bench([lunar, path], runs, 1, 120)
    if res is None:
        sys.exit(f"benchmark failed: {path}")
    return res[0], res[1]


def read_baseline():
    base = {}
    with open(BASELINE) as f:
        for line in f:
            if line.startswith("#") or not line.strip():
                continue
            name, mean, sd = line.rstrip("\n").split("\t")
            base[name] = (float(mean), float(sd))
    return base


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lunar", default=os.path.join(ROOT, "lunar"))
    ap.add_argument("--runs", type=int, default=10)
    ap.add_argument("--save", action="store_true", help="overwrite the baseline")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory() as tmp:
        progs, bp = programs(tmp)

        if args.save:
            commit = subprocess.run(["git", "rev-parse", "--short", "HEAD"], cwd=ROOT,
                                    capture_output=True, text=True).stdout.strip()
            lines = [f"# Lunar benchmark baseline, commit {commit}, "
                     f"{datetime.date.today()}, mean and stdev of {args.runs} runs (s)"]
            for name, path in progs.items():
                mean, sd = measure(bp, args.lunar, path, args.runs)
                print(f"{name:<28} {mean:.3f}s ± {sd:.3f}")
                lines.append(f"{name}\t{mean:.4f}\t{sd:.4f}")
            with open(BASELINE, "w") as f:
                f.write("\n".join(lines) + "\n")
            print(f"\nSaved {os.path.relpath(BASELINE, ROOT)}")
            return

        base = read_baseline()
        regressions = []
        print(f"| Benchmark | Baseline | Now | Change |\n|---|---|---|---|")
        for name, path in progs.items():
            if name not in base:
                print(f"| {name} | n/a | (not in baseline) | |")
                continue
            b = base[name][0]
            mean, sd = measure(bp, args.lunar, path, args.runs)
            if mean > b * (1 + LIMIT):
                mean, sd = measure(bp, args.lunar, path, args.runs)   # re-run once
            change = (mean - b) / b
            flag = " **SLOWER**" if change > LIMIT else ""
            if flag:
                regressions.append(name)
            print(f"| {name} | {b:.3f} s | {mean:.3f} s ± {sd:.3f} | {change:+.1%}{flag} |")

    if regressions:
        print(f"\nMore than {LIMIT:.0%} slower after a re-run: {', '.join(regressions)}")
        sys.exit(1)
    print(f"\nNo benchmark more than {LIMIT:.0%} slower than the baseline.")


if __name__ == "__main__":
    main()
