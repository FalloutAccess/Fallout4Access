"""Dump likely bone names from Fallout 4 skeleton NIFs.

Brute-force scan: looks for `u32 length + <length> printable ASCII bytes`
patterns in the file. That catches the NiNode name strings (plus some incidental
other names), which is plenty for seeing turret/robot bone vocabulary.
"""

from __future__ import annotations

import string
import struct
import sys
from pathlib import Path

PRINTABLE = set(string.ascii_letters + string.digits + "_-. []()")


def scan_strings(data: bytes, min_len: int = 2, max_len: int = 64) -> list[str]:
    found: list[str] = []
    seen: set[str] = set()
    n = len(data)
    i = 0
    while i + 4 <= n:
        slen = struct.unpack_from("<I", data, i)[0]
        if min_len <= slen <= max_len and i + 4 + slen <= n:
            chunk = data[i + 4:i + 4 + slen]
            if all(chr(b) in PRINTABLE for b in chunk):
                s = chunk.decode("ascii")
                # Skip obvious non-bone junk (paths, doubles-of-doubles, etc.)
                if "\\" not in s and "/" not in s and "." not in s:
                    if s not in seen:
                        seen.add(s)
                        found.append(s)
                    i += 4 + slen
                    continue
        i += 1
    return found


def main() -> None:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).parent / "skeletons"
    nifs = sorted(root.rglob("*.nif"))
    for nif in nifs:
        print(f"\n=== {nif.relative_to(root)} ===")
        names = scan_strings(nif.read_bytes())
        for n in names:
            print(f"  {n}")


if __name__ == "__main__":
    main()
