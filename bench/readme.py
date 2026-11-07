#!/usr/bin/env python3
"""
Benchmark tables: Lunar against Lua 5.4 and Python 3, and Lunar over time
(the first five benchmarks on older versions, each built from git).

Each benchmark is a bench/<name>.lunar file with .lua and .py versions that
compute the same thing; their outputs are checked against each other. Every
program runs once to warm up, then 5 times; times are mean ± stddev of the
wall-clock time, including process startup. The tables are printed, not
written anywhere.

Usage (from the repo root, after `make`):
    python3 bench/readme.py
    python3 bench/readme.py --no-history --only fib_32 nbody_100k
"""

import argparse
import os
import platform
import shutil
import statistics
import subprocess
import sys
import tempfile
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BENCH = os.path.join(ROOT, "bench")

# (label, file name); the first five are the original set, used for history
PROGRAMS = [
    ("fib(32)", "fib_32"),
    ("loop (10M)", "loop_10m"),
    ("array (1M)", "array_1m"),
    ("method calls (1M)", "method_calls_1m"),
    ("closures (1M)", "closures_1m"),
    ("maps (100k keys)", "maps_100k"),
    ("strings (100k lines)", "strings_100k"),
    ("for-in (11M items)", "forin_10m"),
    ("n-body (100k steps)", "nbody_100k"),
]
ORIGINAL = 5

# older versions for "Lunar over time": (label, git ref)
HISTORY = [("v0.1 (41a15e0)", "41a15e0"), ("v0.2.0", "v0.2.0"), ("v0.3.0", "v0.3.0")]


def run_once(cmd):
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True)
    dt = time.perf_counter() - t0
    if p.returncode != 0:
        sys.exit(f"{' '.join(cmd)} failed: {p.stderr.strip()[:300]}")
    return dt, p.stdout.strip()


def measure(cmd, runs):
    """(mean, stddev, output), after one warmup run"""
    run_once(cmd)
    times, out = [], ""
    for _ in range(runs):
        dt, out = run_once(cmd)
        times.append(dt)
    return statistics.mean(times), statistics.stdev(times), out


def same_number(outs):
    """Lunar prints numbers with %g, so compare to 1e-5 relative."""
    try:
        vals = [float(o) for o in outs]
    except ValueError:
        return len(set(outs)) == 1
    ref = vals[0]
    return all(abs(v - ref) <= 1e-5 * max(abs(ref), abs(v), 1.0) for v in vals)


def fmt(res):
    return f"{res[0]:.3f} s ± {res[1]:.3f}"


def version(cmd):
    p = subprocess.run(cmd, capture_output=True, text=True)
    return (p.stdout + p.stderr).strip().splitlines()[0]


def cpu_name():
    if sys.platform == "darwin":
        return subprocess.run(["sysctl", "-n", "machdep.cpu.brand_string"],
                              capture_output=True, text=True).stdout.strip()
    return platform.processor() or platform.machine()


def build(ref, tmp):
    src = os.path.join(tmp, ref)
    os.makedirs(src)
    archive = subprocess.run(["git", "archive", ref], cwd=ROOT, capture_output=True, check=True)
    subprocess.run(["tar", "-x", "-C", src], input=archive.stdout, check=True)
    r = subprocess.run(["make", "-C", src], capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit(f"building {ref} failed:\n{r.stderr[-500:]}")
    return os.path.join(src, "lunar")


def table(header, rows):
    out = ["| " + " | ".join(header) + " |", "|" + "---|" * len(header)]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--lunar", default=os.path.join(ROOT, "lunar"))
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--only", nargs="*", help="file names, e.g. fib_32 nbody_100k")
    ap.add_argument("--no-history", action="store_true", help="skip the Lunar-over-time table")
    args = ap.parse_args()

    lua = shutil.which("lua5.4") or shutil.which("lua")
    python = shutil.which("python3")
    langs = [("Lunar", [args.lunar], "lunar"), ("Lua", [lua], "lua"), ("Python", [python], "py")]
    programs = [p for p in PROGRAMS if not args.only or p[1] in args.only]

    rows = []
    for label, name in programs:
        results, outs = [], []
        for lang, cmd, ext in langs:
            print(f"  {label} / {lang}", file=sys.stderr)
            res = measure(cmd + [os.path.join(BENCH, f"{name}.{ext}")], args.runs)
            results.append(fmt(res))
            outs.append(res[2])
        if not same_number(outs):
            sys.exit(f"{name}: outputs differ: {outs}")
        rows.append([label] + results)
    parts = [table(["Benchmark", "Lunar", "Lua", "Python"], rows)]

    machine = f"{cpu_name()} ({platform.system()} {platform.machine()})"
    parts.append(f"Measured on {machine}, mean of {args.runs} runs ± stddev, wall-clock time "
                 "including process startup. Lower is better.")
    parts.append(f"Versions: Lunar (local build), {version([lua, '-v']).split('  ')[0]}, "
                 f"{version([python, '--version'])}.")

    if not args.no_history:
        tmp = tempfile.mkdtemp()
        try:
            builds = [(label, build(ref, tmp)) for label, ref in HISTORY]
            builds.append(("now", args.lunar))
            hist = []
            for label, name in PROGRAMS[:ORIGINAL]:
                row = [label]
                for vlabel, binary in builds:
                    print(f"  {label} / Lunar {vlabel}", file=sys.stderr)
                    row.append(fmt(measure([binary, os.path.join(BENCH, f"{name}.lunar")], args.runs)))
                hist.append(row)
        finally:
            shutil.rmtree(tmp)
        parts.append("**Lunar over time** (the first five benchmarks; each version built from git with `make`):")
        parts.append(table(["Benchmark"] + [v for v, _ in builds], hist))

    print("\n\n".join(parts))


if __name__ == "__main__":
    main()
