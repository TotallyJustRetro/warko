#!/bin/sh
# usage: ./build.sh path/to/rom.gb[c] [name] [trace-frames]
# 1) builds a headless interpreter, 2) traces a scripted play session, 3) recompiles using the
# trace as extra seeds, 4) builds the final SDL2 binary at build/<name>/<name>.
set -e
ROM="$1"; NAME="${2:-$(basename "${ROM%.*}" | tr -c 'A-Za-z0-9_\n-' _)}"; FR="${3:-6000}"
HERE="$(cd "$(dirname "$0")" && pwd)"; B="$HERE/build"; mkdir -p "$B"
python3 "$HERE/gbrecomp.py" "$ROM" -o "$B/${NAME}_trace" --interp-only --name "${NAME}_trace"
make -C "$B/${NAME}_trace" HEADLESS=1 -j"$(nproc)" >/dev/null
"$B/${NAME}_trace/${NAME}_trace" "$ROM" --trace "$B/$NAME.trace" --headless "$FR" --mash
python3 "$HERE/gbrecomp.py" "$ROM" -o "$B/$NAME" --trace "$B/$NAME.trace" --name "$NAME"
make -C "$B/$NAME" -j"$(nproc)" ${HEADLESS:+HEADLESS=1}
echo "done: $B/$NAME/$NAME  (run: $B/$NAME/$NAME \"$ROM\")"
