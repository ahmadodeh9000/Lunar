# Lunar Roadmap

Lunar is a small, friendly language for games, tools, and scripts. Each milestone gets a written spec, then tests, then docs.

## Phase 1–2: core features and modules

- [x] **M1** `break` / `continue`
- [x] **M2** Remove the 256-constant and 255-element array literal limits
- [x] **M3** Maps: `{ "key": value }`, `m["key"]`, `has` / `keys` / `remove` / `len`
- [x] **M4** `for x in collection`
- [x] **M5** Errors: `throw`, `try` / `catch`, built-ins throw on bad arguments
- [x] **M6** String interpolation and escapes: `"hp: {hp}"`, `\n` `\t` `\"` `\\` `\{`
- ~~**M8** Multiple return values~~ (dropped)
- [x] **M9** Modules: `import("path.lunar")`

## Phase 3: built-in modules

- [x] **P3.0** Foundation: `import("name")` opens a built-in module once and caches its map; `define_native` works at any stack depth
- [x] **P3.1** `string`: split, join, find, replace, slice, case, trim, chars, to_number, bytes (Go-style: lengths and indexes count bytes)
- [ ] **P3.2** `fs`: read, write, append, exists, is_dir, list, remove, mkdir, lines
- [ ] **P3.3** `os`: args, env, exit, run, time, clock, sleep, platform, cwd, input
- [ ] `examples/tools/`: a word-count CLI using all three modules
- [ ] **P3.4** Raise `FRAMES_MAX` from 64 to 1024 and grow the value stack to match, so deeply recursive programs (such as an interpreter written in Lunar) can run. Must not slow down `fib_32` or `method_calls_1m`.
- [ ] **P3.5** Computed-goto dispatch in `run()` (GCC/clang labels-as-values), falling back to the current switch on other compilers. Done together with P3.4, since both touch `run()`. Goal: faster and more stable benchmarks; report variance as well as medians.

## Later

Known issues and ideas that are not part of a milestone yet.

- Number literals with an exponent (`1e3`) don't parse.
- Hash-based constant dedup in the compiler. `make_const` scans linearly, so compiling 100k constants takes 6.4 s (20k: 0.26 s).
- Friendlier error when a map literal starts a statement, e.g. `{ "a": 1 };`. Today it reports `Expect ';' after expression.`; it should say "A map literal can't start a statement; wrap it in parentheses."
- `for (i, c in s)` is still a runtime error. With the byte model chosen in P3.1, `i` could be the character's byte index, as in Go.
- Default and variadic parameters: `fn f(a, b = 10)`, `fn sum(...nums)` (was M7).
- `finally` for try / catch.
- More string escapes: `\r`, `\0`, and `\u{...}` for Unicode code points.
- Errors thrown inside a Lunar callback called from a native (e.g. a future `sort` with a compare function) must propagate correctly across the C call. Today no native calls back into Lunar.
- Windows build: the module code uses `realpath`, `getcwd`, and `stat` (POSIX only).
