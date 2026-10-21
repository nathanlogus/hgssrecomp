#!/bin/bash
# Test helper: level up by walking through grass, healing at the Pokemon Center between rounds.
# usage: grind.sh <snapshot in> <snapshot out> <iterations> <exit waypoints> <grass a> <grass b> <return waypoints>
# The return waypoints must end on the tile below the nurse. Logs go to test/grind.log.
cd "$(dirname "$0")/.."
IN="$1"; OUT="$2"; N="$3"
F=$(./build/johto-online ../pokeheartgold/build/soulsilver.us/pokesoulsilver.us.nds --headless --no-save \
    --state-load "$IN" --frames 2 --pos-every 1 2>&1 | grep -m1 "^pos" | awk '{print $2}')
: > test/grind.log
for i in $(seq "$N"); do
    S=$((F + 200))
    { echo "$((S)) $((S + 20)) UP"
      for k in $(seq 0 34); do echo "$((S + 40 + k * 40)) $((S + 46 + k * 40)) $([ $k -lt 28 ] && echo A || echo B)"; done
    } > test/grind.txt
    W="$4;$5;$6;$5;$6;$5;$6;$5;$6;$5;$6;$7"
    tools/navrun.sh "$IN" test/grind.txt $((S + 1500)) "$W" 150 --log-sections --state-save "$OUT" --state-at $((S + 1490)) >> test/grind.log 2>&1
    IN="$OUT"
    F=$((S + 1490))
    echo "iteration $i done at frame $F" >> test/grind.log
done
