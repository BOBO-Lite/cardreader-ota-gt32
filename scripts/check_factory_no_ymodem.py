#!/usr/bin/env python3
"""Prove factory_pack sources do not include YMODEM send / drv_ymodem."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
FILES = [
    "include/factory_pack.h",
    "src/factory_pack.c",
    "tools/factory_pack_main.c",
    "tests/test_factory_pack.c",
]


def strip_c_noise(text: str) -> str:
    out = []
    i = 0
    n = len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if text[i] in "\"'":
            q = text[i]
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == q:
                    i += 1
                    break
                i += 1
            continue
        out.append(text[i])
        i += 1
    return "".join(out)


PAT = re.compile(
    r"(?i)drv_ymodem|Ymodem_|YMODEM_|#include\s*[<\"]drv_ymodem"
)

bad = []
for rel in FILES:
    path = ROOT / rel
    if not path.is_file():
        bad.append(f"missing:{rel}")
        continue
    code = strip_c_noise(path.read_text(encoding="utf-8", errors="replace"))
    for lineno, line in enumerate(code.splitlines(), 1):
        if PAT.search(line):
            bad.append(f"{rel}:{lineno}:{line.strip()}")

if bad:
    print("FAIL: YMODEM symbols in factory_pack sources:")
    print("\n".join(bad))
    sys.exit(1)
print("PASS: factory_pack sources have no YMODEM/drv_ymodem implementation")
