#!/usr/bin/env python3
"""
Lunar benchmark runner #2: different workloads, different languages.

Workloads: tak (recursion), mandelbrot (float math), sieve (array writes),
matmul (nested arrays), binary trees (allocation / GC).
Languages: Lunar, Wren, Ruby, Node.js, Lua, Python (missing ones are skipped).

Prints two markdown tables: absolute times, and time relative to Lunar
(< 1.00x means faster than Lunar). With --update-readme it writes both
between these markers in README.md:

    <!-- BENCH2:START -->
    <!-- BENCH2:END -->

Usage (from the repo root):
    python3 bench2.py
    python3 bench2.py --runs 7 --update-readme
    python3 bench2.py --only tak sieve --langs Lunar Lua Node
"""

import argparse
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import time

# --------------------------------------------------------------------------
# Programs. Every one prints a single number so outputs can be cross-checked.
# Only +, -, *, <, >, and float literals are used for arithmetic, so there is
# no integer-vs-float division ambiguity between languages.
# --------------------------------------------------------------------------

BENCHMARKS = {
    "tak(24,16,8)": {
        "Lunar": """
fn tak(x, y, z) {
    if (y < x) ret tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
    ret z;
}
print tak(24, 16, 8);
""",
        "Wren": """
class B {
  static tak(x, y, z) {
    if (y < x) return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y))
    return z
  }
}
System.print(B.tak(24, 16, 8))
""",
        "Ruby": """
def tak(x, y, z)
  return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y)) if y < x
  z
end
puts tak(24, 16, 8)
""",
        "Node": """
function tak(x, y, z) {
  if (y < x) return tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y));
  return z;
}
console.log(tak(24, 16, 8));
""",
        "Lua": """
local function tak(x, y, z)
  if y < x then return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y)) end
  return z
end
print(tak(24, 16, 8))
""",
        "Python": """
def tak(x, y, z):
    if y < x:
        return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y))
    return z
print(tak(24, 16, 8))
""",
    },
    "mandelbrot (300x300)": {
        "Lunar": """
let count = 0;
for (let py = 0; py < 300; py = py + 1) {
    for (let px = 0; px < 300; px = px + 1) {
        let cr = px * 0.01 - 2.0;
        let ci = py * 0.01 - 1.5;
        let zr = 0.0;
        let zi = 0.0;
        let k = 0;
        let inside = 1;
        while (k < 50) {
            let t = zr * zr - zi * zi + cr;
            zi = 2.0 * zr * zi + ci;
            zr = t;
            if (4.0 < zr * zr + zi * zi) { inside = 0; k = 50; }
            k = k + 1;
        }
        count = count + inside;
    }
}
print count;
""",
        "Wren": """
var count = 0
var py = 0
while (py < 300) {
  var px = 0
  while (px < 300) {
    var cr = px * 0.01 - 2.0
    var ci = py * 0.01 - 1.5
    var zr = 0.0
    var zi = 0.0
    var k = 0
    var inside = 1
    while (k < 50) {
      var t = zr * zr - zi * zi + cr
      zi = 2.0 * zr * zi + ci
      zr = t
      if (4.0 < zr * zr + zi * zi) {
        inside = 0
        k = 50
      }
      k = k + 1
    }
    count = count + inside
    px = px + 1
  }
  py = py + 1
}
System.print(count)
""",
        "Ruby": """
count = 0
py = 0
while py < 300
  px = 0
  while px < 300
    cr = px * 0.01 - 2.0
    ci = py * 0.01 - 1.5
    zr = 0.0
    zi = 0.0
    k = 0
    inside = 1
    while k < 50
      t = zr * zr - zi * zi + cr
      zi = 2.0 * zr * zi + ci
      zr = t
      if 4.0 < zr * zr + zi * zi
        inside = 0
        k = 50
      end
      k += 1
    end
    count += inside
    px += 1
  end
  py += 1
end
puts count
""",
        "Node": """
let count = 0;
for (let py = 0; py < 300; py++) {
  for (let px = 0; px < 300; px++) {
    const cr = px * 0.01 - 2.0, ci = py * 0.01 - 1.5;
    let zr = 0.0, zi = 0.0, k = 0, inside = 1;
    while (k < 50) {
      const t = zr * zr - zi * zi + cr;
      zi = 2.0 * zr * zi + ci;
      zr = t;
      if (4.0 < zr * zr + zi * zi) { inside = 0; k = 50; }
      k++;
    }
    count += inside;
  }
}
console.log(count);
""",
        "Lua": """
local count = 0
for py = 0, 299 do
  for px = 0, 299 do
    local cr = px * 0.01 - 2.0
    local ci = py * 0.01 - 1.5
    local zr, zi, k, inside = 0.0, 0.0, 0, 1
    while k < 50 do
      local t = zr * zr - zi * zi + cr
      zi = 2.0 * zr * zi + ci
      zr = t
      if 4.0 < zr * zr + zi * zi then inside = 0; k = 50 end
      k = k + 1
    end
    count = count + inside
  end
end
print(count)
""",
        "Python": """
count = 0
for py in range(300):
    for px in range(300):
        cr = px * 0.01 - 2.0
        ci = py * 0.01 - 1.5
        zr = 0.0
        zi = 0.0
        k = 0
        inside = 1
        while k < 50:
            t = zr * zr - zi * zi + cr
            zi = 2.0 * zr * zi + ci
            zr = t
            if 4.0 < zr * zr + zi * zi:
                inside = 0
                k = 50
            k = k + 1
        count = count + inside
print(count)
""",
    },
    "sieve (2M)": {
        "Lunar": """
let n = 2000000;
let a = [];
for (let i = 0; i < n + 1; i = i + 1) { push(a, 1); }
let i = 2;
while (i * i < n + 1) {
    if (0 < a[i]) {
        let j = i * i;
        while (j < n + 1) { a[j] = 0; j = j + i; }
    }
    i = i + 1;
}
let count = 0;
for (let k = 2; k < n + 1; k = k + 1) { count = count + a[k]; }
print count;
""",
        "Wren": """
var n = 2000000
var a = []
var i = 0
while (i < n + 1) {
  a.add(1)
  i = i + 1
}
i = 2
while (i * i < n + 1) {
  if (0 < a[i]) {
    var j = i * i
    while (j < n + 1) {
      a[j] = 0
      j = j + i
    }
  }
  i = i + 1
}
var count = 0
var k = 2
while (k < n + 1) {
  count = count + a[k]
  k = k + 1
}
System.print(count)
""",
        "Ruby": """
n = 2000000
a = []
i = 0
while i < n + 1
  a << 1
  i += 1
end
i = 2
while i * i < n + 1
  if 0 < a[i]
    j = i * i
    while j < n + 1
      a[j] = 0
      j += i
    end
  end
  i += 1
end
count = 0
k = 2
while k < n + 1
  count += a[k]
  k += 1
end
puts count
""",
        "Node": """
const n = 2000000;
const a = [];
for (let i = 0; i < n + 1; i++) a.push(1);
for (let i = 2; i * i < n + 1; i++) {
  if (0 < a[i]) {
    for (let j = i * i; j < n + 1; j += i) a[j] = 0;
  }
}
let count = 0;
for (let k = 2; k < n + 1; k++) count += a[k];
console.log(count);
""",
        "Lua": """
local n = 2000000
local a = {}
for i = 0, n do a[i] = 1 end
local i = 2
while i * i < n + 1 do
  if 0 < a[i] then
    local j = i * i
    while j < n + 1 do a[j] = 0; j = j + i end
  end
  i = i + 1
end
local count = 0
for k = 2, n do count = count + a[k] end
print(count)
""",
        "Python": """
n = 2000000
a = []
for i in range(n + 1):
    a.append(1)
i = 2
while i * i < n + 1:
    if 0 < a[i]:
        j = i * i
        while j < n + 1:
            a[j] = 0
            j = j + i
    i = i + 1
count = 0
for k in range(2, n + 1):
    count = count + a[k]
print(count)
""",
    },
    "matmul (100x100)": {
        "Lunar": """
fn mk(n, off) {
    let m = [];
    for (let i = 0; i < n; i = i + 1) {
        let r = [];
        for (let j = 0; j < n; j = j + 1) { push(r, i + j + off); }
        push(m, r);
    }
    ret m;
}
let n = 100;
let a = mk(n, 0);
let b = mk(n, 1);
let total = 0;
for (let i = 0; i < n; i = i + 1) {
    for (let j = 0; j < n; j = j + 1) {
        let s = 0;
        for (let k = 0; k < n; k = k + 1) { s = s + a[i][k] * b[k][j]; }
        total = total + s;
    }
}
print total;
""",
        "Wren": """
var mk = Fn.new {|n, off|
  var m = []
  for (i in 0...n) {
    var r = []
    for (j in 0...n) r.add(i + j + off)
    m.add(r)
  }
  return m
}
var n = 100
var a = mk.call(n, 0)
var b = mk.call(n, 1)
var total = 0
for (i in 0...n) {
  for (j in 0...n) {
    var s = 0
    for (k in 0...n) s = s + a[i][k] * b[k][j]
    total = total + s
  }
}
System.print(total)
""",
        "Ruby": """
def mk(n, off)
  m = []
  n.times do |i|
    r = []
    n.times { |j| r << (i + j + off) }
    m << r
  end
  m
end
n = 100
a = mk(n, 0)
b = mk(n, 1)
total = 0
n.times do |i|
  n.times do |j|
    s = 0
    n.times { |k| s += a[i][k] * b[k][j] }
    total += s
  end
end
puts total
""",
        "Node": """
function mk(n, off) {
  const m = [];
  for (let i = 0; i < n; i++) {
    const r = [];
    for (let j = 0; j < n; j++) r.push(i + j + off);
    m.push(r);
  }
  return m;
}
const n = 100, a = mk(n, 0), b = mk(n, 1);
let total = 0;
for (let i = 0; i < n; i++)
  for (let j = 0; j < n; j++) {
    let s = 0;
    for (let k = 0; k < n; k++) s += a[i][k] * b[k][j];
    total += s;
  }
console.log(total);
""",
        "Lua": """
local function mk(n, off)
  local m = {}
  for i = 0, n - 1 do
    local r = {}
    for j = 0, n - 1 do r[#r+1] = i + j + off end
    m[#m+1] = r
  end
  return m
end
local n = 100
local a, b = mk(n, 0), mk(n, 1)
local total = 0
for i = 1, n do
  for j = 1, n do
    local s = 0
    for k = 1, n do s = s + a[i][k] * b[k][j] end
    total = total + s
  end
end
print(string.format("%d", total))
""",
        "Python": """
def mk(n, off):
    m = []
    for i in range(n):
        r = []
        for j in range(n):
            r.append(i + j + off)
        m.append(r)
    return m
n = 100
a = mk(n, 0)
b = mk(n, 1)
total = 0
for i in range(n):
    for j in range(n):
        s = 0
        for k in range(n):
            s = s + a[i][k] * b[k][j]
        total = total + s
print(total)
""",
    },
    "binary trees (d15 x8)": {
        "Lunar": """
struct Node {
    init(l, r) { self.l = l; self.r = r; }
}
fn make(d) {
    if (d < 1) ret Node(0, 0);
    ret Node(make(d - 1), make(d - 1));
}
fn check(node, d) {
    if (d < 1) ret 1;
    ret 1 + check(node.l, d - 1) + check(node.r, d - 1);
}
let total = 0;
for (let i = 0; i < 8; i = i + 1) { total = total + check(make(15), 15); }
print total;
""",
        "Wren": """
class Node {
  construct new(l, r) {
    _l = l
    _r = r
  }
  l { _l }
  r { _r }
}
var make
make = Fn.new {|d|
  if (d < 1) return Node.new(0, 0)
  return Node.new(make.call(d - 1), make.call(d - 1))
}
var check
check = Fn.new {|node, d|
  if (d < 1) return 1
  return 1 + check.call(node.l, d - 1) + check.call(node.r, d - 1)
}
var total = 0
for (i in 0...8) total = total + check.call(make.call(15), 15)
System.print(total)
""",
        "Ruby": """
class Node
  attr_reader :l, :r
  def initialize(l, r)
    @l = l
    @r = r
  end
end
def make(d)
  return Node.new(0, 0) if d < 1
  Node.new(make(d - 1), make(d - 1))
end
def check(node, d)
  return 1 if d < 1
  1 + check(node.l, d - 1) + check(node.r, d - 1)
end
total = 0
8.times { total += check(make(15), 15) }
puts total
""",
        "Node": """
class Node {
  constructor(l, r) { this.l = l; this.r = r; }
}
function make(d) {
  if (d < 1) return new Node(0, 0);
  return new Node(make(d - 1), make(d - 1));
}
function check(node, d) {
  if (d < 1) return 1;
  return 1 + check(node.l, d - 1) + check(node.r, d - 1);
}
let total = 0;
for (let i = 0; i < 8; i++) total += check(make(15), 15);
console.log(total);
""",
        "Lua": """
local function make(d)
  if d < 1 then return {l = 0, r = 0} end
  return {l = make(d - 1), r = make(d - 1)}
end
local function check(node, d)
  if d < 1 then return 1 end
  return 1 + check(node.l, d - 1) + check(node.r, d - 1)
end
local total = 0
for i = 1, 8 do total = total + check(make(15), 15) end
print(total)
""",
        "Python": """
class Node:
    __slots__ = ("l", "r")
    def __init__(self, l, r):
        self.l = l
        self.r = r
def make(d):
    if d < 1:
        return Node(0, 0)
    return Node(make(d - 1), make(d - 1))
def check(node, d):
    if d < 1:
        return 1
    return 1 + check(node.l, d - 1) + check(node.r, d - 1)
total = 0
for i in range(8):
    total = total + check(make(15), 15)
print(total)
""",
    },
}

EXT = {"Lunar": "lunar", "Wren": "wren", "Ruby": "rb",
       "Node": "js", "Lua": "lua", "Python": "py"}
VERSION_FLAG = {"Wren": ["--version"], "Ruby": ["-v"], "Node": ["-v"],
                "Lua": ["-v"], "Python": ["--version"]}
START, END = "<!-- BENCH2:START -->", "<!-- BENCH2:END -->"


# --------------------------------------------------------------------------


def first_found(names):
    for name in names:
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
    """Equal strings, or numerically equal within 1e-5 relative (Lunar's
    print may use %g, so 2.17831e+06 should match 2178309)."""
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
    """Returns (mean, stdev, output) or None on failure / timeout."""
    try:
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
    except subprocess.TimeoutExpired:
        print(f"    ! timed out after {timeout}s")
        return None
    sd = statistics.stdev(times) if len(times) > 1 else 0.0
    return statistics.mean(times), sd, output


def fmt_abs(res):
    if res is None:
        return "n/a"
    return f"{res[0]:.3f} s ± {res[1]:.3f}"


def fmt_rel(res, base):
    if res is None or base is None:
        return "n/a"
    return f"{res[0] / base[0]:.2f}x"


def table(header, rows):
    sep = "|---|" + "---|" * (len(header) - 1)
    out = ["| " + " | ".join(header) + " |", sep]
    out += ["| " + " | ".join(r) + " |" for r in rows]
    return "\n".join(out)


def build_tables(names, results, langs):
    abs_rows = [[n] + [fmt_abs(results[n].get(l)) for l in langs] for n in names]
    rel_rows = [[n] + [fmt_rel(results[n].get(l), results[n].get("Lunar"))
                       for l in langs] for n in names]

    # geometric mean of the ratio vs Lunar, over benchmarks where both ran
    gm = ["**geomean**"]
    for l in langs:
        ratios = []
        for n in names:
            r, b = results[n].get(l), results[n].get("Lunar")
            if r and b and b[0] > 0:
                ratios.append(r[0] / b[0])
        gm.append(f"**{math.exp(sum(map(math.log, ratios)) / len(ratios)):.2f}x**"
                  if ratios else "n/a")
    rel_rows.append(gm)

    header = ["Benchmark"] + langs
    return table(header, abs_rows), table(header, rel_rows)


def update_readme(path, block):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if START in text and END in text:
        new = re.sub(re.escape(START) + r".*?" + re.escape(END),
                     lambda _: block, text, flags=re.S)
    else:
        new = text.rstrip() + "\n\n## Benchmarks (cross-language)\n\n" + block + "\n"
    with open(path, "w", encoding="utf-8") as f:
        f.write(new)
    print(f"\nUpdated {path}")


def main():
    ap = argparse.ArgumentParser(description="Benchmark Lunar vs Wren/Ruby/Node/Lua/Python")
    ap.add_argument("--lunar", default="./lunar", help="path to lunar binary")
    ap.add_argument("--runs", type=int, default=5)
    ap.add_argument("--warmup", type=int, default=1)
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--only", nargs="*", help="substrings of benchmark names")
    ap.add_argument("--langs", nargs="*", help="restrict to these languages")
    ap.add_argument("--dir", default="bench2", help="where to write the programs")
    ap.add_argument("--readme", default="README.md")
    ap.add_argument("--update-readme", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(args.lunar):
        sys.exit(f"Lunar binary not found at {args.lunar} (run `make` first, "
                 "without -DLUNAR_DEBUG_STRESS_GC).")

    candidates = {
        "Lunar": [args.lunar],
        "Wren": ["wren_cli", "wren"],
        "Ruby": ["ruby"],
        "Node": ["node", "nodejs"],
        "Lua": ["lua5.4", "lua", "luajit"],
        "Python": [sys.executable],
    }
    runners = {}
    for lang, names in candidates.items():
        if args.langs and lang.lower() not in [x.lower() for x in args.langs] \
                and lang != "Lunar":
            continue
        exe = first_found(names) if lang != "Lunar" else args.lunar
        if exe:
            runners[lang] = exe
        else:
            print(f"{lang} not found, skipping it.")

    os.makedirs(args.dir, exist_ok=True)
    files = {}
    for name, progs in BENCHMARKS.items():
        slug = re.sub(r"[^a-z0-9]+", "_", name.lower()).strip("_")
        files[name] = {}
        for lang, code in progs.items():
            path = os.path.join(args.dir, f"{slug}.{EXT[lang]}")
            with open(path, "w") as f:
                f.write(code.lstrip("\n"))
            files[name][lang] = path

    names = [n for n in BENCHMARKS
             if not args.only or any(o.lower() in n.lower() for o in args.only)]

    print(f"Runs: {args.runs} (+{args.warmup} warmup), timeout {args.timeout}s\n")
    results = {}
    for n in names:
        print(n)
        results[n] = {}
        outputs = {}
        for lang, exe in runners.items():
            print(f"  {lang:<7}", end="", flush=True)
            res = bench([exe, files[n][lang]], args.runs, args.warmup, args.timeout)
            results[n][lang] = res
            if res:
                outputs[lang] = res[2]
                print(f" {res[0]:.3f}s ± {res[1]:.3f}")
        if not outputs_match(list(outputs.values())):
            print(f"  ! WARNING: outputs differ between languages: {outputs}")

    langs = list(runners)
    abs_table, rel_table = build_tables(names, results, langs)

    versions = ["Lunar (local build)"]
    for lang in langs:
        if lang == "Lunar":
            continue
        if lang == "Python":
            versions.append(f"Python {platform.python_version()}")
        else:
            versions.append(version_of([runners[lang]] + VERSION_FLAG[lang]))

    info = [
        f"Measured on {cpu_name()} ({platform.system()} {platform.machine()}), "
        f"mean of {args.runs} runs ± stddev, wall-clock including process "
        "startup. Lower is better.",
        "",
        "Versions: " + ", ".join(versions) + ".",
        "",
        "Reproduce: `python3 bench2.py`",
    ]
    block = (f"{START}\n**Absolute time**\n\n{abs_table}\n\n"
             f"**Relative to Lunar** (below 1.00x = faster than Lunar)\n\n"
             f"{rel_table}\n\n" + "\n".join(info) + f"\n{END}")

    print("\n" + abs_table + "\n\n" + rel_table + "\n")
    print("\n".join(info))

    if args.update_readme:
        update_readme(args.readme, block)


if __name__ == "__main__":
    main()