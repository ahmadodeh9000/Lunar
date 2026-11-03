#!/usr/bin/env python3
"""
Writes the M2 limit tests next to this file. They are committed, so this only
needs to run again if the tests change:
    python3 tests/limits/gen.py
    UPDATE=1 sh tests/run.sh     (then check the diff of the .expected files)
"""

import os

HERE = os.path.dirname(os.path.abspath(__file__))


def nums(start, count):
    return ", ".join(str(n) for n in range(start, start + count))


def many_constants():
    out = ["// Over 256 constants in one function. Every opcode with a constant operand",
           "// is used after the 256th constant, so each one runs in its OP_WIDE form.",
           "",
           "// 300 globals: names and values give 600 constants"]
    out += [f"let g{i} = {i};" for i in range(300)]
    out.append("let gsum = " + " + ".join(f"g{i}" for i in range(300)) + ";")
    out.append("print gsum;                      // wide OP_GET_GLOBAL")
    out.append("g299 = g299 + 1;                 // wide OP_SET_GLOBAL")
    out.append("print g299;")
    out.append("")
    out.append("// 1500 more distinct numbers, wide OP_CONST")
    out.append("let nsum = 0;")
    for start in range(10000, 11500, 50):
        out.append("nsum = nsum + " + " + ".join(str(n) for n in range(start, start + 50)) + ";")
    out.append(f"print nsum;                      // {sum(range(10000, 11500))}")
    out.append("print 0.25;")
    out.append('print "a late string";')
    out.append("")
    out.append("// wide OP_STRUCT, OP_METHOD, OP_CLOSURE, OP_INVOKE, OP_GET/SET_PROPERTY")
    out.append("struct Late {")
    out.append("    init() { self.base = 7; }")
    out.append("    late_method(x) { ret self.base + x; }")
    out.append("    speak() { ret \"late speaks\"; }")
    out.append("}")
    out.append("let obj = Late();")
    out.append("print obj.late_method(3);")
    out.append("let bound = obj.late_method;")
    out.append("print bound(4);")
    out += [f"obj.p{i} = {i};" for i in range(300)]
    out.append("print obj.p0 + obj.p150 + obj.p299;")
    out.append("")
    out.append("fn late_fn(a) { ret a * 2; }")
    out.append("print late_fn(21);")
    out.append("")
    out.append("// a subclass whose method has its own 300+ constants before using super,")
    out.append("// so OP_GET_SUPER and OP_SUPER_INVOKE are wide too")
    out.append("struct Later < Late {")
    out.append("    speak() {")
    out.append(f"        let pad = [{nums(50000, 300)}];")
    out.append("        let got = super.speak;")
    out.append("        ret super.speak() + \" / \" + got() + \" / \" + str(len(pad));")
    out.append("    }")
    out.append("}")
    out.append("print Later().speak();")
    out.append("")
    out.append("// inside a function: its own constant pool, and a closure with upvalues")
    out.append("fn big() {")
    out.append(f"    let pad = [{nums(70000, 300)}];")
    out.append("    let local = 5;")
    out.append("    fn inner(x) { ret local + x + g0; }   // wide OP_CLOSURE with upvalue bytes")
    out.append("    print inner(1);")
    out.append("    print g1 + g298;                   // wide OP_GET_GLOBAL inside a function")
    out.append("    print pad[299];")
    out.append("    let s = Late();")
    out.append("    s.q = 9;                           // wide OP_SET_PROPERTY")
    out.append("    print s.q + s.late_method(1);      // wide OP_GET_PROPERTY and OP_INVOKE")
    out.append("    ret inner;")
    out.append("}")
    out.append("print big()(10);")
    out.append("")
    out.append("// a global defined after the 256th constant, wide OP_DEFINE_GLOBAL")
    out.append("let defined_late = \"defined late\";")
    out.append("print defined_late;")
    return "\n".join(out) + "\n"


def big_array_literal():
    out = ["// Array literals past 255 elements are built in batches of 255.",
           "fn check(name, a, want_len) {",
           "    let sum = 0;",
           "    for (let i = 0; i < len(a); i = i + 1) sum = sum + a[i];",
           "    print name + \": len \" + str(len(a)) + \", first \" + str(a[0]) + \", last \" + str(a[len(a) - 1]) + \", sum \" + str(sum);",
           "    if (len(a) != want_len) print \"FAIL \" + name;",
           "}"]
    for n in (254, 255, 256, 509, 510, 511, 765, 1000):
        out.append(f"check(\"{n}\", [{nums(0, n)}], {n});")
    out.append(f"check(\"256 with trailing comma\", [{nums(0, 256)},], 256);")
    out.append("")
    out.append("// order across batch boundaries")
    out.append(f"let a = [{nums(0, 600)}];")
    out.append("print [a[253], a[254], a[255], a[256], a[509], a[510], a[511], a[599]];")
    out.append("")
    out.append("// 20000 elements: without batches they would overflow the VM stack")
    big = ", ".join(str(i % 1000) for i in range(20000))
    out.append(f"check(\"20000\", [{big}], 20000);")
    out.append("")
    out.append("// mixed and nested elements, as an argument, and in a struct field")
    mixed = ", ".join(f'"s{i}"' if i % 3 == 0 else (f"[{i}]" if i % 3 == 1 else str(i)) for i in range(300))
    out.append(f"let m = [{mixed}];")
    out.append("print [m[0], m[1], m[2], m[255], m[256], m[299]];")
    out.append(f"let nested = [[{nums(0, 300)}], [{nums(1000, 300)}]];")
    out.append("print len(nested[0]) + len(nested[1]);")
    out.append("print nested[1][299];")
    out.append(f"print len([{nums(0, 400)}]);")
    out.append("struct Holder { init() { self.items = nil; } }")
    out.append("let h = Holder();")
    out.append(f"h.items = [{nums(0, 300)}];")
    out.append("print len(h.items);")
    out.append("push(h.items, \"pushed\");")
    out.append("print h.items[300];")
    return "\n".join(out) + "\n"


def write(name, text):
    with open(os.path.join(HERE, name), "w") as f:
        f.write(text)


write("many_constants.lunar", many_constants())
write("big_array_literal.lunar", big_array_literal())
