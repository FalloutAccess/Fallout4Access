"""Parse NIF files to extract collision geometry bounds for walkable surface dimensions."""
import struct
import os

NIF_DIR = r"E:\Games\modding\Fallout4\fallout4access\tools\nifs"


def parse_nif_header(data):
    """Parse NIF header and return block info."""
    header_line_end = data.index(b"\x0a") + 1

    pos = header_line_end
    version = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    pos += 1  # endian byte
    pos += 4  # user_version
    num_blocks = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    pos += 4  # bs_version

    # Author string
    author_len = data[pos]
    pos += 1 + author_len
    pos += 4  # unknown int
    # Process script
    proc_len = data[pos]
    pos += 1 + proc_len
    # Export script
    exp_len = data[pos]
    pos += 1 + exp_len

    # Block type names
    num_block_types = struct.unpack_from("<H", data, pos)[0]
    pos += 2
    block_type_names = []
    for _ in range(num_block_types):
        name_len = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        name = data[pos : pos + name_len].decode("ascii", errors="replace")
        block_type_names.append(name)
        pos += name_len

    # Block type indices
    block_type_indices = []
    for _ in range(num_blocks):
        idx = struct.unpack_from("<H", data, pos)[0]
        block_type_indices.append(idx)
        pos += 2

    # Block sizes
    block_sizes = []
    for _ in range(num_blocks):
        size = struct.unpack_from("<I", data, pos)[0]
        block_sizes.append(size)
        pos += 4

    # String table
    num_strings = struct.unpack_from("<I", data, pos)[0]
    pos += 4
    pos += 4  # max_string_len
    for _ in range(num_strings):
        slen = struct.unpack_from("<I", data, pos)[0]
        pos += 4 + slen

    pos += 4  # unknown int

    return block_type_names, block_type_indices, block_sizes, pos


def find_collision_bounds(data, block_type_names, block_type_indices, block_sizes, blocks_start):
    """Find collision shape bounds in NIF blocks."""
    results = {}
    block_offset = blocks_start

    for i in range(len(block_sizes)):
        btype = block_type_names[block_type_indices[i]]
        bsize = block_sizes[i]
        block_data = data[block_offset : block_offset + bsize]

        if btype == "bhkBoxShape":
            # Search for half-extents: 3 positive floats + w≈0
            for off in range(0, len(block_data) - 16, 4):
                hx, hy, hz, hw = struct.unpack_from("<ffff", block_data, off)
                if 0.5 < hx < 500 and 0.5 < hy < 500 and 0.1 < hz < 500:
                    if abs(hw) < 1.0:
                        if "boxes" not in results:
                            results["boxes"] = []
                        results["boxes"].append((hx, hy, hz))

        if btype == "bhkConvexVerticesShape":
            # Find vertex array: numVerts(uint32) then hkVector4 vertices
            for off in range(0, min(40, len(block_data) - 4), 4):
                nv = struct.unpack_from("<I", block_data, off)[0]
                if 4 <= nv <= 200:
                    verts = []
                    voff = off + 4
                    valid = True
                    for _ in range(nv):
                        if voff + 16 > len(block_data):
                            valid = False
                            break
                        x, y, z, w = struct.unpack_from("<ffff", block_data, voff)
                        # Havok uses meters (game units / 69.99)
                        if abs(x) > 20 or abs(y) > 20 or abs(z) > 20:
                            valid = False
                            break
                        verts.append((x, y, z))
                        voff += 16
                    if valid and len(verts) == nv:
                        min_x = min(v[0] for v in verts)
                        max_x = max(v[0] for v in verts)
                        min_y = min(v[1] for v in verts)
                        max_y = max(v[1] for v in verts)
                        min_z = min(v[2] for v in verts)
                        max_z = max(v[2] for v in verts)
                        HAVOK_SCALE = 69.99  # Havok to game units
                        if "convex" not in results:
                            results["convex"] = []
                        results["convex"].append({
                            "nverts": nv,
                            "min_havok": (min_x, min_y, min_z),
                            "max_havok": (max_x, max_y, max_z),
                            "size_game": (
                                (max_x - min_x) * HAVOK_SCALE,
                                (max_y - min_y) * HAVOK_SCALE,
                                (max_z - min_z) * HAVOK_SCALE,
                            ),
                            "verts_havok": verts,
                        })
                        break

        block_offset += bsize

    return results


for name in sorted(os.listdir(NIF_DIR)):
    if not name.endswith(".nif"):
        continue
    path = os.path.join(NIF_DIR, name)
    print(f"=== {name} ===")
    with open(path, "rb") as f:
        data = f.read()

    try:
        type_names, type_indices, sizes, bstart = parse_nif_header(data)
        collision_types = [n for n in type_names if "bhk" in n]
        print(f"  Collision types: {collision_types}")

        results = find_collision_bounds(data, type_names, type_indices, sizes, bstart)

        if "boxes" in results:
            for hx, hy, hz in results["boxes"]:
                print(f"  Box half-extents (Havok): ({hx:.3f}, {hy:.3f}, {hz:.3f})")
                print(f"  Box size (game units): ({hx*2*69.99:.0f} x {hy*2*69.99:.0f} x {hz*2*69.99:.0f})")

        if "convex" in results:
            for cv in results["convex"]:
                print(f"  Convex shape ({cv['nverts']} verts):")
                print(f"    Havok bounds: {cv['min_havok']} to {cv['max_havok']}")
                print(f"    Game unit size: X={cv['size_game'][0]:.0f} Y={cv['size_game'][1]:.0f} Z={cv['size_game'][2]:.0f}")

        if not results:
            print("  No collision shapes found")
    except Exception as e:
        print(f"  Error: {e}")
    print()
