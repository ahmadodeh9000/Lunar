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
- **String interning** — strings deduplicated via FNV-1a hash table, pointer equality comparison
- **Bitwise operators** — `&` `|` `^` `~` `<<` `>>`
- **SDL2 support** — optional build target for 2D graphics, images, text, keyboard and mouse

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

Install SDL2 first:

```bash
# Ubuntu / Debian
sudo apt install libsdl2-dev libsdl2-image-dev libsdl2-ttf-dev

# Arch
sudo pacman -S sdl2 sdl2_image sdl2_ttf

# macOS
brew install sdl2 sdl2_image sdl2_ttf
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

| Type     | Example          | Notes                       |
|----------|------------------|-----------------------------|
| Number   | `3.14`, `42`     | 64-bit IEEE 754 double      |
| Bool     | `true`, `false`  |                             |
| Nil      | `nil`            | Absence of a value          |
| String   | `"hello"`        | UTF-8, multi-line supported |
| Function | `fn f() { ... }` | First-class closure         |
| Instance | `Point(x, y)`    | Instance of a struct        |

`nil` and `false` are falsey. Everything else is truthy.

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

| Function   | Signature           | Description                    |
|------------|---------------------|--------------------------------|
| `print`    | `print x`           | Print value with newline       |
| `clock()`  | `() → number`       | Seconds since program start    |
| `sqrt(x)`  | `(number) → number` | Square root                    |
| `abs(x)`   | `(number) → number` | Absolute value                 |
| `floor(x)` | `(number) → number` | Round toward negative infinity |
| `ceil(x)`  | `(number) → number` | Round toward positive infinity |
| `str(x)`   | `(any) → string`    | Convert any value to string    |
| `len(s)`   | `(string) → number` | String length in bytes         |

---

## SDL2 API

Only available when built with `make sdl`.

### Lifecycle

```lunar
sdl_init("Title", 800, 600);   // open window + renderer
sdl_quit();                     // destroy window, free resources
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
sdl_clear(r, g, b)                          // clear screen with color
sdl_fill_rect(x, y, w, h, r, g, b, a)      // filled rectangle
sdl_draw_rect(x, y, w, h, r, g, b, a)      // outlined rectangle
sdl_draw_line(x1, y1, x2, y2, r, g, b, a)  // line
sdl_draw_point(x, y, r, g, b, a)           // single pixel
sdl_present()                               // flip buffer to screen
```

### Images

```lunar
let img = sdl_load_image("player.png");
sdl_draw_image(img, x, y, w, h);
sdl_draw_image_ex(img, x, y, w, h, angle, flip);
sdl_free_image(img);
```

### Text

```lunar
let font = sdl_load_font("font.ttf", 24);
sdl_draw_text(font, "Score: " + str(score), x, y, r, g, b);
sdl_free_font(font);
```

### Input

```lunar
let e = sdl_poll();      // "quit" | "keydown" | "keyup"
                         // "mousemove" | "mousedown" | "mouseup" | nil

sdl_key_down("left")     // true if key currently held
                         // keys: "up" "down" "left" "right"
                         //       "space" "escape" "w" "a" "s" "d"

sdl_mouse_x()            // current cursor x
sdl_mouse_y()            // current cursor y
sdl_mouse_down(1)        // true if button held (1=left 2=middle 3=right)
```

### Timing

```lunar
sdl_ticks()    // milliseconds since sdl_init
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

### Source files

| File              | Purpose                                             |
|-------------------|-----------------------------------------------------|
| `src/scanner.c`   | Lexer — keywords, literals, operators               |
| `src/compiler.c`  | Pratt parser + bytecode emitter                     |
| `src/vm.c`        | Bytecode interpreter, call frames, native registry  |
| `src/chunk.c`     | Bytecode array + constant pool                      |
| `src/object.c`    | Heap objects: strings, functions, closures, structs |
| `src/table.c`     | Hash table — open addressing, FNV-1a hashing        |
| `src/memory.c`    | Allocator + tri-color mark-and-sweep GC             |
| `src/debug.c`     | Bytecode disassembler                               |
| `src/lunar_sdl.c` | SDL2 native bindings (optional)                     |
| `src/main.c`      | Entry point — REPL + file runner                    |

---

## Roadmap

- [ ] Native array type with `push`, `pop`, `len`
- [ ] String escape sequences (`\n`, `\t`, `\\`)
- [ ] `break` and `continue` in loops
- [ ] Variadic functions
- [ ] Error handling (`try` / `catch`)
- [ ] Multiline comments (`/* */`)
- [ ] More SDL2 — image and text rendering
- [ ] More built-in math and string functions

---

## License

MIT