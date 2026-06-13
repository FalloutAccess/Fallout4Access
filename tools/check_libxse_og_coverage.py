"""
Measure how many of CommonLibF4 (libxse) baked-in REL::IDs actually resolve on
the OG (1.10.163) Address Library vs AE (1.11.191).

This tells us whether a single libxse-compiled DLL can work on OG at all, or
whether OG needs a separately-compiled (OG-ID) build.

    python check_libxse_og_coverage.py
"""

from __future__ import annotations

import re
import struct
from pathlib import Path

IDS_H = Path(r"E:\Games\modding\Fallout4\fallout4access\lib\commonlibf4\include\RE\IDs.h")
BIN_DIR = Path(
    r"E:\Games\ModOrganizer\Fallout 4\mods\Address Library for F4SE Plugins\F4SE\Plugins"
)
OG = BIN_DIR / "version-1-10-163-0.bin"
AE = BIN_DIR / "version-1-11-191-0.bin"


def parse_addrlib(path: Path) -> dict[int, int]:
    data = path.read_bytes()
    (count,) = struct.unpack_from("<Q", data, 0)
    out: dict[int, int] = {}
    pos = 8
    for _ in range(count):
        rid, off = struct.unpack_from("<QQ", data, pos)
        out[rid] = off
        pos += 16
    return out


def main() -> int:
    text = IDS_H.read_text(encoding="utf-8", errors="replace")
    # Match `REL::ID Name{ 1234567 }` and `REL::ID Name{1234567}`.
    ids = [int(m) for m in re.findall(r"REL::ID\s+\w+\s*\{\s*(\d+)\s*\}", text)]
    ids = sorted(set(ids))
    print(f"Extracted {len(ids)} distinct REL::IDs from IDs.h\n")

    og = parse_addrlib(OG)
    ae = parse_addrlib(AE)
    print(f"OG db: {len(og)} entries (max id {max(og)})")
    print(f"AE db: {len(ae)} entries (max id {max(ae)})\n")

    in_og = sum(1 for i in ids if i in og)
    in_ae = sum(1 for i in ids if i in ae)
    print(f"libxse IDs present in AE: {in_ae}/{len(ids)} ({in_ae/len(ids):.1%})")
    print(f"libxse IDs present in OG: {in_og}/{len(ids)} ({in_og/len(ids):.1%})")

    # Break down by ID magnitude band to show the pattern.
    bands = [(0, 1_582_975, "<=OG max (1.58M)"), (1_582_976, 10**9, "> OG max")]
    print("\nBy ID band (present in OG):")
    for lo, hi, label in bands:
        band = [i for i in ids if lo <= i <= hi]
        if not band:
            continue
        hit = sum(1 for i in band if i in og)
        print(f"  {label:<20} {hit}/{len(band)} present in OG ({hit/len(band):.1%})")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
