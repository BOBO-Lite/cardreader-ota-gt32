#!/usr/bin/env python3
"""Prove Slice 8 AppOta sources have no Modbus FC/register implementation.

Documentary comments mentioning that Modbus is out of scope are allowed;
executable code / non-comment text must not contain Modbus/FC/register mapping.
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]

FILES = [
    "include/app_ota.h",
    "src/app_ota.c",
    "tests/test_app_ota.c",
]


def strip_c_noise(text: str) -> str:
    """Remove block/line comments and string/char literals."""
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


# After stripping comments: forbid Modbus symbols / FC / register-map APIs
PAT = re.compile(
    r"(?i)Modbus|MODBUS|\bFC\s*/|\bFC\d+\b|holding.?reg|coil.?reg|"
    r"function.?code|寄存器"
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
    print("FAIL: Modbus/FC/register symbols in AppOta sources (code, not comments):")
    print("\n".join(bad))
    sys.exit(1)
print("PASS: AppOta sources have no Modbus FC/register implementation symbols")
