#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
FONT_FILE="$ROOT_DIR/assets/fonts/Anton-Regular.ttf"
BDFCONV="$ROOT_DIR/vendor/u8g2-tools/tools/font/bdfconv/bdfconv"
OUT_DIR="$ROOT_DIR/build/generated-fonts"
FONT_OUT_DIR="$ROOT_DIR/main/fonts"

mkdir -p "$OUT_DIR" "$FONT_OUT_DIR"

if ! command -v otf2bdf >/dev/null 2>&1; then
  echo "otf2bdf not found. Install it with: brew install otf2bdf" >&2
  exit 1
fi

if [ ! -x "$BDFCONV" ]; then
  echo "bdfconv not found. Build it in: vendor/u8g2-tools/tools/font/bdfconv" >&2
  exit 1
fi

otf2bdf -r 72 -p 82 -o "$OUT_DIR/anton_82.bdf" "$FONT_FILE"
otf2bdf -r 72 -p 42 -o "$OUT_DIR/anton_42.bdf" "$FONT_FILE"
otf2bdf -r 72 -p 90 -o "$OUT_DIR/anton_90.bdf" "$FONT_FILE"
otf2bdf -r 72 -p 28 -o "$OUT_DIR/anton_28.bdf" "$FONT_FILE"

"$BDFCONV" -b 0 -f 1 -m '48-58' "$OUT_DIR/anton_82.bdf" \
  -n u8g2_font_anton_82_time \
  -o "$FONT_OUT_DIR/anton_82_time.c"

"$BDFCONV" -b 0 -f 1 -m '37,45-57,67' "$OUT_DIR/anton_42.bdf" \
  -n u8g2_font_anton_42_climate \
  -o "$FONT_OUT_DIR/anton_42_climate.c"

"$BDFCONV" -b 0 -f 1 -m '48-58' "$OUT_DIR/anton_90.bdf" \
  -n u8g2_font_anton_90_time \
  -o "$FONT_OUT_DIR/anton_90_time.c"

"$BDFCONV" -b 0 -f 1 -m '32,37,45-57,67' "$OUT_DIR/anton_28.bdf" \
  -n u8g2_font_anton_28_climate \
  -o "$FONT_OUT_DIR/anton_28_climate.c"

python3 - "$FONT_OUT_DIR/anton_82_time.c" "$FONT_OUT_DIR/anton_42_climate.c" <<'PY'
from pathlib import Path
import sys

for name in sys.argv[1:]:
    path = Path(name)
    text = path.read_text()
    if '#include "u8g2.h"' not in text:
        marker = "*/\n"
        text = text.replace(marker, marker + '#include "u8g2.h"\n\n', 1)
        path.write_text(text)
PY

python3 - "$FONT_OUT_DIR/anton_90_time.c" "$FONT_OUT_DIR/anton_28_climate.c" <<'PY'
from pathlib import Path
import sys

for name in sys.argv[1:]:
    path = Path(name)
    text = path.read_text()
    if '#include "u8g2.h"' not in text:
        marker = "*/\n"
        text = text.replace(marker, marker + '#include "u8g2.h"\n\n', 1)
        path.write_text(text)
PY

echo "Generated Anton U8G2 fonts in $FONT_OUT_DIR"
