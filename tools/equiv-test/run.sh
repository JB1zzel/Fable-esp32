#!/bin/sh
# Host-side regression test for the editor (no ESP32 needed).
#
# Replays pseudo-random key presses through fable_main() against a fake
# screen and a fake SD folder, and prints a hash of everything drawn plus
# the files written. Run it on two versions of the source and compare:
#
#   ./run.sh /path/to/old/Fable-esp32  > old.txt
#   ./run.sh /path/to/new/Fable-esp32  > new.txt
#   diff old.txt new.txt && echo same behaviour
#
# Needs gcc (with AddressSanitizer) on Linux/WSL/macOS.
TREE=${1:-../..}
SEEDS=${SEEDS:-20}
KEYS=${KEYS:-4000}
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$TREE" && pwd)
W=$(mktemp -d)
SRCS=$(ls "$TREE"/src/*.c | grep -v tinyspell.c)

gcc -g -fsanitize=address,undefined -fno-sanitize-recover=undefined -w \
    -DFABLE_ESP32 -I"$TREE/src" -I"$HERE/stub" \
    -o "$W/fable_test" "$HERE/harness.c" $SRCS || exit 1

i=1
while [ $i -le "$SEEDS" ]; do
    rm -rf "$W/fs"; mkdir -p "$W/fs/diary"; echo "sample line one" > "$W/fs/readme.txt"
    out=$("$W/fable_test" $i $KEYS "$W/fs" 2>&1 | tail -1)
    files=$(cd "$W/fs" && find . -type f | sort | while read f; do printf '%s:' "$f"; cksum < "$f" | cut -d' ' -f1,2; done | tr '\n' ' ')
    echo "seed $i: $out | $files"
    i=$((i+1))
done
rm -rf "$W"
