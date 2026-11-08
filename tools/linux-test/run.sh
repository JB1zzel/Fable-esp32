#!/bin/sh
# Builds Fable for Linux and drives the real program in a pseudo-terminal:
# types text, saves, opens files, uses the file browser, diary, games menu and
# checks the screen each time. Needs gcc and python3.
#
#   tools/linux-test/run.sh
HERE=$(cd "$(dirname "$0")" && pwd)
TREE=$(cd "$HERE/../.." && pwd)
FABLE_TEST_DIR=${FABLE_TEST_DIR:-/tmp/fable-linux-test}
export FABLE_TEST_DIR
mkdir -p "$FABLE_TEST_DIR/bin" "$FABLE_TEST_DIR/work"
gcc -O1 -o "$FABLE_TEST_DIR/bin/fable" "$TREE"/src/*.c || exit 1
cp "$TREE/src/dict.smd" "$FABLE_TEST_DIR/bin/"
python3 "$HERE/tests.py"
