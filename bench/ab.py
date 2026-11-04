#!/usr/bin/env python3
"""
The official benchmark check: the current ./lunar against a fresh build of
another git ref (main by default), run side by side.

Each benchmark runs 15 times per binary, alternating between the two so both
see the same machine conditions, and the medians are compared. Each time is
shown as median (min–max, standard deviation). A benchmark
more than 5% slower is re-run once; it is a regression only if it is still
more than 5% slower. A benchmark the ref can't run (it uses a newer feature)
is timed on the current build only. bench/compare.py (the saved baseline) is
a secondary reference.

Usage (from the repo root, after `make`):
    python3 bench/ab.py
    python3 bench/ab.py --ref phase-1-2~3 --runs 21
    python3 bench/ab.py --json times.json     # also save every run's time
"""

import argparse
import json
import glob
import importlib.util
import os
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LIMIT = 0.05


def build_ref(ref, tmp):
    src = os.path.join(tmp, "ref")
    os.makedirs(src)
    archive = subprocess.run(["git", "archive", ref], cwd=ROOT, capture_output=True, check=True)
    subprocess.run(["tar", "-x", "-C", src], input=archive.stdout, check=True)
    r = subprocess.run(["make", "-C", src], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"building {ref} failed:\n{r.stderr}")
    return os.path.join(src, "lunar")


def programs(tmp):
    spec = importlib.util.spec_from_file_location("bench_py", os.path.join(ROOT, "bench.py"))
    bp = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(bp)
    out = {}
    for name, progs in bp.BENCHMARKS.items():
        path = os.path.join(tmp, f"bench_py_{len(out)}.lunar")
        with open(path, "w") as f:
            f.write(progs["Lunar"].lstrip("\n"))
        out[name] = path
    for path in sorted(glob.glob(os.path.join(ROOT, "bench", "*.lunar"))):
        out[os.path.relpath(path, ROOT)] = path
    return out


def run(binary, path):
    t0 = time.perf_counter()
    p = subprocess.run([binary, path], stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
    if p.returncode != 0:
        sys.exit(f"{binary} {path} failed: {p.stderr.decode()[:200]}")
    return time.perf_counter() - t0


def runs_ok(binary, path):
    p = subprocess.run([binary, path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return p.returncode == 0


def ab(old, new, path, runs):
    """`runs` alternating runs of each binary, after one warmup each."""
    run(old, path); run(new, path)
    t_old, t_new = [], []
    for _ in range(runs):
        t_old.append(run(old, path))
        t_new.append(run(new, path))
    return t_old, t_new


def describe(times):
    """median (min–max, σ stddev)"""
    return (f"{statistics.median(times):.3f} s ({min(times):.3f}–{max(times):.3f}, "
            f"σ {statistics.stdev(times):.4f})")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ref", default="main", help="git ref to compare against")
    ap.add_argument("--lunar", default=os.path.join(ROOT, "lunar"))
    ap.add_argument("--runs", type=int, default=15)
    ap.add_argument("--json", help="write every run's time to this file")
    args = ap.parse_args()

    tmp = tempfile.mkdtemp()
    try:
        old = build_ref(args.ref, tmp)
        regressions = []
        raw = {}
        print(f"| Benchmark | {args.ref} | now | Change |\n|---|---|---|---|")
        for name, path in programs(tmp).items():
            if not runs_ok(old, path):
                t_new = [run(args.lunar, path) for _ in range(args.runs)]
                raw[name] = {"new": t_new}
                print(f"| {name} | n/a (new) | {describe(t_new)} | |")
                continue
            t_old, t_new = ab(old, args.lunar, path, args.runs)
            if statistics.median(t_new) > statistics.median(t_old) * (1 + LIMIT):
                t_old, t_new = ab(old, args.lunar, path, args.runs)   # re-run once
            raw[name] = {"ref": t_old, "new": t_new}
            m_old, m_new = statistics.median(t_old), statistics.median(t_new)
            change = (m_new - m_old) / m_old
            flag = " **SLOWER**" if change > LIMIT else ""
            if flag:
                regressions.append(name)
            print(f"| {name} | {describe(t_old)} | {describe(t_new)} | {change:+.1%}{flag} |")
    finally:
        shutil.rmtree(tmp)
    if args.json:
        with open(args.json, "w") as f:
            json.dump({"ref": args.ref, "runs": args.runs, "times": raw}, f, indent=1)

    print(f"\n{args.runs} alternating runs each: median (min–max, σ standard deviation).")
    if regressions:
        print(f"More than {LIMIT:.0%} slower after a re-run: {', '.join(regressions)}")
        sys.exit(1)
    print(f"No benchmark more than {LIMIT:.0%} slower than {args.ref}.")


if __name__ == "__main__":
    main()
