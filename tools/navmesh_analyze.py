"""Analyze navmesh connectivity for a specific cell in Fallout4.esm"""
import struct
import zlib
from collections import deque

ESM = r"E:\SteamLibrary\steamapps\common\Fallout 4\Data\Fallout4.esm"
TARGET_CELL = 0x000D393F  # MQ203KelloggsBrain

# FO4 NVNM format (from xEdit wbDefinitionsFO4.pas):
# Header: version(u32) + CRC(u32) + worldspace(formid) + cell(formid) = 16 bytes
# Then: count(u32) + vertices(count * 12 bytes)
# Then: count(u32) + triangles(count * 21 bytes)
#   Triangle: v0(u16) v1(u16) v2(u16) edge01(s16) edge12(s16) edge20(s16) height(f32) unk(u8) flags(u16) coverFlags(u16)
# Then: count(u32) + edge_links(count * 11 bytes)
#   EdgeLink: type(u32) navmeshFormID(u32) triangle(s16) edgeIndex(u8)

VERT_SIZE = 12
TRI_SIZE = 21
EDGE_LINK_SIZE = 11

with open(ESM, "rb") as f:
    data = f.read()

pos = 0
navmeshes = []

while pos < len(data) - 24:
    if data[pos:pos+4] == b"NAVM":
        data_size = struct.unpack_from("<I", data, pos+4)[0]
        flags = struct.unpack_from("<I", data, pos+8)[0]
        form_id = struct.unpack_from("<I", data, pos+12)[0]
        compressed = (flags & 0x00040000) != 0
        record_start = pos + 24
        record_end = record_start + data_size
        cell_bytes = struct.pack("<I", TARGET_CELL)

        record_data = None
        if not compressed:
            if cell_bytes in data[record_start:record_end]:
                record_data = data[record_start:record_end]
        else:
            try:
                compressed_data = data[record_start+4:record_end]
                decompressed = zlib.decompress(compressed_data)
                if cell_bytes in decompressed:
                    record_data = decompressed
            except Exception:
                pass

        if record_data is not None:
            spos = 0
            while spos < len(record_data) - 6:
                sub_type = record_data[spos:spos+4]
                sub_size = struct.unpack_from("<H", record_data, spos+4)[0]

                if sub_type == b"NVNM":
                    nvnm = record_data[spos+6:spos+6+sub_size]
                    off = 0

                    # Header
                    if len(nvnm) < 20:
                        break
                    version = struct.unpack_from("<I", nvnm, 0)[0]
                    # crc(4) + worldspace(4) + cell(4) = 12 bytes
                    off = 16

                    # Vertices
                    num_verts = struct.unpack_from("<I", nvnm, off)[0]
                    off += 4
                    verts = []
                    for i in range(num_verts):
                        if off + VERT_SIZE > len(nvnm):
                            break
                        x, y, z = struct.unpack_from("<fff", nvnm, off)
                        verts.append((x, y, z))
                        off += VERT_SIZE

                    # Triangles
                    if off + 4 > len(nvnm):
                        break
                    num_tris = struct.unpack_from("<I", nvnm, off)[0]
                    off += 4
                    tris = []
                    for i in range(num_tris):
                        if off + TRI_SIZE > len(nvnm):
                            break
                        v0, v1, v2 = struct.unpack_from("<HHH", nvnm, off)
                        e01, e12, e20 = struct.unpack_from("<hhh", nvnm, off+6)
                        height = struct.unpack_from("<f", nvnm, off+12)[0]
                        unk = nvnm[off+16]
                        tri_flags = struct.unpack_from("<H", nvnm, off+17)[0]
                        cover_flags = struct.unpack_from("<H", nvnm, off+19)[0]
                        tris.append({
                            "verts": (v0, v1, v2),
                            "edges": (e01, e12, e20),
                            "height": height,
                            "flags": tri_flags,
                        })
                        off += TRI_SIZE

                    # Edge Links
                    edge_links = []
                    if off + 4 <= len(nvnm):
                        num_edge_links = struct.unpack_from("<I", nvnm, off)[0]
                        off += 4
                        for i in range(num_edge_links):
                            if off + EDGE_LINK_SIZE > len(nvnm):
                                break
                            link_type = struct.unpack_from("<I", nvnm, off)[0]
                            link_mesh = struct.unpack_from("<I", nvnm, off+4)[0]
                            link_tri = struct.unpack_from("<h", nvnm, off+8)[0]
                            link_edge = nvnm[off+10]
                            edge_links.append({
                                "type": link_type,
                                "navmesh": link_mesh,
                                "triangle": link_tri,
                                "edge": link_edge,
                            })
                            off += EDGE_LINK_SIZE

                    # Internal connectivity
                    visited = set()
                    components = []
                    for start_tri in range(len(tris)):
                        if start_tri in visited:
                            continue
                        component = set()
                        queue = deque([start_tri])
                        while queue:
                            t = queue.popleft()
                            if t in visited:
                                continue
                            visited.add(t)
                            component.add(t)
                            for edge_val in tris[t]["edges"]:
                                if edge_val >= 0 and edge_val < len(tris) and edge_val not in visited:
                                    queue.append(edge_val)
                        components.append(component)

                    bbox = None
                    if verts:
                        bbox = (
                            min(v[0] for v in verts), min(v[1] for v in verts), min(v[2] for v in verts),
                            max(v[0] for v in verts), max(v[1] for v in verts), max(v[2] for v in verts),
                        )

                    navmeshes.append({
                        "formid": form_id,
                        "num_verts": len(verts),
                        "num_tris": len(tris),
                        "bbox": bbox,
                        "components": len(components),
                        "component_sizes": sorted([len(c) for c in components], reverse=True),
                        "edge_links": edge_links,
                    })
                    break
                spos += 6 + sub_size

        pos = record_end if record_end > pos + 4 else pos + 4
    else:
        pos += 1

cell_formids = {nm["formid"] for nm in navmeshes}

print(f"Cell MQ203KelloggsBrain (0x{TARGET_CELL:08X}): {len(navmeshes)} navmeshes\n")

total_tris = 0
for nm in navmeshes:
    total_tris += nm["num_tris"]
    bbox = nm["bbox"]
    sizes = " ".join(str(s) for s in nm["component_sizes"])
    print(f"NAVM 0x{nm['formid']:08X}: {nm['num_verts']} verts, {nm['num_tris']} tris, "
          f"{nm['components']} internal component(s) [{sizes}]")
    if bbox:
        print(f"  BBox: X[{bbox[0]:.0f}..{bbox[3]:.0f}] Y[{bbox[1]:.0f}..{bbox[4]:.0f}] Z[{bbox[2]:.0f}..{bbox[5]:.0f}]")
    for link in nm["edge_links"]:
        in_cell = "IN-CELL" if link["navmesh"] in cell_formids else "EXTERNAL"
        type_names = {0: "Portal", 1: "Unused", 2: "Linked-Door"}
        tname = type_names.get(link["type"], f"Type{link['type']}")
        print(f"  EdgeLink: {tname} -> NAVM 0x{link['navmesh']:08X} tri {link['triangle']} ({in_cell})")
    print()

print(f"Total triangles across all navmeshes: {total_tris}\n")

# Build cross-navmesh connectivity graph
print("--- Cross-Navmesh Connectivity ---")
nm_graph = {nm["formid"]: set() for nm in navmeshes}
for nm in navmeshes:
    for link in nm["edge_links"]:
        target = link["navmesh"]
        if target in nm_graph:
            nm_graph[nm["formid"]].add(target)
            nm_graph[target].add(nm["formid"])

visited = set()
nm_components = []
for start in nm_graph:
    if start in visited:
        continue
    comp = set()
    queue = deque([start])
    while queue:
        n = queue.popleft()
        if n in visited:
            continue
        visited.add(n)
        comp.add(n)
        for neighbor in nm_graph[n]:
            if neighbor not in visited:
                queue.append(neighbor)
    nm_components.append(comp)

print(f"Connected navmesh islands: {len(nm_components)}")
for i, comp in enumerate(nm_components):
    ids = ", ".join(f"0x{fid:08X}" for fid in sorted(comp))
    total = sum(nm["num_tris"] for nm in navmeshes if nm["formid"] in comp)
    print(f"  Island {i+1} ({len(comp)} meshes, {total} tris): {ids}")
