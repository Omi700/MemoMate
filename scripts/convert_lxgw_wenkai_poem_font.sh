#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
FONT_FILE="$ROOT_DIR/firmware/echofridge/assets/fonts/lxgw-wenkai/LXGWWenKai-Regular.ttf"
POEMS_JSON="$ROOT_DIR/papers/poems.json"
BUILD_DIR="$ROOT_DIR/firmware/echofridge/build/generated-fonts"
OUT_DIR="$ROOT_DIR/firmware/echofridge/main/fonts"
BDFCONV="$ROOT_DIR/firmware/echofridge/vendor/u8g2-tools/tools/font/bdfconv/bdfconv"
CHARS_FILE="$BUILD_DIR/poem_chars.txt"

mkdir -p "$BUILD_DIR" "$OUT_DIR"

python3 - "$POEMS_JSON" "$CHARS_FILE" <<'PY'
import json
import sys
from pathlib import Path

poems_path = Path(sys.argv[1])
chars_path = Path(sys.argv[2])
poems = json.loads(poems_path.read_text(encoding="utf-8"))

chars = set("0123456789: -.·—《》、，。！？?！\n ")
for poem in poems:
    for key in ("title", "dynasty", "author", "source", "full_text"):
        chars.update(poem.get(key, "") or "")
    for line in poem.get("lines", []):
        chars.update(line)

chars_path.write_text("".join(sorted(chars)), encoding="utf-8")
print(f"Font subset chars: {len(chars)}")
PY

convert_size() {
    local size="$1"
    local name="u8g2_font_lxgw_wenkai_${size}_poem"
    local bdf="$BUILD_DIR/lxgw_wenkai_${size}_poem.bdf"
    local output="$OUT_DIR/lxgw_wenkai_${size}_poem.c"

    otf2bdf -p "$size" -r 72 -o "$bdf" "$FONT_FILE"
    "$BDFCONV" -f 1 -b 0 -u "$CHARS_FILE" -n "$name" -o "$output" "$bdf"
    python3 - "$output" <<'PY'
import sys
from pathlib import Path

path = Path(sys.argv[1])
text = path.read_text(encoding="utf-8")
if '#include "u8g2.h"' not in text:
    text = text.replace("*/\n", '*/\n#include "u8g2.h"\n\n', 1)
    path.write_text(text, encoding="utf-8")
PY
    ls -lh "$output"
}

convert_size 12
convert_size 14
convert_size 16
convert_size 17
convert_size 18
