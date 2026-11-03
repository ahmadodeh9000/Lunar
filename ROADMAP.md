# Lunar Roadmap

Lunar is a small, friendly language for games, tools, and scripts. Each milestone gets a written spec, then tests, then docs.

## Phase 1–2: core features and modules

- [x] **M1** `break` / `continue`
- [x] **M2** Remove the 256-constant and 255-element array literal limits
- [x] **M3** Maps: `{ "key": value }`, `m["key"]`, `has` / `keys` / `remove` / `len`
- [x] **M4** `for x in collection`
- [ ] **M5** Errors: `throw`, `try` / `catch`, built-ins throw on bad arguments
- [ ] **M6** String interpolation and escapes: `"hp: {hp}"`, `\n` `\t` `\"` `\\` `\{`
- [ ] **M7** Default and variadic parameters
- [ ] **M8** Multiple return values
- [ ] **M9** Modules: `import("path.lunar")`

## Later

Known issues and ideas that are not part of a milestone yet.

- Number literals with an exponent (`1e3`) don't parse.
- Hash-based constant dedup in the compiler. `make_const` scans linearly, so compiling 100k constants takes 6.4 s (20k: 0.26 s).
- Friendlier error when a map literal starts a statement, e.g. `{ "a": 1 };`. Today it reports `Expect ';' after expression.`; it should say "A map literal can't start a statement; wrap it in parentheses."
- Decide the string model: whether `len`, indexing, and slicing count bytes or characters (Phase 3 string module). for-in already walks UTF-8 characters, and `for (i, c in s)` is a runtime error until this is decided.
- `define_native` assumes an empty VM stack (it uses slots 0 and 1). Fix it when built-in modules arrive.
