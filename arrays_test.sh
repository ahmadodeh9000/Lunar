#!/bin/bash
# usage: ./arrays_test.sh [path-to-lunar]     (default ./lunar, run from the repo root)
# expects arrays_ok.lunar next to this script
LUNAR=${1:-./lunar}
DIR=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
pass=0; fail=0

# ── 1. valid cases: diff real output against //=> markers ──
grep -o '//=> .*' "$DIR/arrays_ok.lunar" | sed 's|^//=> ||' > "$TMP/expected.txt"
"$LUNAR" "$DIR/arrays_ok.lunar" > "$TMP/actual.txt" 2>&1
if diff -u "$TMP/expected.txt" "$TMP/actual.txt"; then
    echo "OK   arrays_ok.lunar ($(wc -l < "$TMP/expected.txt" | tr -d ' ') lines)"
    pass=$((pass+1))
else
    echo "FAIL arrays_ok.lunar"
    fail=$((fail+1))
fi

# ── 2. error cases: each needs its own run, since a runtime error aborts the script ──
check() {   # check "<expected substring>" "<lunar source>"
    printf '%s\n' "$2" > "$TMP/case.lunar"
    out=$("$LUNAR" "$TMP/case.lunar" 2>&1)
    case "$out" in
        *"$1"*) pass=$((pass+1)) ;;
        *) fail=$((fail+1))
           echo "FAIL: $2"
           echo "   expected substring: $1"
           echo "   got: $out" ;;
    esac
}

# runtime: bounds
check "out of bounds"          'print [1, 2, 3][3];'
check "out of bounds"          'print [1, 2, 3][-1];'
check "out of bounds"          'print [1, 2, 3][1.5];'
check "out of bounds"          'print [][0];'
check "out of bounds"          'print [1][0 / 0];'          # NaN index
check "out of bounds"          'print [1][1 / 0];'          # inf index
check "out of bounds"          'let a = [1]; a[1] = 0;'
check "out of bounds"          'let a = [1]; a[-1] = 0;'

# runtime: types
check "index must be a number" 'print [1, 2, 3]["a"];'
check "index must be a number" 'print [1, 2, 3][nil];'
check "index must be a number" 'print [1, 2, 3][[0]];'
check "index must be a number" 'let a = [1]; a[nil] = 0;'
check "Only arrays"            'print 5[0];'
check "Only arrays"            'print "abc"[0];'
check "Only arrays"            'print nil[0];'
check "Only arrays"            'let n = nil; n[0] = 1;'
check "Only arrays"            'let n = 3; n[0] = 1;'

# runtime error inside a function still reports a trace
check "in f()"                 'fn f() { ret [1][5]; } f();'

# compile errors
check "Expect ']' after array elements." 'print [1, 2;'
check "Expect ']' after array elements." 'print [1 2];'
check "Expect ']' after index."          'let a = [1]; print a[0;'
check "Expected expression."             'let a = [1]; print a[];'
check "Expected expression."             'print [1, , 2];'
check "Invalid assignment target."       '[1, 2] = 3;'

# literal size limit: 255 ok, 256 rejected (nil elements use no constants)
elems() { local s="" i=0; while [ $i -lt "$1" ]; do s="$s${s:+,}nil"; i=$((i+1)); done; echo "$s"; }
check "ok"                               "let a = [$(elems 255)]; print \"ok\";"
check "Can't have more than 255"         "let a = [$(elems 256)];"

rm -rf "$TMP"
echo "passed: $pass  failed: $fail"
[ $fail -eq 0 ]
