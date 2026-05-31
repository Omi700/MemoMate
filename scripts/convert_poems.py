#!/usr/bin/env python3
import json
import re
import sys
from pathlib import Path


POEM_RE = re.compile(r"^\*\*《(?P<title>.+?)》\*\*\s*[（(](?P<meta>.+?)[）)]\s*(?P<body>.+?)\s*$")
SENTENCE_SPLIT_RE = re.compile(r"(?<=[。！？?])\s*")


def clean_text(text: str) -> str:
    text = text.strip()
    text = text.replace("（", "(").replace("）", ")")
    text = re.sub(r"\s+", " ", text)
    return text


def parse_meta(meta: str):
    meta = meta.strip()
    if "·" in meta:
        dynasty, author = meta.split("·", 1)
        return dynasty.strip(), author.strip(), ""

    source_match = re.fullmatch(r"《(.+?)》", meta)
    if source_match:
        return "", "佚名", source_match.group(1).strip()

    return "", meta, ""


def split_lines(body: str):
    body = clean_text(body)
    lines = []
    for part in SENTENCE_SPLIT_RE.split(body):
        part = part.strip()
        if not part:
            continue
        lines.append(part)
    return lines


def make_id(index: int, title: str):
    ascii_slug = re.sub(r"[^a-zA-Z0-9]+", "-", title).strip("-").lower()
    if ascii_slug:
        return f"poem_{index:03d}_{ascii_slug}"
    return f"poem_{index:03d}"


def convert(input_path: Path):
    poems = []
    seen = set()

    for raw_line in input_path.read_text(encoding="utf-8").splitlines():
        raw_line = raw_line.strip()
        if not raw_line:
            continue

        match = POEM_RE.match(raw_line)
        if not match:
            raise ValueError(f"无法解析这一行：{raw_line}")

        title = clean_text(match.group("title"))
        dynasty, author, source = parse_meta(match.group("meta"))
        lines = split_lines(match.group("body"))
        full_text = "".join(lines)

        key = (title, dynasty, author, full_text)
        if key in seen:
            continue
        seen.add(key)

        poem = {
            "id": make_id(len(poems) + 1, title),
            "title": title,
            "dynasty": dynasty,
            "author": author,
            "source": source,
            "lines": lines,
            "full_text": full_text,
        }
        poems.append(poem)

    return poems


def main():
    if len(sys.argv) != 3:
        print("Usage: convert_poems.py <input.md> <output.json>", file=sys.stderr)
        return 2

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2])
    poems = convert(input_path)

    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(
        json.dumps(poems, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    print(f"Converted {len(poems)} poems -> {output_path}")


if __name__ == "__main__":
    raise SystemExit(main())
