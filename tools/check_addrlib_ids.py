"""
Inspect F4SE Address Library (.bin) databases.

Two modes:

1. Matrix (default): for a set of REL::IDs, report each ID's image-relative
   offset across every runtime database. Used to see which IDs exist on
   which runtime.

       python check_addrlib_ids.py

2. Reverse lookup: given an image-relative offset (RVA) found in Ghidra for
   ONE runtime, report which REL::ID(s) map to it. Used to convert a
   Ghidra-discovered OG/NG offset into the REL::ID we need for VariantID.

       python check_addrlib_ids.py --reverse og 0xA0AA70
       python check_addrlib_ids.py --reverse 1-10-163 0xa0aa70 0x32694e8 ...

   The runtime token may be a short alias (og / ng / ae) or any substring
   that uniquely matches one of the known bin filenames (e.g. "1-10-163").

F4SE Address Library format (V0, as loaded by CommonLibF4 IDDB.cpp::load_v0):
    uint64 count
    repeat count times:
        uint64 id
        uint64 offset (image-relative)
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path


def parse_addrlib(path: Path) -> dict[int, int]:
    """Return {id: image_relative_offset} from an F4SE V0 Address Library bin."""
    data = path.read_bytes()
    (count,) = struct.unpack_from("<Q", data, 0)
    expected_size = 8 + count * 16
    if len(data) != expected_size:
        raise ValueError(
            f"{path.name}: size mismatch — header says {count} entries "
            f"(expected {expected_size} bytes), file is {len(data)} bytes"
        )

    out: dict[int, int] = {}
    pos = 8
    for _ in range(count):
        rel_id, offset = struct.unpack_from("<QQ", data, pos)
        out[rel_id] = offset
        pos += 16
    return out


IDS_TO_CHECK: dict[int, str] = {
    2222440: "HUDMessage: ShowHUDMessage",
    2222474: "HUDMessage: ShowHUDNotification",
    1395125: "ExamineMenu: CookingMenu vtable",
    1057885: "ExamineMenu: RobotModMenu vtable",
    4801904: "HUDMenu: HUDDataModel singleton",
    2221655: "HUDMenu: QuickContainerUp",
    2221656: "HUDMenu: QuickContainerDown",
}

BIN_DIR = Path(
    r"E:\Games\ModOrganizer\Fallout 4\mods\Address Library for F4SE Plugins\F4SE\Plugins"
)
TARGETS = ["version-1-10-163-0.bin", "version-1-10-984-0.bin", "version-1-11-191-0.bin"]

ALIASES = {
    "og": "version-1-10-163-0.bin",
    "ng": "version-1-10-984-0.bin",
    "ae": "version-1-11-191-0.bin",
}


def resolve_bin(token: str) -> Path:
    """Map an alias/substring to a concrete bin path."""
    if token in ALIASES:
        return BIN_DIR / ALIASES[token]
    matches = [t for t in TARGETS if token in t]
    if len(matches) == 1:
        return BIN_DIR / matches[0]
    if not matches:
        raise SystemExit(f"No runtime bin matches '{token}'. Known: {', '.join(ALIASES)} or a filename substring.")
    raise SystemExit(f"Ambiguous runtime '{token}' matches: {matches}")


def reverse_lookup(bin_path: Path, offsets: list[int]) -> int:
    """For each given image-relative offset, print the REL::ID(s) that map to it."""
    table = parse_addrlib(bin_path)
    # Build reverse map once (offset -> [ids]); offsets are usually unique.
    rev: dict[int, list[int]] = {}
    for rid, off in table.items():
        rev.setdefault(off, []).append(rid)

    print(f"Reverse lookup in {bin_path.name} ({len(table)} entries)\n")
    print(f"{'Offset (RVA)':>14}  {'REL::ID(s)':<24}  Note")
    print("-" * 60)
    rc = 0
    for off in offsets:
        ids = rev.get(off)
        if ids:
            id_str = ", ".join(str(i) for i in sorted(ids))
            note = "" if len(ids) == 1 else "MULTIPLE ids share this offset"
            print(f"0x{off:>12X}  {id_str:<24}  {note}")
        else:
            # No exact match — show nearest entries below/above to aid manual alignment.
            below = max((o for o in rev if o < off), default=None)
            above = min((o for o in rev if o > off), default=None)
            hint_parts = []
            if below is not None:
                hint_parts.append(f"nearest below 0x{below:X} (id {rev[below][0]})")
            if above is not None:
                hint_parts.append(f"nearest above 0x{above:X} (id {rev[above][0]})")
            print(f"0x{off:>12X}  {'(no exact id)':<24}  {'; '.join(hint_parts)}")
            rc = 1
    return rc


def parse_offset(s: str) -> int:
    return int(s, 16) if s.lower().startswith("0x") else int(s, 0)


def run_matrix() -> int:
    tables: dict[str, dict[int, int]] = {}
    for name in TARGETS:
        path = BIN_DIR / name
        if not path.exists():
            print(f"MISSING: {path}")
            continue
        try:
            tables[name] = parse_addrlib(path)
            print(f"Loaded {name}: {len(tables[name])} entries")
        except Exception as e:
            print(f"FAILED {name}: {e}")

    print()
    header = f"{'ID':>10}  {'Symbol':<40}"
    for name in TARGETS:
        header += f"  {name.replace('version-', '').replace('-0.bin', ''):>15}"
    print(header)
    print("-" * len(header))

    for rel_id, label in IDS_TO_CHECK.items():
        row = f"{rel_id:>10}  {label:<40}"
        for name in TARGETS:
            t = tables.get(name)
            if t is None:
                row += f"  {'(no db)':>15}"
            elif rel_id in t:
                row += f"  0x{t[rel_id]:>13X}"
            else:
                row += f"  {'MISSING':>15}"
        print(row)

    return 0


def main(argv: list[str]) -> int:
    if argv and argv[0] in ("--reverse", "-r"):
        if len(argv) < 3:
            raise SystemExit("usage: check_addrlib_ids.py --reverse <runtime> <offset> [offset ...]")
        bin_path = resolve_bin(argv[1])
        offsets = [parse_offset(a) for a in argv[2:]]
        return reverse_lookup(bin_path, offsets)
    return run_matrix()


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
