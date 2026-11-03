#!/bin/sh
# Runs every tests/**/*.lunar and compares it with the .expected file next to it.
# From the repo root:
#     sh tests/run.sh [path to lunar]            run the tests
#     UPDATE=1 sh tests/run.sh [path to lunar]   rewrite the .expected files (check the diff!)
#
# An .expected file holds the script's stdout, then its stderr, then its exit code:
#     <stdout>
#     -- stderr --
#     <stderr>
#     -- exit 0 --
# Scripts run from the repo root. Sanitizer reports land in stderr, so they fail the test.

LUNAR=${1:-./lunar}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
export UBSAN_OPTIONS

pass=0
fail=0
failed=""

for test in $(find tests -name '*.lunar' | sort); do
    expected="${test%.lunar}.expected"
    "$LUNAR" "$test" > "$TMP/out" 2> "$TMP/err"
    status=$?
    {
        cat "$TMP/out"
        echo "-- stderr --"
        cat "$TMP/err"
        echo "-- exit $status --"
    } > "$TMP/actual"

    if [ -n "$UPDATE" ]; then
        cp "$TMP/actual" "$expected"
        echo "updated $expected"
        continue
    fi

    if [ ! -f "$expected" ]; then
        echo "FAIL $test (no $expected; create it with UPDATE=1)"
        fail=$((fail + 1)); failed="$failed $test"
    elif diff -u "$expected" "$TMP/actual" > "$TMP/diff"; then
        pass=$((pass + 1))
    else
        echo "FAIL $test"
        sed 's/^/    /' "$TMP/diff"
        fail=$((fail + 1)); failed="$failed $test"
    fi
done

[ -n "$UPDATE" ] && exit 0

# the minecraft game logic tests print FAIL lines and end with "game tests: done"
sh examples/minecraft/run_tests.sh "$LUNAR" > "$TMP/mc" 2>&1
status=$?
if [ $status -eq 0 ] && ! grep -q FAIL "$TMP/mc" && [ "$(tail -n 1 "$TMP/mc")" = "game tests: done" ]; then
    pass=$((pass + 1))
else
    echo "FAIL examples/minecraft/run_tests.sh (exit $status)"
    sed 's/^/    /' "$TMP/mc" | tail -n 20
    fail=$((fail + 1)); failed="$failed minecraft"
fi

echo "$LUNAR: $pass passed, $fail failed"
[ $fail -eq 0 ] || { echo "failed:$failed"; exit 1; }
