#!/bin/sh
# Runs the windowless game logic tests. From the repo root:
#     sh examples/minecraft/run_tests.sh [path to lunar]
# There is no module system, so this puts the tests inside game() in place of its main() call.
LUNAR=${1:-./lunar}
OUT=$(mktemp)
sed '/^    main();$/,$d' examples/minecraft/minecraft.lunar > "$OUT"
cat examples/minecraft/game_tests.lunar >> "$OUT"
printf '}\ngame();\n' >> "$OUT"
"$LUNAR" "$OUT"
STATUS=$?
rm -f "$OUT"
exit $STATUS
