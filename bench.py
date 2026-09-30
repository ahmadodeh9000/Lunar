#!/usr/bin/env python3
"""
Lunar benchmark runner.

Generates equivalent benchmark programs for Lunar, Lua and Python, runs each
several times, and prints a markdown table. With --update-readme it also
writes the table into README.md between these markers:

    <!-- BENCH:START -->
    <!-- BENCH:END -->

Usage (from the repo root):
    python3 bench.py
    python3 bench.py --runs 7 --update-readme
    python3 bench.py --lunar ./lunar --lua lua5.4 --only fib loop
"""

import argparse
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time

# --------------------------------------------------------------------------
# Benchmark programs. Each one prints a result so we can check that every
# language computed the same answer (catches bugs and dead-code elimination).
# --------------------------------------------------------------------------

BENCHMARKS = {
    "fib(32)": {
        "lunar": """
fn fib(n) {
    if (n < 2) ret n;
    ret fib(n - 1) + fib(n - 2);
}
print fib(32);
""",
        "lua": """
local function fib(n)
  if n < 2 then return n end
  return fib(n-1) + fib(n-2)
end
print(fib(32))
""",
        "python": """
def fib(n):
    return n if n < 2 else fib(n-1) + fib(n-2)
print(fib(32))
""",
    },
    "loop (10M)": {
        "lunar": """
let sum = 0;
let i = 0;
while (i < 10000000) {
    sum = sum + i;
    i = i + 1;
}
print sum;
""",
        "lua": """
local sum = 0
local i = 0
while i < 10000000 do
  sum = sum + i
  i = i + 1
end
print(string.format("%d", sum))
""",
        "python": """
s = 0
i = 0
while i < 10000000:
    s = s + i
    i = i + 1
print(s)
""",
    },
    "array (1M)": {
        "lunar": """
let a = [];
for (let i = 0; i < 1000000; i = i + 1) { push(a, i); }
let total = 0;
for (let i = 0; i < len(a); i = i + 1) { total = total + a[i]; }
print total;
""",
        "lua": """
local a = {}
for i = 0, 999999 do a[#a+1] = i end
local total = 0
for i = 1, #a do total = total + a[i] end
print(string.format("%d", total))
""",
        "python": """
a = []
for i in range(1000000):
    a.append(i)
total = 0
for i in range(len(a)):
    total = total + a[i]
print(total)
""",
    },
    "method calls (1M)": {
        "lunar": """
struct Counter {
    init() { self.n = 0; }
    inc() { self.n = self.n + 1; }
}
let c = Counter();
for (let i = 0; i < 1000000; i = i + 1) { c.inc(); }
print c.n;
""",
        "lua": """
local Counter = {}
Counter.__index = Counter
function Counter.new() return setmetatable({n = 0}, Counter) end
function Counter:inc() self.n = self.n + 1 end
local c = Counter.new()
for i = 1, 1000000 do c:inc() end
print(c.n)
""",
        "python": """
class Counter:
    def __init__(self):
        self.n = 0
    def inc(self):
        self.n = self.n + 1
c = Counter()
for i in range(1000000):
    c.inc()
print(c.n)
""",
    },
    "closures (1M)": {
        "lunar": """
fn make() {
    let n = 0;
    fn inc() { n = n + 1; ret n; }
    ret inc;
}
let f = make();
let r = 0;
for (let i = 0; i < 1000000; i = i + 1) { r = f(); }
print r;
""",
        "lua": """
local function make()
  local n = 0
  return function() n = n + 1; return n end
end
local f = make()
local r = 0
for i = 1, 1000000 do r = f() end
print(r)
""",
        "python": """
def make():
    n = 0
    def inc():
        nonlocal n
        n = n + 1
        return n
    return inc
f = make()
r = 0
for i in range(1000000):
    r = f()
print(r)
""",
    },
}

EXT = {"lunar": "lunar", "lua": "lua", "python": "py"}
START, END = "<!-- BENCH:START -->", "<!-- BENCH:END -->"


# --------------------------------------------------------------------------


def find_lua(preferred):
    for name in ([preferred] if preferred else []) + ["lua5.4", "lua", "luajit"]:
        if name and shutil.which(name):
            return shutil.which(name)
    return None


def version_of(cmd):
    try:
        out = subprocess.run(cmd, capture_output=True, text=True, timeout=10)
        text = (out.stdout + out.stderr).strip().splitlines()
        return text[0] if text else "unknown"
    except Exception:
        return "unknown"


def cpu_name():
    try:
        if sys.platform == "darwin":
            return subprocess.run(
                ["sysctl", "-n", "machdep.cpu.brand_string"],
                capture_output=True, text=True).stdout.strip()
        if sys.platform.startswith("linux"):
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.startswith("model name"):
                        return line.split(":", 1)[1].strip()
    except Exception:
        pass
    return platform.processor() or platform.machine()


def outputs_match(outs):
    """True if all outputs are equal, treating numbers with a tolerance
    (so '2.17831e+06' matches '2178309' if print() only shows %g precision)."""
    if len(set(outs)) <= 1:
        return True
    try:
        vals = [float(o) for o in outs]
    except ValueError:
        return False
    ref = vals[0]
    return all(abs(v - ref) <= 1e-5 * max(abs(ref), abs(v), 1.0) for v in vals)


def run_once(cmd, timeout):
    t0 = time.perf_counter()
    p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
    dt = time.perf_counter() - t0
    return dt, p.returncode, p.stdout.strip(), p.stderr.strip()


def bench(cmd, runs, warmup, timeout):
    """Returns (mean, stdev, output) or None on failure."""
    for _ in range(warmup):
        _, rc, _, err = run_once(cmd, timeout)
        if rc != 0:
            print(f"    ! failed: {err[:200]}")
            return None
    times, output = [], ""
    for _ in range(runs):
        dt, rc, out, err = run_once(cmd, timeout)
        if rc != 0:
            print(f"    ! failed: {err[:200]}")
            return None
        times.append(dt)
        output = out
    sd = statistics.stdev(times) if len(times) > 1 else 0.0
    return statistics.mean(times), sd, output


def fmt(res):
    if res is None:
        return "n/a"
    mean, sd, _ = res
    return f"{mean:.3f} s ± {sd:.3f}"


def build_table(names, results, langs):
    header = "| Benchmark | " + " | ".join(langs) + " |"
    sep = "|---|" + "---|" * len(langs)
    rows = []
    for n in names:
        cells = []
        for lang in langs:
            r = results[n].get(lang)
            cells.append(fmt(r))
        rows.append(f"| {n} | " + " | ".join(cells) + " |")
    return "\n".join([header, sep] + rows)


def update_readme(path, block):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if START in text and END in text:
        new = re.sub(
            re.escape(START) + r".*?" + re.escape(END),
            lambda _: block, text, flags=re.S)
    else:
        new = text.rstrip() + "\n\n## Benchmarks\n\n" + block + "\n"
    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    print(f"\nUpdated {path}")


def main():
    ap = argparse.ArgumentParser(description="Benchmark Lunar vs Lua vs Python")
    ap.add_argument("--lunar", default="./lunar", help="path to lunar binary")
    ap.add_argument("--lua", default=None, help="lua command (auto-detected)")
    ap.add_argument("--python", default=sys.executable, help="python command")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--only", nargs="*", help="substrings of benchmark names")
    ap.add_argument("--dir", default="bench", help="where to write the programs")
    ap.add_argument("--readme", default="README.md")
    ap.add_argument("--update-readme", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.lunar):
        sys.exit(f"Lunar binary not found at {args.lunar} (run `make` first, "
                 "without -DLUNAR_DEBUG_STRESS_GC).")

    runners = {"Lunar": args.lunar}
    lua = find_lua(args.lua)
    if lua:
        runners["Lua"] = lua
    else:
        print("Lua not found, skipping it.")
    runners["Python"] = args.python

    # write the programs to disk
    os.makedirs(args.dir, exist_ok=True)
    files = {}
    for name, progs in BENCHMARKS.items():
        slug = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
        files[name] = {}
        for lang, code in progs.items():
            path = os.path.join(args.dir, f"{slug}.{EXT[lang]}")
            with open(path, "w") as f:
                f.write(code.lstrip("\n"))
            files[name][lang.capitalize()] = path

    names = [n for n in BENCHMARKS
             if not args.only or any(o.lower() in n.lower() for o in args.only)]

    print(f"Runs: {args.runs} (+{args.warmup} warmup), timeout {args.timeout}s\n")
    results = {}
    for n in names:
        print(f"{n}")
        results[n] = {}
        outputs = {}
        for lang, exe in runners.items():
            path = files[n][lang]
            print(f"  {lang:<7}", end="", flush=True)
            res = bench([exe, path], args.runs, args.warmup, args.timeout)
            results[n][lang] = res
            if res:
                outputs[lang] = res[2]
                print(f" {res[0]:.3f}s ± {res[1]:.3f}")
        if not outputs_match(list(outputs.values())):
            print(f"  ! WARNING: outputs differ between languages: {outputs}")

    langs = list(runners)
    table = build_table(names, results, langs)

    lunar_ver = "Lunar (local build)"
    info = [
        f"Measured on {cpu_name()} ({platform.system()} {platform.machine()}), "
        f"mean of {args.runs} runs ± stddev, wall-clock time including "
        "process startup. Lower is better.",
        "",
        f"Versions: {lunar_ver}"
        + (f", {version_of([lua, '-v'])}" if lua else "")
        + f", Python {platform.python_version()}.",
        "",
        "Reproduce: `python3 bench.py`",
    ]
    block = f"{START}\n{table}\n\n" + "\n".join(info) + f"\n{END}"

    print("\n" + table + "\n")
    print("\n".join(info))

    if args.update_readme:
        update_readme(args.readme, block)


if __name__ == "__main__":
    main()