#!/usr/bin/env python3
"""Extract Bethesda string files (.STRINGS, .DLSTRINGS, .ILSTRINGS) to readable text."""

import struct
import sys
from pathlib import Path


def read_strings_file(filepath: Path) -> dict[int, str]:
    """Read a .STRINGS file (null-terminated strings)."""
    strings = {}
    with open(filepath, 'rb') as f:
        count, data_size = struct.unpack('<II', f.read(8))

        # Read directory
        directory = []
        for _ in range(count):
            string_id, offset = struct.unpack('<II', f.read(8))
            directory.append((string_id, offset))

        # Read strings
        data_start = f.tell()
        for string_id, offset in directory:
            f.seek(data_start + offset)
            chars = []
            while True:
                c = f.read(1)
                if c == b'\x00' or c == b'':
                    break
                chars.append(c)
            try:
                strings[string_id] = b''.join(chars).decode('utf-8')
            except UnicodeDecodeError:
                strings[string_id] = b''.join(chars).decode('cp1252', errors='replace')

    return strings


def read_dlstrings_file(filepath: Path) -> dict[int, str]:
    """Read a .DLSTRINGS or .ILSTRINGS file (length-prefixed strings)."""
    strings = {}
    with open(filepath, 'rb') as f:
        count, data_size = struct.unpack('<II', f.read(8))

        # Read directory
        directory = []
        for _ in range(count):
            string_id, offset = struct.unpack('<II', f.read(8))
            directory.append((string_id, offset))

        # Read strings
        data_start = f.tell()
        for string_id, offset in directory:
            f.seek(data_start + offset)
            length = struct.unpack('<I', f.read(4))[0]
            data = f.read(length)
            # Remove trailing null if present
            if data and data[-1:] == b'\x00':
                data = data[:-1]
            try:
                strings[string_id] = data.decode('utf-8')
            except UnicodeDecodeError:
                strings[string_id] = data.decode('cp1252', errors='replace')

    return strings


def extract_to_text(filepath: Path, output_path: Path = None):
    """Extract a string file to readable text."""
    ext = filepath.suffix.upper()

    if ext == '.STRINGS':
        strings = read_strings_file(filepath)
    elif ext in ('.DLSTRINGS', '.ILSTRINGS'):
        strings = read_dlstrings_file(filepath)
    else:
        print(f"Unknown file type: {ext}")
        return

    if output_path is None:
        output_path = filepath.with_suffix('.txt')

    with open(output_path, 'w', encoding='utf-8') as f:
        f.write(f"# Extracted from {filepath.name}\n")
        f.write(f"# Total strings: {len(strings)}\n\n")

        for string_id in sorted(strings.keys()):
            text = strings[string_id]
            # Escape newlines for readability
            text_escaped = text.replace('\n', '\\n').replace('\r', '\\r')
            f.write(f"{string_id:08X}\t{text_escaped}\n")

    print(f"Extracted {len(strings)} strings to {output_path}")


def main():
    if len(sys.argv) < 2:
        print("Usage: python extract_strings.py <file.STRINGS|DLSTRINGS|ILSTRINGS> [output.txt]")
        print("\nExample: python extract_strings.py Fallout4_en.STRINGS")
        return

    input_path = Path(sys.argv[1])
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 else None

    if not input_path.exists():
        print(f"File not found: {input_path}")
        return

    extract_to_text(input_path, output_path)


if __name__ == '__main__':
    main()
