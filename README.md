# Lunar

A fast, dynamically-typed scripting language with a bytecode VM, written in C from scratch.  
Influenced by Lua and JavaScript. Built following [Crafting Interpreters](https://craftinginterpreters.com/) Part II with extensions.

```lunar
struct Dog < Animal {
    speak() {
        print "Woof!";
        super.speak();
    }
}

let d = Dog("Rex");
d.speak();
```

---

## Features

- **Bytecode compiled** — source compiles to compact bytecode executed by a stack-based VM
- **Garbage collected** — tri-color mark-and-sweep GC with a gray stack
- **First-class functions & closures** — functions capture variables by reference across scopes
- **Structs & single inheritance** — `self`, `super`, bound methods, dynamic fields
- **Arrays** — literals, nested arrays, indexing and index assignment, `push` / `pop` / `len`
- **String interning** — strings deduplicated via FNV-1a hash table, pointer equality comparison
- **Bitwise operators** — `&` `|` `^` `~` `<<` `>>`
- **Foreign Function Interface** — call into native shared libraries via `libffi`
- **SDL2 support** — optional build target for 2D graphics, textures, and keyboard input

---

## Building

### Requirements

- GCC or Clang
- Make

### Standard build

```bash
git clone https://github.com/ahmadodeh9000/Lunar
cd Lunar
make
```

### Build with SDL2

Install SDL2 and SDL2_image first:

```bash
# Ubuntu / Debian
sudo apt install libsdl2-dev libsdl2-image-dev

# Arch
sudo pacman -S sdl2 sdl2_image

# macOS
brew install sdl2 sdl2_image
```

```bash
make sdl
```

---

## Usage

```bash
# Run a script
./lunar script.lunar

# Start the REPL
./lunar
```

---

## Language Tour

### Variables

```lunar
let x     = 10;
let name  = "Lunar";
let flag  = true;
let empty = nil;
```

Variables are block-scoped inside `{ }`, global otherwise. Reassign without `let`:

```lunar
x = 20;
```

---

### Types

| Type     | Example          | Notes                                          |
|----------|------------------|------------------------------------------------|
| Number   | `3.14`, `42`     | 64-bit IEEE 754 double                         |
| Bool     | `true`, `false`  |                                                |
| Nil      | `nil`            | Absence of a value                             |
| String   | `"hello"`        | UTF-8, multi-line supported                    |
| Array    | `[1, "a", nil]`  | Heap-allocated, mixed types, reference semantics |
| Function | `fn f() { ... }` | First-class closure                            |
| Instance | `Point(x, y)`    | Instance of a struct                           |

`nil` and `false` are falsey. Everything else is truthy, including empty arrays.

---

### Operators

**Arithmetic**
```lunar
10 + 3   // 13
10 - 3   // 7
10 * 3   // 30
10 / 3   // 3.333...
10 % 3   // 1
2 ** 8   // 256  (power, right-associative)
-x       // negation
```

**Comparison**
```lunar
x == y    x != y
x <  y    x >  y
x <= y    x >= y
```

**Logic**
```lunar
a && b   // and (short-circuits)
a || b   // or  (short-circuits)
!a       // not
```

**Bitwise** (truncates to 32-bit int)
```lunar
5 & 3    // 1   AND
5 | 3    // 7   OR
5 ^ 3    // 6   XOR
~5       // -6  NOT
1 << 3   // 8   left shift
16 >> 2  // 4   right shift
```

---

### Strings

```lunar
let s = "Hello, " + "Lunar!";
print len(s);      // 13
print str(42);     // "42"
```

Multi-line:
```lunar
let poem = "line one
line two
line three";
```

---

### Arrays

Arrays are ordered, zero-indexed, and can hold any mix of values, including other arrays.

```lunar
let empty  = [];
let nums   = [10, 20, 30];
let mixed  = [1, "two", nil, true, [5, 6]];
let padded = [1, 2, 3,];           // trailing comma is fine
```

**Indexing and assignment**

```lunar
print nums[0];          // 10
print nums[1 + 1];      // 30

nums[0] = 99;
print nums;             // [99, 20, 30]

// assignment is an expression
nums[0] = nums[1] = 7;
print nums;             // [7, 7, 30]
```

**Nesting**

```lunar
let grid = [[1, 2], [3, 4]];
print grid[1][0];       // 3
grid[0][1] = 20;
print grid;             // [[1, 20], [3, 4]]
```

**Growing and shrinking**

```lunar
let stack = [];
push(stack, 1);
push(stack, 2);
print stack;            // [1, 2]
print pop(stack);       // 2
print len(stack);       // 1
```

**Iterating**

```lunar
let total = 0;
let xs = [5, 10, 15];
for (let i = 0; i < len(xs); i = i + 1) {
    total = total + xs[i];
}
print total;            // 30
```

Arrays work with functions, closures, and struct fields like any other value:

```lunar
fn map(arr, f) {
    let out = [];
    for (let i = 0; i < len(arr); i = i + 1) {
        push(out, f(arr[i]));
    }
    ret out;
}

fn square(n) { ret n * n; }
print map([1, 2, 3], square);   // [1, 4, 9]
```

**Rules and behavior**

- **Reference semantics.** Assigning or passing an array shares it; mutations are visible through every reference.
  ```lunar
  let a = [1, 2];
  let b = a;
  b[0] = 100;
  print a;              // [100, 2]
  ```
- **Equality is by reference.** `[1] == [1]` is `false`; `a == b` above is `true`.
- **Indexes must be whole numbers within bounds.** `a[-1]`, `a[1.5]`, and `a[len(a)]` are runtime errors. Setting an index never grows the array; use `push`.
- **Only arrays can be indexed.** Indexing anything else is a runtime error, as is a non-number index.
- **A literal holds at most 255 elements.** Build larger arrays with `push`.
- **Printing.** `print` shows elements in brackets, with strings unquoted: `[1, a, nil]`.

---

### Control Flow

```lunar
if (x > 0) {
    print "positive";
} else if (x < 0) {
    print "negative";
} else {
    print "zero";
}
```

```lunar
let i = 0;
while (i < 5) {
    print i;
    i = i + 1;
}
```

```lunar
for (let i = 0; i < 5; i = i + 1) {
    print i;
}
```

---

### Functions

```lunar
fn add(a, b) {
    ret a + b;
}

print add(3, 4);   // 7
```

Functions are first-class values:

```lunar
fn apply(f, x) { ret f(x); }
fn double(n)   { ret n * 2; }

print apply(double, 5);   // 10
```

No explicit `ret` returns `nil`.

---

### Closures

Functions capture variables from their enclosing scope. Mutations are shared:

```lunar
fn make_counter() {
    let count = 0;
    fn inc() {
        count = count + 1;
        ret count;
    }
    ret inc;
}

let c = make_counter();
print c();   // 1
print c();   // 2
print c();   // 3
```

Closures can nest arbitrarily deep:

```lunar
fn outer() {
    let a = 1;
    fn middle() {
        let b = 2;
        fn inner() { ret a + b; }
        ret inner;
    }
    ret middle;
}

print outer()()();   // 3
```

---

### Structs

```lunar
struct Point {
    init(x, y) {
        self.x = x;
        self.y = y;
    }

    distance_to(other) {
        let dx = self.x - other.x;
        let dy = self.y - other.y;
        ret sqrt(dx*dx + dy*dy);
    }

    show() {
        print "(" + str(self.x) + ", " + str(self.y) + ")";
    }
}

let a = Point(0, 0);
let b = Point(3, 4);
a.show();                 // (0, 0)
print a.distance_to(b);  // 5
```

- `init` is the constructor — called automatically on `StructName(...)`
- `self` refers to the current instance inside any method
- Fields are set via `self.field` and can be added at any time
- Methods can be retrieved as bound values: `let f = a.show; f();`

Fields can hold arrays:

```lunar
struct Stack {
    init() { self.items = []; }
    push(v) { push(self.items, v); }
    top()   { ret self.items[len(self.items) - 1]; }
}

let s = Stack();
s.push(1);
s.push(2);
print s.top();            // 2
```

---

### Inheritance

```lunar
struct Animal {
    init(name) { self.name = name; }
    speak()    { print self.name; }
}

struct Dog < Animal {
    speak() {
        print "Woof!";
        super.speak();   // calls Animal.speak
    }
}

let d = Dog("Rex");
d.speak();
// Woof!
// Rex
```

- Single inheritance only
- `super.method()` calls the parent version of a method
- All parent methods are inherited unless overridden

---

### Recursion

```lunar
fn fib(n) {
    if (n <= 1) ret n;
    ret fib(n - 1) + fib(n - 2);
}

print fib(10);   // 55
```

```lunar
fn factorial(n) {
    if (n <= 1) ret 1;
    ret n * factorial(n - 1);
}

print factorial(10);   // 3628800
```

---

## Built-in Functions

| Function      | Signature                  | Description                                              |
|---------------|----------------------------|----------------------------------------------------------|
| `print`       | `print x`                  | Print value with newline                                 |
| `clock()`     | `() → number`              | Seconds since program start                              |
| `sqrt(x)`     | `(number) → number`        | Square root                                              |
| `abs(x)`      | `(number) → number`        | Absolute value                                           |
| `floor(x)`    | `(number) → number`        | Round toward negative infinity                           |
| `ceil(x)`     | `(number) → number`        | Round toward positive infinity                           |
| `str(x)`      | `(value) → string`         | Convert a number, bool, nil, string, or array to a string |
| `len(x)`      | `(string \| array) → number` | String length in bytes, or number of array elements    |
| `push(a, v)`  | `(array, any) → array`     | Append `v` to `a`; returns the array, so calls chain     |
| `pop(a)`      | `(array) → any`            | Remove and return the last element; `nil` if empty       |
| `hex(s)`      | `(string) → number`        | Parse a hexadecimal string                               |
| `random(x,y)` | `(r1,r2) → number`         | Random integer in [r1,r2]                                |

Built-ins called with the wrong argument types return `nil` rather than raising an error.

---

## Foreign Function Interface (FFI)

Lunar features a powerful, zero-boilerplate FFI powered by `libffi`. This allows you to load native system binaries or your own custom compiled C code dynamically at runtime, marshaling values automatically across the language boundary.

### Core Lifecycle

```lunar
clib(path)              // Loads a shared library handle (.dylib, .so, .dll)
                        // Note: Pass an empty string "" on macOS/Linux to look up system symbols

cbind(lib, name, ret_type, param1, param2, ...)
                        // Binds a native symbol and returns a callable closure
```

Example — calling a system function:

```lunar
// On macOS, passing an empty string to clib searches the current process
let libc = clib("");

if (libc) {
    print "Successfully loaded system library!";

    let c_puts = cbind(libc, "puts", "int", "string");

    if (c_puts) {
        print "Successfully bound 'puts' from C!";
        c_puts("Hello safely from the Lunar FFI pipeline!");
    } else {
        print "Failed to bind 'puts'.";
    }
} else {
    print "Failed to load system library.";
}
```

Example — calling your own compiled C code:

```c
// test_ffi.c
#include <stdio.h>

// We use attribute((visibility("default"))) to ensure macOS exports the symbol
__attribute__((visibility("default")))
void greet_ahmad(const char* greeting) {
    printf("%s, Ahmad!\n", greeting);
}

__attribute__((visibility("default")))
int add_numbers(int a, int b) {
    return a + b;
}
```

```bash
gcc -dynamiclib -o libcustom.dylib test_ffi.c
```

```lunar
// Load our local custom library by passing its relative path
let mylib = clib("./libcustom.dylib");

if (mylib) {
    print "Successfully loaded libcustom.dylib!";

    // 1. Bind our custom greeting function: void greet_ahmad(const char* greeting)
    let greet = cbind(mylib, "greet_ahmad", "void", "string");

    if (greet) {
        print "Calling greet_ahmad via FFI...";
        greet("Welcome back"); // Should print: Welcome back, Ahmad!
    } else {
        print "Failed to bind greet_ahmad.";
    }

    print "---------------------------------------";

    // 2. Bind our custom math function: int add_numbers(int a, int b)
    let add = cbind(mylib, "add_numbers", "int", "int", "int");

    if (add) {
        print "Calling add_numbers(15, 27)...";
        let sum = add(15, 27);
        print "Result from custom C library:";
        print sum; // Should print 42
    } else {
        print "Failed to bind add_numbers.";
    }
} else {
    print "Failed to load libcustom.dylib. Make sure the path is correct!";
}
```

Arrays can't be passed to FFI functions yet.

---

## SDL2 API

Only available when built with `make sdl` (requires SDL2 and SDL2_image).

### Lifecycle

```lunar
sdl_init("Title", 800, 600);   // open window + renderer, init SDL_image
sdl_quit();                     // destroy window, free textures, quit SDL
```

### Game Loop Pattern

```lunar
sdl_init("My Game", 800, 600);

let running = true;
while (running) {
    let e = sdl_poll();
    if (e == "quit") { running = false; }

    sdl_clear(30, 30, 30);
    // draw here
    sdl_present();
    sdl_delay(16);
}

sdl_quit();
```

### Drawing

```lunar
sdl_clear(r, g, b)                     // clear screen with color
sdl_fill_rect(x, y, w, h, r, g, b, a)  // filled rectangle
sdl_present()                          // flip buffer to screen
```

### Textures

Textures are referenced by an integer handle returned from `sdl_load_texture`.

```lunar
let tex = sdl_load_texture("player.png");  // returns handle, or -1 on failure
sdl_draw_texture(tex, x, y, w, h);         // draw scaled into a dest rect
sdl_texture_width(tex);                    // native pixel width
sdl_texture_height(tex);                   // native pixel height
```

Textures are freed automatically on `sdl_quit()`; there is currently no way to unload a single texture early.

### Input

```lunar
let e = sdl_poll();      // "quit" | nil

sdl_key_down("left")     // true if key currently held
                         // keys: "up" "down" "left" "right"
                         //       "space" "escape" "w" "a" "s" "d"
```

### Timing

```lunar
sdl_delay(16)  // sleep ms — use in loop for ~60fps cap
```

---

## Example — Pong

```lunar
let W = 800;
let H = 600;

sdl_init("Pong", W, H);

let PAD_W = 12;   let PAD_H  = 80;
let p1x   = 20;   let p1y    = H / 2 - PAD_H / 2;
let p2x   = W - 32; let p2y  = H / 2 - PAD_H / 2;
let bx    = W / 2;  let by   = H / 2;
let bvx   = 4;    let bvy    = 3;

fn clamp(v, lo, hi) {
    if (v < lo) ret lo;
    if (v > hi) ret hi;
    ret v;
}

let running = true;
while (running) {
    let e = sdl_poll();
    if (e == "quit") { running = false; }

    if (sdl_key_down("up"))   { p1y = p1y - 5; }
    if (sdl_key_down("down")) { p1y = p1y + 5; }
    p1y = clamp(p1y, 0, H - PAD_H);

    // cpu ai
    let mid = p2y + PAD_H / 2;
    if (mid < by) { p2y = p2y + 3; }
    if (mid > by) { p2y = p2y - 3; }
    p2y = clamp(p2y, 0, H - PAD_H);

    bx = bx + bvx;
    by = by + bvy;
    if (by <= 0 || by + 12 >= H) { bvy = -bvy; }
    if (bx < 0 || bx > W) { bx = W / 2; by = H / 2; }

    sdl_clear(15, 15, 25);
    sdl_fill_rect(p1x, p1y, PAD_W, PAD_H, 80,  200, 255, 255);
    sdl_fill_rect(p2x, p2y, PAD_W, PAD_H, 255, 100, 100, 255);
    sdl_fill_rect(bx,  by,  12,    12,    255, 255, 255, 255);
    sdl_present();
    sdl_delay(16);
}

sdl_quit();
```

---

## Tests

The array suite lives in `tests/`:

- `arrays_ok.lunar` — every valid-path case; each `print` carries an expected-output marker
- `arrays_test.sh` — diffs that output against the markers and runs the error cases (bounds, types, syntax, literal size limit), each in its own process

```bash
./tests/arrays_test.sh ./lunar
```

For GC stress testing, build with `-DLUNAR_DEBUG_STRESS_GC` (a collection runs on every allocation; slow, but it catches missing GC roots).

---

## Known Limitations

- No `break` / `continue`, no `for x in array`, and no hash maps
- Strings support only `+`, `len`, and `str`: no indexing, slicing, or searching
- No file or stdin I/O and no module system
- A runtime error aborts the script; there is no error handling
- Each function (and the top-level script) can reference at most 256 distinct constants
- `print` on an array that contains itself recurses without end
- Arrays can't be passed to FFI functions yet

---

## Architecture

```
source (.lunar)
       │
       ▼
   Scanner         tokenizes into a flat token stream
       │
       ▼
   Compiler        Pratt parser → emits bytecode + constant pool
       │
       ▼
   Bytecode         array of u8 opcodes, one Chunk per function
       │
       ▼
   VM               stack-based dispatch loop, call frames, upvalues
       │
       ▼
   GC               tri-color mark-and-sweep, triggered by allocation
```
## License

MIT

## Benchmarks

<!-- BENCH:START -->
| Benchmark | Lunar | Lua | Python |
|---|---|---|---|
| fib(32) | 0.235 s ± 0.003 | 0.132 s ± 0.004 | 0.263 s ± 0.001 |
| loop (10M) | 0.362 s ± 0.003 | 0.129 s ± 0.000 | 0.942 s ± 0.113 |
| array (1M) | 0.095 s ± 0.000 | 0.033 s ± 0.001 | 0.155 s ± 0.008 |
| method calls (1M) | 0.065 s ± 0.001 | 0.036 s ± 0.001 | 0.087 s ± 0.003 |
| closures (1M) | 0.052 s ± 0.001 | 0.026 s ± 0.000 | 0.097 s ± 0.003 |

Measured on Apple M1 (Darwin arm64), mean of 5 runs ± stddev, wall-clock time including process startup. Lower is better.

Versions: Lunar (local build), Lua 5.4.8  Copyright (C) 1994-2025 Lua.org, PUC-Rio, Python 3.14.7.

Reproduce: `python3 bench.py`
<!-- BENCH:END -->
