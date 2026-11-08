#!/usr/bin/env python3
"""Rebuild compile_commands.json from UBT .cpp.o.rsp files (+ optional PA250Sim CMake CDB).

clangd is unreliable with @*.rsp when paths contain spaces, so arguments are expanded.
"""
from __future__ import annotations

import json
import shlex
from pathlib import Path

ENGINE_SOURCE = Path("/Users/Shared/Epic Games/UE_5.8/Engine/Source")
CLANGXX = (
    "/Applications/Xcode.app/Contents/Developer/Toolchains/"
    "XcodeDefault.xctoolchain/usr/bin/clang++"
)
PROJECT_ROOT = Path(__file__).resolve().parents[1]
INTERMEDIATE = PROJECT_ROOT / "Intermediate/Build/Mac/arm64/UnrealEditor/Development"
OUT = PROJECT_ROOT / "compile_commands.json"
PA250SIM_CDB = PROJECT_ROOT.parent / "PA250Sim/build/compile_commands.json"


def tokenize_rsp(text: str) -> list[str]:
    """Parse an rsp: one flag per line, quotes around values with spaces."""
    args: list[str] = []
    for raw in text.splitlines():
        line = raw.strip()
        if not line:
            continue
        # UBT sometimes packs "-MD -MF\"path\"" on one line.
        parts = shlex.split(line, posix=True)
        args.extend(parts)
    return args


def entry_from_rsp(rsp: Path) -> dict | None:
    args = tokenize_rsp(rsp.read_text(encoding="utf-8", errors="replace"))
    if not args:
        return None

    # Drop PCH binary — clangd often chokes on .gch from a different clang.
    cleaned: list[str] = []
    skip_next = False
    for a in args:
        if skip_next:
            skip_next = False
            continue
        if a == "-include-pch":
            skip_next = True
            continue
        cleaned.append(a)

    src = None
    output = None
    i = 0
    while i < len(cleaned):
        a = cleaned[i]
        if a == "-o" and i + 1 < len(cleaned):
            output = cleaned[i + 1]
            i += 2
            continue
        if a.endswith(".cpp") and a.startswith("/"):
            src = a
        i += 1
    if not src:
        return None

    return {
        "directory": str(ENGINE_SOURCE),
        "file": src,
        "output": output or str(rsp).removesuffix(".rsp"),
        "arguments": [CLANGXX, *cleaned],
    }


def main() -> None:
    entries: list[dict] = []
    seen: set[str] = set()
    for rsp in sorted(INTERMEDIATE.glob("*/*.cpp.o.rsp")):
        e = entry_from_rsp(rsp)
        if not e or e["file"] in seen:
            continue
        seen.add(e["file"])
        entries.append(e)

    if PA250SIM_CDB.is_file():
        for e in json.loads(PA250SIM_CDB.read_text(encoding="utf-8")):
            f = e.get("file")
            if f and f not in seen:
                seen.add(f)
                entries.append(e)

    OUT.write_text(json.dumps(entries, indent=2) + "\n", encoding="utf-8")
    print(f"Wrote {len(entries)} entries -> {OUT}")


if __name__ == "__main__":
    main()
