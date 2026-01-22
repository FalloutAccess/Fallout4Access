# ImportCommonLibF4Symbols.py
# Ghidra Python script to import CommonLibF4 function names using Address Library
#
# Usage:
# 1. Open Fallout4.exe in Ghidra
# 2. Run this script from Script Manager (Window -> Script Manager)
# 3. Select the Address Library .bin file when prompted
# 4. Select the CommonLibF4 include directory when prompted
#
# @category Fallout4
# @author fallout4access

import struct
import os
import re
from ghidra.program.model.symbol import SourceType
from ghidra.program.model.address import AddressFactory

# Configuration
IMAGE_BASE = 0x140000000  # Fallout 4 default image base

def parse_address_library_v0(filepath):
    """Parse V0 format Address Library binary (id, offset pairs)"""
    mappings = {}

    with open(filepath, 'rb') as f:
        # First 8 bytes: count of entries
        count_bytes = f.read(8)
        count = struct.unpack('<Q', count_bytes)[0]

        print("Address Library: {} entries".format(count))

        # Read all mappings (16 bytes each: 8 byte id, 8 byte offset)
        for i in range(count):
            data = f.read(16)
            if len(data) < 16:
                break
            # V0 format: MAPPING struct is {uint64_t id; uint64_t offset;}
            rel_id, offset = struct.unpack('<QQ', data)
            mappings[rel_id] = offset

            if i % 100000 == 0:
                print("  Loaded {} entries...".format(i))

    print("Loaded {} mappings from Address Library".format(len(mappings)))
    return mappings

def parse_ids_header(filepath):
    """Parse CommonLibF4 IDs.h file to extract function names and IDs"""
    symbols = {}

    # Pattern to match: inline constexpr REL::ID FunctionName{ 12345 };
    # Also handles namespaced: namespace Foo { inline constexpr REL::ID Bar{ 123 }; }
    id_pattern = re.compile(r'inline\s+constexpr\s+REL::ID\s+(\w+)\s*\{\s*(\d+)\s*\}')

    current_namespace = []

    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            line = line.strip()

            # Track namespace
            ns_match = re.match(r'namespace\s+(\w+)', line)
            if ns_match:
                current_namespace.append(ns_match.group(1))
                continue

            # Track namespace end
            if line == '}' and current_namespace:
                current_namespace.pop()
                continue

            # Find ID definitions
            match = id_pattern.search(line)
            if match:
                func_name = match.group(1)
                rel_id = int(match.group(2))

                # Build full name with namespace, but skip RE:: and ID:: prefixes
                filtered_ns = [ns for ns in current_namespace if ns not in ('RE', 'ID')]
                if filtered_ns:
                    full_name = '::'.join(filtered_ns) + '::' + func_name
                else:
                    full_name = func_name

                if rel_id != 0:  # Skip placeholder IDs
                    symbols[rel_id] = full_name

    return symbols

def parse_vtable_header(filepath):
    """Parse CommonLibF4 IDs_VTABLE.h file to extract vtable IDs"""
    symbols = {}

    # Pattern for: inline constexpr std::array<REL::ID, N> ClassName{ REL::ID(123), ... };
    vtable_pattern = re.compile(r'inline\s+constexpr\s+std::array<REL::ID,\s*\d+>\s+(\w+)\s*\{([^}]+)\}')
    id_extract = re.compile(r'REL::ID\((\d+)\)')

    with open(filepath, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()

        for match in vtable_pattern.finditer(content):
            class_name = match.group(1)
            ids_str = match.group(2)

            # Extract all IDs from the array
            ids = id_extract.findall(ids_str)
            for idx, rel_id in enumerate(ids):
                rel_id = int(rel_id)
                if rel_id != 0:
                    if len(ids) == 1:
                        name = "vtable_" + class_name
                    else:
                        name = "vtable_{}_{}".format(class_name, idx)
                    symbols[rel_id] = name

    return symbols

def collect_all_symbols(commonlib_path):
    """Collect all symbols from CommonLibF4 headers"""
    all_symbols = {}

    # Main IDs.h
    ids_file = os.path.join(commonlib_path, 'include', 'RE', 'IDs.h')
    if os.path.exists(ids_file):
        print("Parsing IDs.h...")
        syms = parse_ids_header(ids_file)
        all_symbols.update(syms)
        print("  Found {} function IDs".format(len(syms)))

    # VTable IDs
    vtable_file = os.path.join(commonlib_path, 'include', 'RE', 'IDs_VTABLE.h')
    if os.path.exists(vtable_file):
        print("Parsing IDs_VTABLE.h...")
        syms = parse_vtable_header(vtable_file)
        all_symbols.update(syms)
        print("  Found {} vtable IDs".format(len(syms)))

    # RTTI IDs
    rtti_file = os.path.join(commonlib_path, 'include', 'RE', 'IDs_RTTI.h')
    if os.path.exists(rtti_file):
        print("Parsing IDs_RTTI.h...")
        syms = parse_ids_header(rtti_file)
        for rel_id, name in syms.items():
            all_symbols[rel_id] = "RTTI_" + name
        print("  Found {} RTTI IDs".format(len(syms)))

    return all_symbols

def apply_symbols_to_ghidra(mappings, symbols, image_base):
    """Apply symbols to the current Ghidra program"""
    program = currentProgram
    symbol_table = program.getSymbolTable()
    function_manager = program.getFunctionManager()
    address_factory = program.getAddressFactory()
    space = address_factory.getDefaultAddressSpace()

    applied = 0
    skipped = 0
    errors = 0
    renamed_functions = 0

    for rel_id, name in symbols.items():
        if rel_id not in mappings:
            skipped += 1
            continue

        offset = mappings[rel_id]
        addr = space.getAddress(image_base + offset)

        try:
            # Check if there's already a good name
            existing = symbol_table.getPrimarySymbol(addr)
            if existing:
                existing_name = existing.getName()
                # Skip if already has a meaningful name
                if not existing_name.startswith('FUN_') and not existing_name.startswith('DAT_') and not existing_name.startswith('LAB_'):
                    skipped += 1
                    continue

            # Sanitize name for Ghidra (replace :: with __)
            ghidra_name = name.replace('::', '__')

            # Check if this is a vtable (data) or a function
            is_vtable = name.startswith('vtable_') or name.startswith('RTTI_')

            if is_vtable:
                # Create data label for vtables
                symbol_table.createLabel(addr, ghidra_name, SourceType.IMPORTED)
                applied += 1
            else:
                # Try to rename the function if it exists
                func = function_manager.getFunctionAt(addr)
                if func:
                    func.setName(ghidra_name, SourceType.IMPORTED)
                    renamed_functions += 1
                    applied += 1
                else:
                    # No function here, just create a label
                    symbol_table.createLabel(addr, ghidra_name, SourceType.IMPORTED)
                    applied += 1

            if applied % 1000 == 0:
                print("  Applied {} symbols ({} function renames)...".format(applied, renamed_functions))

        except Exception as e:
            errors += 1
            if errors < 10:
                print("Error applying symbol {} at {}: {}".format(name, addr, str(e)))

    return applied, skipped, errors, renamed_functions

def main():
    from ghidra.util.task import TaskMonitor
    from javax.swing import JFileChooser
    from javax.swing.filechooser import FileNameExtensionFilter
    from java.io import File

    print("=" * 60)
    print("CommonLibF4 Symbol Importer for Ghidra")
    print("=" * 60)

    # Select Address Library file
    chooser = JFileChooser()
    chooser.setDialogTitle("Select Address Library .bin file")
    chooser.setFileFilter(FileNameExtensionFilter("Address Library (*.bin)", ["bin"]))
    chooser.setCurrentDirectory(File("E:/Games/ModOrganizer/Fallout 4/mods/Address Library for F4SE Plugins/F4SE/Plugins"))
    # Pre-select the newest version
    chooser.setSelectedFile(File("E:/Games/ModOrganizer/Fallout 4/mods/Address Library for F4SE Plugins/F4SE/Plugins/version-1-11-191-0.bin"))

    if chooser.showOpenDialog(None) != JFileChooser.APPROVE_OPTION:
        print("Cancelled - no Address Library selected")
        return

    address_lib_path = chooser.getSelectedFile().getAbsolutePath()
    print("Address Library: {}".format(address_lib_path))

    # Select CommonLibF4 directory
    chooser = JFileChooser()
    chooser.setDialogTitle("Select CommonLibF4 directory (containing 'include' folder)")
    chooser.setFileSelectionMode(JFileChooser.DIRECTORIES_ONLY)
    chooser.setCurrentDirectory(File("E:/Games/modding/Fallout4/fallout4access/lib/commonlibf4"))

    if chooser.showOpenDialog(None) != JFileChooser.APPROVE_OPTION:
        print("Cancelled - no CommonLibF4 directory selected")
        return

    commonlib_path = chooser.getSelectedFile().getAbsolutePath()
    print("CommonLibF4: {}".format(commonlib_path))

    # Parse Address Library
    print("\nParsing Address Library...")
    mappings = parse_address_library_v0(address_lib_path)

    # Collect symbols from CommonLibF4
    print("\nCollecting symbols from CommonLibF4...")
    symbols = collect_all_symbols(commonlib_path)
    print("Total symbols collected: {}".format(len(symbols)))

    # Apply to Ghidra
    print("\nApplying symbols to Ghidra...")
    applied, skipped, errors, renamed_functions = apply_symbols_to_ghidra(mappings, symbols, IMAGE_BASE)

    print("\n" + "=" * 60)
    print("Import Complete!")
    print("  Applied:  {} symbols".format(applied))
    print("  Renamed:  {} functions".format(renamed_functions))
    print("  Skipped:  {} (ID not in address library or already named)".format(skipped))
    print("  Errors:   {}".format(errors))
    print("=" * 60)

if __name__ == '__main__':
    main()
