#!/bin/sh
# Runs the windowless game logic tests. From the repo root:
#     sh examples/minecraft/run_tests.sh [path to lunar]
# There is no module system, so this joins the game (minus its main() call) with the tests.
LUNAR=${1:-./lunar}
OUT=$(mktemp)
sed '$d' examples/minecraft/minecraft.lunar > "$OUT"
cat examples/minecraft/game_tests.lunar >> "$OUT"
"$LUNAR" "$OUT"
STATUS=$?
rm -f "$OUT"
exit $STATUS
