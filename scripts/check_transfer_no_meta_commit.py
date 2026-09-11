#!/usr/bin/env python3
"""Fail if transfer path references OtaMeta_Commit* (Meta only at SESSION_DONE)."""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
pat = re.compile(r"OtaMeta_Commit")
bad = []

for rel in [
    "src/drv_ymodem.c",
    "include/drv_ymodem.h",
    "src/drv_usart.c",
    "include/drv_usart.h",
]:
    path = ROOT / rel
    for i, line in enumerate(path.read_text().splitlines(), 1):
        if pat.search(line):
            bad.append(f"{rel}:{i}:{line.rstrip()}")

boot = (ROOT / "src/boot.c").read_text()
for name in ("Boot_OtaHandleData", "Boot_OtaEraseSecondary", "Boot_OtaHandleHeader"):
    m = re.search(
        r"(static\s+)?BootOtaStatus_t\s+" + name + r"\s*\([^;]*?\)\s*\{",
        boot,
        re.S,
    )
    if not m:
        continue
    start = m.end() - 1
    depth = 0
    for j in range(start, len(boot)):
        if boot[j] == "{":
            depth += 1
        elif boot[j] == "}":
            depth -= 1
            if depth == 0:
                body = boot[start : j + 1]
                for i, line in enumerate(body.splitlines(), 1):
                    if pat.search(line):
                        bad.append(f"src/boot.c/{name}:{i}:{line.rstrip()}")
                break

if bad:
    print("FAIL: OtaMeta_Commit in transfer path:")
    print("\n".join(bad))
    sys.exit(1)
print("PASS: transfer path does not call OtaMeta_Commit (Meta only at SESSION_DONE)")
