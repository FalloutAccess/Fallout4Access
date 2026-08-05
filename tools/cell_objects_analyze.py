"""
Analyze placed objects in Fallout 4's MQ203KelloggsBrain cell (0x000D393F).
Finds floor/walkway objects in the "gray void" between memory scene navmesh islands.

Parses Fallout4.esm directly to find REFR records that are children of the target cell.
"""

import struct
import zlib
import sys
from collections import defaultdict
from pathlib import Path

ESM_PATH = Path(r"E:\SteamLibrary\steamapps\common\Fallout 4\Data\Fallout4.esm")
TARGET_CELL = 0x000D393F

# Navmesh island bounding boxes (X_min, X_max, Y_min, Y_max) with some padding
NAVMESH_ISLANDS = [
    (3651, 4322, -5383, -4652),
    (6057, 7127, -3960, -1861),
    (512, 1167, -3799, -3174),
    (6529, 7295, 197, 1008),
    (-4510, -4032, -8, 510),
    (-5039, -4789, -54, 443),
    (-2794, -2360, -3945, -3245),
    (4069, 4638, 1290, 2204),
    (-2155, 845, -1507, -909),
    (-5607, -5386, 244, 469),
    (-5588, -5369, -304, -72),
]

# Expand bounding boxes by this many units to account for objects at the edges
ISLAND_PADDING = 200


def is_in_navmesh_island(x, y):
    """Check if an XY position falls within any navmesh island bounding box."""
    for xmin, xmax, ymin, ymax in NAVMESH_ISLANDS:
        if (xmin - ISLAND_PADDING <= x <= xmax + ISLAND_PADDING and
                ymin - ISLAND_PADDING <= y <= ymax + ISLAND_PADDING):
            return True
    return False


def nearest_island(x, y):
    """Return index of nearest navmesh island and distance to it."""
    best_dist = float('inf')
    best_idx = -1
    for i, (xmin, xmax, ymin, ymax) in enumerate(NAVMESH_ISLANDS):
        cx = (xmin + xmax) / 2
        cy = (ymin + ymax) / 2
        dist = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
        if dist < best_dist:
            best_dist = dist
            best_idx = i
    return best_idx, best_dist


def read_subrecords(data):
    """Parse subrecords from record data. Yields (type, data) tuples."""
    pos = 0
    while pos < len(data):
        if pos + 6 > len(data):
            break
        sub_type = data[pos:pos + 4]
        sub_size = struct.unpack_from('<H', data, pos + 4)[0]
        pos += 6
        if pos + sub_size > len(data):
            break
        sub_data = data[pos:pos + sub_size]
        pos += sub_size
        yield sub_type, sub_data


def parse_refr(record_data, flags):
    """Parse a REFR record's data, return dict with extracted fields."""
    # Handle compressed records
    if flags & 0x40000:
        if len(record_data) < 4:
            return None
        decompressed_size = struct.unpack_from('<I', record_data, 0)[0]
        try:
            record_data = zlib.decompress(record_data[4:], bufsize=decompressed_size)
        except zlib.error:
            return None

    result = {}
    for sub_type, sub_data in read_subrecords(record_data):
        if sub_type == b'NAME' and len(sub_data) == 4:
            result['base_formid'] = struct.unpack_from('<I', sub_data, 0)[0]
        elif sub_type == b'DATA' and len(sub_data) >= 24:
            x, y, z, rx, ry, rz = struct.unpack_from('<6f', sub_data, 0)
            result['pos'] = (x, y, z)
            result['rot'] = (rx, ry, rz)
        elif sub_type == b'XSCL' and len(sub_data) == 4:
            result['scale'] = struct.unpack_from('<f', sub_data, 0)[0]
    return result


def find_cell_children_groups(f, file_size):
    """
    Scan through the ESM to find GRUP type 6/8/9 groups for our target cell.
    Returns list of (offset, size, group_type) for matching groups.

    Strategy: We scan the top-level groups to find the CELL group,
    then dive into it to find our target cell's child groups.
    """
    results = []

    # Read top-level groups
    f.seek(0)
    # Skip TES4 header record
    header_type = f.read(4)
    if header_type != b'TES4':
        print(f"ERROR: Expected TES4, got {header_type}")
        return results
    header_size = struct.unpack('<I', f.read(4))[0]
    f.seek(f.tell() + 16 + header_size)  # skip rest of header + data

    print(f"File size: {file_size:,} bytes")
    print(f"Scanning top-level groups for CELL group...")

    while f.tell() < file_size:
        pos = f.tell()
        chunk = f.read(24)
        if len(chunk) < 24:
            break

        rec_type = chunk[0:4]
        if rec_type != b'GRUP':
            # Skip non-group record at top level (shouldn't happen)
            data_size = struct.unpack_from('<I', chunk, 4)[0]
            f.seek(pos + 24 + data_size)
            continue

        group_size = struct.unpack_from('<I', chunk, 4)[0]
        label = chunk[8:12]
        group_type = struct.unpack_from('<I', chunk, 12)[0]

        if group_type == 0 and label == b'CELL':
            print(f"  Found CELL top group at offset {pos:#x}, size {group_size:,}")
            # Now scan inside this group for our target cell
            scan_cell_group(f, pos + 24, pos + group_size, results)
            break  # Only one CELL top group
        else:
            f.seek(pos + group_size)

    return results


def scan_cell_group(f, start, end, results):
    """
    Scan inside the CELL top-level group. Structure:
    - GRUP type 2 (interior cell block)
      - GRUP type 3 (interior cell sub-block)
        - CELL record
        - GRUP type 6 (cell children)
          - GRUP type 8 (persistent children)
          - GRUP type 9 (temporary children)
            - REFR records
    """
    f.seek(start)
    found_cell = False

    while f.tell() < end:
        pos = f.tell()
        chunk = f.read(24)
        if len(chunk) < 24:
            break

        rec_type = chunk[0:4]
        size_field = struct.unpack_from('<I', chunk, 4)[0]

        if rec_type == b'GRUP':
            group_size = size_field
            label = chunk[8:12]
            group_type = struct.unpack_from('<I', chunk, 12)[0]

            if group_type == 2:
                # Interior cell block - scan inside
                print(f"    Interior cell block at {pos:#x}, size {group_size:,}")
                scan_cell_group(f, pos + 24, pos + group_size, results)
                f.seek(pos + group_size)
            elif group_type == 3:
                # Interior cell sub-block - scan inside
                scan_cell_group(f, pos + 24, pos + group_size, results)
                f.seek(pos + group_size)
            elif group_type in (6, 8, 9):
                label_formid = struct.unpack_from('<I', label, 0)[0]
                if label_formid == TARGET_CELL:
                    print(f"    FOUND target cell child group type {group_type} at {pos:#x}, size {group_size:,}")
                    results.append((pos, group_size, group_type))
                    found_cell = True
                f.seek(pos + group_size)
            else:
                f.seek(pos + group_size)
        else:
            # Regular record (CELL, etc.) - check if it's our cell
            data_size = size_field
            formid = struct.unpack_from('<I', chunk, 12)[0]
            if rec_type == b'CELL' and formid == TARGET_CELL:
                print(f"    FOUND target CELL record at {pos:#x}, FormID {formid:#010x}")
                found_cell = True
            f.seek(pos + 24 + data_size)

    return found_cell


def extract_refs_from_group(f, group_offset, group_size):
    """Extract all REFR records from a group (type 6, 8, or 9)."""
    refs = []
    end = group_offset + group_size
    f.seek(group_offset + 24)  # Skip group header

    while f.tell() < end:
        pos = f.tell()
        chunk = f.read(24)
        if len(chunk) < 24:
            break

        rec_type = chunk[0:4]
        size_field = struct.unpack_from('<I', chunk, 4)[0]

        if rec_type == b'GRUP':
            # Nested group (type 8 or 9 inside type 6)
            group_size_inner = size_field
            inner_type = struct.unpack_from('<I', chunk, 12)[0]
            print(f"      Nested group type {inner_type} at {pos:#x}, size {group_size_inner:,}")
            inner_refs = extract_refs_from_group(f, pos, group_size_inner)
            refs.extend(inner_refs)
            f.seek(pos + group_size_inner)
        else:
            data_size = size_field
            flags = struct.unpack_from('<I', chunk, 8)[0]
            formid = struct.unpack_from('<I', chunk, 12)[0]

            if rec_type == b'REFR':
                record_data = f.read(data_size)
                parsed = parse_refr(record_data, flags)
                if parsed and 'pos' in parsed:
                    parsed['formid'] = formid
                    parsed['flags'] = flags
                    refs.append(parsed)
            else:
                f.seek(pos + 24 + data_size)

    return refs


def collect_base_object_types(f, formids_to_lookup):
    """
    Look up the record type (STAT, MSTT, ACHR, etc.) for a set of base object FormIDs.
    Scans the file for these records.
    """
    if not formids_to_lookup:
        return {}

    lookup_set = set(formids_to_lookup)
    result = {}

    f.seek(0)
    file_size = f.seek(0, 2)
    f.seek(0)

    # Skip TES4
    f.read(4)
    header_size = struct.unpack('<I', f.read(4))[0]
    f.seek(f.tell() + 16 + header_size)

    print(f"\nLooking up record types for {len(lookup_set)} base objects...")

    while f.tell() < file_size and lookup_set:
        pos = f.tell()
        chunk = f.read(24)
        if len(chunk) < 24:
            break

        rec_type = chunk[0:4]
        size_field = struct.unpack_from('<I', chunk, 4)[0]

        if rec_type == b'GRUP':
            # Enter group
            continue  # groups contain records, just keep reading
        else:
            data_size = size_field
            formid = struct.unpack_from('<I', chunk, 12)[0]

            if formid in lookup_set:
                result[formid] = rec_type.decode('ascii', errors='replace')
                lookup_set.discard(formid)

            f.seek(pos + 24 + data_size)

    return result


def resolve_base_types_targeted(f, formid_set):
    """
    Resolve base object types by scanning only relevant top-level groups.
    Much faster than scanning the entire file.
    """
    result = {}
    remaining = set(formid_set)

    file_size = f.seek(0, 2)
    f.seek(0)

    # Skip TES4
    f.read(4)
    header_size = struct.unpack('<I', f.read(4))[0]
    f.seek(f.tell() + 16 + header_size)

    # Relevant record types for placed objects
    relevant_groups = {b'STAT', b'MSTT', b'ACTI', b'FURN', b'DOOR', b'CONT',
                       b'LIGH', b'NPC_', b'FLOR', b'TREE', b'SCOL', b'TXST',
                       b'MOVT', b'SNDR', b'ADDN', b'HAZD', b'SPEL', b'MISC',
                       b'ARMO', b'WEAP', b'AMMO', b'ALCH', b'BOOK', b'NOTE',
                       b'KEYM', b'TERM', b'IDLM', b'BNDS', b'TXST'}

    print(f"\nResolving base object types for {len(remaining)} FormIDs...")

    while f.tell() < file_size and remaining:
        pos = f.tell()
        chunk = f.read(24)
        if len(chunk) < 24:
            break

        rec_type = chunk[0:4]
        size_field = struct.unpack_from('<I', chunk, 4)[0]

        if rec_type == b'GRUP':
            group_size = size_field
            group_type = struct.unpack_from('<I', chunk, 12)[0]

            if group_type == 0:
                label = chunk[8:12]
                # Scan inside relevant groups
                scan_group_for_formids(f, pos + 24, pos + group_size, remaining, result)
                f.seek(pos + group_size)
            else:
                # skip non-top-level groups we encounter at unexpected places
                f.seek(pos + group_size)
        else:
            data_size = size_field
            formid = struct.unpack_from('<I', chunk, 12)[0]
            if formid in remaining:
                result[formid] = rec_type.decode('ascii', errors='replace')
                remaining.discard(formid)
            f.seek(pos + 24 + data_size)

    print(f"  Resolved {len(result)} of {len(formid_set)} base objects. {len(remaining)} unresolved.")
    return result


def scan_group_for_formids(f, start, end, remaining, result):
    """Recursively scan a group looking for records with matching FormIDs."""
    f.seek(start)
    while f.tell() < end and remaining:
        pos = f.tell()
        chunk = f.read(24)
        if len(chunk) < 24:
            break

        rec_type = chunk[0:4]
        size_field = struct.unpack_from('<I', chunk, 4)[0]

        if rec_type == b'GRUP':
            group_size = size_field
            scan_group_for_formids(f, pos + 24, pos + group_size, remaining, result)
            f.seek(pos + group_size)
        else:
            data_size = size_field
            formid = struct.unpack_from('<I', chunk, 12)[0]
            if formid in remaining:
                result[formid] = rec_type.decode('ascii', errors='replace')
                remaining.discard(formid)
            f.seek(pos + 24 + data_size)


def get_editorids_for_formids(f, formid_set):
    """Get EDID (editor ID) subrecords for a set of FormIDs."""
    result = {}
    remaining = set(formid_set)
    file_size = f.seek(0, 2)
    f.seek(0)

    # Skip TES4
    f.read(4)
    header_size = struct.unpack('<I', f.read(4))[0]
    f.seek(f.tell() + 16 + header_size)

    print(f"\nResolving editor IDs for {len(remaining)} FormIDs...")

    def scan_for_edids(start, end):
        f.seek(start)
        while f.tell() < end and remaining:
            pos = f.tell()
            chunk = f.read(24)
            if len(chunk) < 24:
                break

            rec_type = chunk[0:4]
            size_field = struct.unpack_from('<I', chunk, 4)[0]

            if rec_type == b'GRUP':
                scan_for_edids(pos + 24, pos + size_field)
                f.seek(pos + size_field)
            else:
                data_size = size_field
                flags = struct.unpack_from('<I', chunk, 8)[0]
                formid = struct.unpack_from('<I', chunk, 12)[0]

                if formid in remaining:
                    record_data = f.read(data_size)
                    # Decompress if needed
                    if flags & 0x40000 and len(record_data) >= 4:
                        try:
                            dec_size = struct.unpack_from('<I', record_data, 0)[0]
                            record_data = zlib.decompress(record_data[4:], bufsize=dec_size)
                        except zlib.error:
                            record_data = b''

                    # Find EDID subrecord
                    for sub_type, sub_data in read_subrecords(record_data):
                        if sub_type == b'EDID':
                            edid = sub_data.rstrip(b'\x00').decode('ascii', errors='replace')
                            result[formid] = edid
                            break

                    remaining.discard(formid)
                else:
                    f.seek(pos + 24 + data_size)

    scan_for_edids(f.tell(), file_size)
    print(f"  Resolved {len(result)} editor IDs.")
    return result


def main():
    print(f"Opening {ESM_PATH}")
    print(f"Target cell: {TARGET_CELL:#010x} (MQ203KelloggsBrain)")
    print()

    with open(ESM_PATH, 'rb') as f:
        file_size = f.seek(0, 2)
        f.seek(0)

        # Step 1: Find the cell's child groups
        groups = find_cell_children_groups(f, file_size)

        if not groups:
            print("\nERROR: Could not find target cell's child groups!")
            sys.exit(1)

        # Step 2: Extract REFR records from all child groups
        all_refs = []
        for offset, size, gtype in groups:
            type_name = {6: "cell children", 8: "persistent", 9: "temporary"}.get(gtype, f"type {gtype}")
            print(f"\n  Extracting REFRs from {type_name} group...")
            refs = extract_refs_from_group(f, offset, size)
            print(f"    Found {len(refs)} REFR records")
            all_refs.extend(refs)

        print(f"\nTotal REFR records found: {len(all_refs)}")

        # Step 3: Resolve base object types
        base_formids = {r['base_formid'] for r in all_refs if 'base_formid' in r}
        base_types = resolve_base_types_targeted(f, base_formids)

        # Step 4: Get editor IDs for base objects
        edids = get_editorids_for_formids(f, base_formids)

    # Annotate refs with base type and EDID
    for ref in all_refs:
        base = ref.get('base_formid', 0)
        ref['base_type'] = base_types.get(base, '????')
        ref['edid'] = edids.get(base, '')

    # Step 5: Categorize objects
    in_void = []
    on_island = []

    for ref in all_refs:
        x, y, z = ref['pos']
        if is_in_navmesh_island(x, y):
            on_island.append(ref)
        else:
            in_void.append(ref)

    print(f"\n{'=' * 80}")
    print(f"RESULTS SUMMARY")
    print(f"{'=' * 80}")
    print(f"Total placed objects: {len(all_refs)}")
    print(f"Objects on navmesh islands: {len(on_island)}")
    print(f"Objects in void areas: {len(in_void)}")

    # Group void objects by type
    void_by_type = defaultdict(list)
    for ref in in_void:
        void_by_type[ref['base_type']].append(ref)

    print(f"\n--- Void objects by record type ---")
    for rtype in sorted(void_by_type.keys()):
        refs = void_by_type[rtype]
        print(f"  {rtype}: {len(refs)} objects")

    # Focus on static/structural types that could be walkways
    structural_types = {'STAT', 'MSTT', 'SCOL', 'ACTI'}
    walkway_candidates = [r for r in in_void if r['base_type'] in structural_types]

    print(f"\n{'=' * 80}")
    print(f"POTENTIAL WALKWAY/FLOOR OBJECTS IN VOID (STAT/MSTT/SCOL/ACTI)")
    print(f"{'=' * 80}")
    print(f"Found {len(walkway_candidates)} structural objects in void areas\n")

    # Group by Z level (rounded to nearest 50 units)
    by_z = defaultdict(list)
    for ref in walkway_candidates:
        z_level = round(ref['pos'][2] / 50) * 50
        by_z[z_level].append(ref)

    for z_level in sorted(by_z.keys()):
        refs = by_z[z_level]
        print(f"\n--- Z level ~{z_level:.0f} ({len(refs)} objects) ---")

        # Group by EDID to show unique base objects
        by_edid = defaultdict(list)
        for ref in refs:
            key = ref['edid'] or f"[{ref['base_formid']:#010x}]"
            by_edid[key].append(ref)

        for edid in sorted(by_edid.keys()):
            instances = by_edid[edid]
            ref0 = instances[0]
            base_fid = ref0['base_formid']
            btype = ref0['base_type']

            if len(instances) == 1:
                x, y, z = ref0['pos']
                scale = ref0.get('scale', 1.0)
                idx, dist = nearest_island(x, y)
                print(f"  {btype} {base_fid:#010x} {edid:40s} pos=({x:8.1f}, {y:8.1f}, {z:8.1f}) "
                      f"scale={scale:.2f} near_island={idx} dist={dist:.0f}")
            else:
                print(f"  {btype} {base_fid:#010x} {edid:40s} x{len(instances)} instances:")
                for inst in sorted(instances, key=lambda r: (r['pos'][0], r['pos'][1])):
                    x, y, z = inst['pos']
                    scale = inst.get('scale', 1.0)
                    idx, dist = nearest_island(x, y)
                    print(f"    REFR {inst['formid']:#010x} pos=({x:8.1f}, {y:8.1f}, {z:8.1f}) "
                          f"scale={scale:.2f} near_island={idx} dist={dist:.0f}")

    # Also show unique base objects used as walkways with counts
    print(f"\n{'=' * 80}")
    print(f"UNIQUE BASE OBJECTS IN VOID (structural types)")
    print(f"{'=' * 80}")

    base_counts = defaultdict(int)
    for ref in walkway_candidates:
        key = (ref['base_formid'], ref['base_type'], ref['edid'])
        base_counts[key] += 1

    for (fid, btype, edid), count in sorted(base_counts.items(), key=lambda x: -x[1]):
        print(f"  {btype} {fid:#010x} {edid:50s} x{count}")

    # Show what connects islands
    print(f"\n{'=' * 80}")
    print(f"OBJECTS BETWEEN SPECIFIC ISLAND PAIRS")
    print(f"{'=' * 80}")

    for ref in walkway_candidates:
        x, y, z = ref['pos']
        # Find two nearest islands
        distances = []
        for i, (xmin, xmax, ymin, ymax) in enumerate(NAVMESH_ISLANDS):
            cx = (xmin + xmax) / 2
            cy = (ymin + ymax) / 2
            dist = ((x - cx) ** 2 + (y - cy) ** 2) ** 0.5
            distances.append((dist, i))
        distances.sort()
        ref['nearest_two'] = (distances[0][1], distances[1][1])
        ref['nearest_dists'] = (distances[0][0], distances[1][0])

    # Group by island pairs
    by_pair = defaultdict(list)
    for ref in walkway_candidates:
        pair = tuple(sorted(ref['nearest_two']))
        by_pair[pair].append(ref)

    for pair in sorted(by_pair.keys()):
        refs = by_pair[pair]
        i1, i2 = pair
        island1_center = ((NAVMESH_ISLANDS[i1][0] + NAVMESH_ISLANDS[i1][1]) / 2,
                          (NAVMESH_ISLANDS[i1][2] + NAVMESH_ISLANDS[i1][3]) / 2)
        island2_center = ((NAVMESH_ISLANDS[i2][0] + NAVMESH_ISLANDS[i2][1]) / 2,
                          (NAVMESH_ISLANDS[i2][2] + NAVMESH_ISLANDS[i2][3]) / 2)
        print(f"\n  Between island {i1} ({island1_center[0]:.0f},{island1_center[1]:.0f}) "
              f"and island {i2} ({island2_center[0]:.0f},{island2_center[1]:.0f}): "
              f"{len(refs)} objects")
        for ref in sorted(refs, key=lambda r: r['nearest_dists'][0]):
            x, y, z = ref['pos']
            edid = ref['edid'] or f"[{ref['base_formid']:#010x}]"
            print(f"    {ref['base_type']} {edid:40s} ({x:8.1f}, {y:8.1f}, {z:8.1f})")


if __name__ == '__main__':
    main()
