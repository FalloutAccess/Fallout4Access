"""
Generate navmesh ESP plugin for MQ203KelloggsBrain cell walkway bridges.

Creates NAVM records for bridge pieces with edge links for cross-piece
pathfinding.  Straight/ramp pieces get 1 NAVM each; turn pieces get 2 NAVMs
(incoming arm + outgoing arm) connected by an internal edge link to avoid
concave L-shapes that the CK rejects ("opposite normals").

Output: Data/fallout4access_navmesh.esp
"""

import math
import struct
import zlib
from dataclasses import dataclass, field
from pathlib import Path

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

TARGET_CELL = 0x000D393F  # MQ203KelloggsBrain
CELL_EDID = "MQ203KelloggsBrain"

# NVNM constants
NVNM_VERSION = 15
NVNM_CRC = 0xA5E9A03C  # "PathingCell"

# ESP record format
RECORD_HEADER_SIZE = 24
GROUP_HEADER_SIZE = 24
FO4_FORM_VERSION = 131
COMPRESSED_FLAG = 0x00040000

# Bridge geometry
BRIDGE_LENGTH = 362.0
BRIDGE_WIDTH = 600.0
HALF_LEN = BRIDGE_LENGTH / 2.0
HALF_WID = BRIDGE_WIDTH / 2.0

# Connectivity threshold (center-to-center distance for piece pre-filter)
CONNECT_DIST = 540.0

# FormID allocation: master 0 = Fallout4.esm, our plugin = index 01
NAVM_BASE_FORMID = 0x01000800
CELL_OVERRIDE_FORMID = 0x000D393F  # Override existing cell (master 00 prefix)

# Block/sub-block for interior cell 0x000D393F
# last 12 bits = 0x93F = 2367
# block = 2367 % 10 = 7, sub_block = (2367 // 10) % 10 = 6
BLOCK_NUM = 7
SUB_BLOCK_NUM = 6

# ESM path for reading island navmeshes
ESM_PATH = Path(r"E:\SteamLibrary\steamapps\common\Fallout 4\Data\Fallout4.esm")

# Island NAVM FormIDs from Fallout4.esm that need edge links to our bridges
ISLAND_NAVM_FORMIDS = {
    0x0006401E,  # Island 0
    0x0006401D,  # Island 1
    0x0006401C,  # Island 2
    0x00048AC0,  # Island 3
    0x00048AC6,  # Island 7
    0x00048AC7,  # Island 8
}

# Maximum distance (in units) for bridge-to-island connections
ISLAND_CONNECT_DIST = 700.0

# Bridge endpoint to island connection data
# (bridge_index, endpoint_name, endpoint_xy, island_navm_formid)
ISLAND_CONNECTIONS = [
    (43, "left",  (6946, 344),    0x00048AC0),  # Bridge[43] TurnLeft -> Island 3, dist=261
    (2,  "front", (-328, -1389),  0x00048AC7),  # Bridge[2] Straight -> Island 8, dist=374
    (43, "back",  (6737, 197),    0x00048AC0),  # Bridge[43] TurnLeft -> Island 3, dist=441
    (4,  "front", (771, -3000),   0x0006401C),  # Bridge[4] Straight -> Island 2, dist=491
    (22, "front", (4625, 1321),   0x00048AC6),  # Bridge[22] RampUp -> Island 7, dist=505
    (17, "back",  (3610, -4563),  0x0006401E),  # Bridge[17] RampUp -> Island 0, dist=591
    (27, "front", (7154, 47),     0x00048AC0),  # Bridge[27] RampDown -> Island 3, dist=606
    (7,  "back",  (6136, -3322),  0x0006401D),  # Bridge[7] Straight -> Island 1, dist=614
    (21, "back",  (4246, -4428),  0x0006401E),  # Bridge[21] RampUp -> Island 0, dist=644
]

# Output path
SCRIPT_DIR = Path(__file__).resolve().parent
OUTPUT_PATH = SCRIPT_DIR.parent / "Data" / "fallout4access_navmesh.esp"


# ---------------------------------------------------------------------------
# Bridge piece data
# ---------------------------------------------------------------------------

PIECES = [
    # (type, formid, x, y, z, rz_rad)
    ("Start", 0x0006510E, 2329, 574, -1350, 5.811),
    ("Straight", 0x0005666F, -3574, 319, 155, 3.142),
    ("Straight", 0x001012A3, -328, -1208, 32, 3.142),
    ("Straight", 0x0005667F, 440, -1976, -96, 4.712),
    ("Straight", 0x00100CD1, 952, -3000, -96, 4.712),
    ("Straight", 0x0005503E, 4406, -2683, -977, 2.164),
    ("Straight", 0x00055040, 5611, -3186, -1105, 3.735),
    ("Straight", 0x0005503F, 6035, -3472, -1105, 3.735),
    ("Straight", 0x001287DB, 7152, -1436, -1104, 1.745),
    ("RampUp", 0x000FF97D, -3319, -962, 155, 4.712),
    ("RampUp", 0x00100EB6, -2551, -1218, 27, 3.142),
    ("RampUp", 0x00054E21, 184, -1208, -96, 3.142),
    ("RampUp", 0x0010129F, 1661, -4668, -216, 3.752),
    ("RampUp", 0x00054DEA, 2311, -4186, -344, 2.182),
    ("RampUp", 0x00054DEE, 2895, 1153, -1350, 4.237),
    ("RampUp", 0x00054DE8, 3087, -4417, -472, 3.752),
    ("RampUp", 0x00054DEF, 3116, 1843, -1222, 1.096),
    ("RampUp", 0x00054E13, 3506, -4711, -600, 3.752),
    ("RampUp", 0x00054DF4, 3695, 1277, -1094, 5.811),
    ("RampUp", 0x00054E17, 3903, -3887, -849, 0.593),
    ("RampUp", 0x00054E15, 4120, -3107, -977, 2.164),
    ("RampUp", 0x0010129D, 4396, -4529, -721, 2.164),
    ("RampUp", 0x00048AD7, 4458, 1251, -969, 1.173),
    ("RampUp", 0x00054E1A, 5186, -2900, -1105, 3.735),
    ("RampUp", 0x00054DE3, 5265, 1313, -841, 5.885),
    ("RampUp", 0x00054DE5, 6072, 1375, -713, 1.173),
    ("RampDown", 0x00054E23, -3319, -450, 283, 4.712),
    ("RampDown", 0x00054DE7, 7122, -131, -592, 0.175),
    ("RampDown", 0x00054DE2, 7626, -219, -720, 0.175),
    ("RampDown", 0x00056672, 7953, -1317, -976, 3.316),
    ("RampDown", 0x00054E01, 8338, -605, -848, 1.745),
    ("TurnLeft", 0x00054E24, -3319, 62, 155, 4.712),
    ("TurnLeft", 0x0005667D, 440, -1464, -96, 4.712),
    ("TurnLeft", 0x00056675, 952, -2488, -96, 4.712),
    ("TurnLeft", 0x00056682, 1178, -4018, -88, 5.323),
    ("TurnLeft", 0x00054DE9, 2667, -4123, -344, 3.752),
    ("TurnLeft", 0x00167A51, 2785, 809, -1350, 5.811),
    ("TurnLeft", 0x00054DF0, 3351, 1388, -1094, 1.096),
    ("TurnLeft", 0x00054E16, 3834, -3531, -849, 2.164),
    ("TurnLeft", 0x00054E25, 4657, 779, -841, 1.173),
    ("TurnLeft", 0x00054E1B, 4762, -2613, -977, 3.735),
    ("TurnLeft", 0x00054DF9, 4992, 643, -841, 5.885),
    ("TurnLeft", 0x001287DE, 6271, 904, -585, 1.173),
    ("TurnLeft", 0x00048AD6, 6915, 166, -592, 1.745),
    ("TurnLeft", 0x00054E0F, 7449, -1228, -1104, 3.316),
    ("TurnRight", 0x00100CCF, -3063, -1218, 155, 3.142),
    ("TurnRight", 0x0005667E, 696, -2232, -96, 3.142),
    ("TurnRight", 0x00056680, 1241, -4375, -88, 3.752),
    ("TurnRight", 0x00054DEB, 2017, -4605, -216, 2.182),
    ("TurnRight", 0x00054DEC, 2661, 1609, -1222, 4.237),
    ("TurnRight", 0x00054DED, 2772, 1953, -1222, 5.811),
    ("TurnRight", 0x00054E18, 4327, -4173, -721, 0.593),
    ("TurnRight", 0x00054DFB, 5128, 978, -841, 4.314),
    ("TurnRight", 0x00054DE4, 5737, 1512, -713, 5.885),
    ("TurnRight", 0x00054E12, 8131, -308, -848, 0.175),
    ("TurnRight", 0x00054E02, 8249, -1109, -976, 1.745),
]


# ---------------------------------------------------------------------------
# Data classes
# ---------------------------------------------------------------------------

@dataclass
class Vertex:
    x: float
    y: float
    z: float


@dataclass
class Triangle:
    v0: int
    v1: int
    v2: int
    edge01: int = -1  # neighbor triangle index (-1 = no neighbor / external)
    edge12: int = -1
    edge20: int = -1
    flags: int = 0
    cover_flags: int = 0

    def set_edge(self, idx, val):
        if idx == 0:
            self.edge01 = val
        elif idx == 1:
            self.edge12 = val
        elif idx == 2:
            self.edge20 = val


@dataclass
class EdgeLink:
    """Cross-NAVM edge link (portal between two navmeshes)."""
    link_type: int = 0  # 0 = portal
    navmesh_formid: int = 0
    triangle: int = 0
    edge_index: int = 0  # which edge on the remote triangle


@dataclass
class PieceNavmesh:
    """Complete navmesh data for a single bridge piece (or one arm of a turn)."""
    piece_index: int
    navm_formid: int
    vertices: list  # list of Vertex
    triangles: list  # list of Triangle
    edge_links: list = field(default_factory=list)  # list of EdgeLink
    # Boundary info: maps boundary name -> list of (tri_idx, edge_idx)
    # edge_idx: 0=edge01, 1=edge12, 2=edge20
    boundaries: dict = field(default_factory=dict)
    # World-space position and rotation of this navmesh (used for connectivity)
    center_x: float = 0.0
    center_y: float = 0.0
    center_z: float = 0.0
    rz: float = 0.0


@dataclass
class IslandNavmesh:
    """Parsed island navmesh from Fallout4.esm with raw NVNM data for splicing."""
    navm_formid: int
    vertices: list       # list of (x, y, z) tuples
    triangles: list      # list of dicts with verts, edges, height, unk, flags, cover_flags
    edge_links: list     # list of existing EdgeLink objects
    new_edge_links: list # list of new EdgeLink objects to add
    # Raw NVNM bytes: we splice new edge links in and copy the rest verbatim
    nvnm_pre_edgelinks: bytes   # everything before the edge link count
    nvnm_post_edgelinks: bytes  # everything after the existing edge links
    # Original record metadata
    original_flags: int = 0


# ---------------------------------------------------------------------------
# Geometry helpers
# ---------------------------------------------------------------------------

def to_world(lx, ly, cx, cy, rz):
    """Convert local coords to world coords using rotation angle rz.
    Forward direction vector: (sin(rz), cos(rz)).
    Right direction vector: (cos(rz), -sin(rz)).
    """
    fwd_x = math.sin(rz)
    fwd_y = math.cos(rz)
    right_x = math.cos(rz)
    right_y = -math.sin(rz)
    # lx = forward distance, ly = rightward distance
    wx = cx + lx * fwd_x + ly * right_x
    wy = cy + lx * fwd_y + ly * right_y
    return wx, wy


# ---------------------------------------------------------------------------
# Navmesh generation per piece type
# ---------------------------------------------------------------------------

def generate_straight(piece_idx, navm_fid, cx, cy, cz, rz):
    """Straight/Start/Ramp piece: rectangle, 4 verts, 2 tris.

    Local coordinate system:
    - Forward (+X local) = direction of rz
    - Right (+Y local) = perpendicular right

    Vertices:
        0: front-left  (+HL, -HW)
        1: front-right (+HL, +HW)
        2: back-left   (-HL, -HW)
        3: back-right  (-HL, +HW)

    Triangles (CCW winding for upward-facing normals):
        Tri 0: (0, 2, 1) - front-left, back-left, front-right
            edge01 = v0-v2 (left edge)
            edge12 = v2-v1 (diagonal, shared with tri 1)
            edge20 = v1-v0 (front edge)
        Tri 1: (1, 2, 3) - front-right, back-left, back-right
            edge01 = v1-v2 (diagonal, shared with tri 0)
            edge12 = v2-v3 (back edge)
            edge20 = v3-v1 (right edge)
    """
    HL = HALF_LEN
    HW = HALF_WID

    # Local: (forward_dist, right_dist)
    local_verts = [
        (+HL, -HW),  # 0: front-left
        (+HL, +HW),  # 1: front-right
        (-HL, -HW),  # 2: back-left
        (-HL, +HW),  # 3: back-right
    ]

    verts = []
    for lx, ly in local_verts:
        wx, wy = to_world(lx, ly, cx, cy, rz)
        verts.append(Vertex(wx, wy, cz))

    tris = [
        Triangle(0, 2, 1),  # Tri 0 (CCW)
        Triangle(1, 2, 3),  # Tri 1 (CCW)
    ]

    # Internal adjacency: Tri 0 edge12 (v2-v1) <-> Tri 1 edge01 (v1-v2)
    tris[0].edge12 = 1
    tris[1].edge01 = 0

    # Boundary edges:
    # Front: Tri 0, edge 2 (v1-v0)
    # Back:  Tri 1, edge 1 (v2-v3)
    # Left:  Tri 0, edge 0 (v0-v2)
    # Right: Tri 1, edge 2 (v3-v1)
    boundaries = {
        "front": [(0, 2)],
        "back": [(1, 1)],
    }

    nm = PieceNavmesh(piece_idx, navm_fid, verts, tris, [], boundaries)
    nm.center_x = cx
    nm.center_y = cy
    nm.center_z = cz
    nm.rz = rz
    return nm


def generate_turn_left(piece_idx, navm_fid, cx, cy, cz, rz):
    """TurnLeft: Two rectangles (incoming arm + outgoing arm) as separate NAVMs.

    Returns a list of 2 PieceNavmesh objects with an internal edge link.
    Incoming arm: rectangle along forward axis (back to junction center).
    Outgoing arm: rectangle along -right axis (junction center to exit).
    """
    HL = HALF_LEN

    # Incoming arm: centered between back and junction
    # Center at (-HL/2, 0) relative to piece center, length=HL, width=2*HW
    inc_cx = cx + (-HL / 2) * math.sin(rz)
    inc_cy = cy + (-HL / 2) * math.cos(rz)
    incoming = generate_straight(piece_idx, navm_fid, inc_cx, inc_cy, cz, rz)

    # Outgoing arm: along -right direction (left turn = rz + pi/2)
    # Center at (0, -HL/2) in local coords
    out_rz = rz + math.pi / 2  # rotated 90 degrees left
    right_x = math.cos(rz)
    right_y = -math.sin(rz)
    out_cx = cx + (-HL / 2) * (-right_x)  # -right direction
    out_cy = cy + (-HL / 2) * (-right_y)
    outgoing = generate_straight(piece_idx, navm_fid + 1, out_cx, out_cy, cz, out_rz)

    # Rename boundaries for the turn
    incoming.boundaries = {"back": incoming.boundaries["back"], "junction_front": incoming.boundaries["front"]}
    outgoing.boundaries = {"junction_back": outgoing.boundaries["back"], "left": outgoing.boundaries["front"]}

    # Add edge links between incoming front and outgoing back
    inc_front = incoming.boundaries["junction_front"]
    out_back = outgoing.boundaries["junction_back"]
    for tri_idx, edge_idx in inc_front:
        incoming.triangles[tri_idx].set_edge(edge_idx, -1)
        incoming.edge_links.append(EdgeLink(0, outgoing.navm_formid, out_back[0][0], out_back[0][1]))
    for tri_idx, edge_idx in out_back:
        outgoing.triangles[tri_idx].set_edge(edge_idx, -1)
        outgoing.edge_links.append(EdgeLink(0, incoming.navm_formid, inc_front[0][0], inc_front[0][1]))

    return [incoming, outgoing]


def generate_turn_right(piece_idx, navm_fid, cx, cy, cz, rz):
    """TurnRight: Two rectangles (incoming arm + outgoing arm) as separate NAVMs.

    Returns a list of 2 PieceNavmesh objects with an internal edge link.
    Mirror of TurnLeft: outgoing goes in +right direction (rz - pi/2).
    """
    HL = HALF_LEN

    # Incoming arm: centered between back and junction (same as TurnLeft)
    inc_cx = cx + (-HL / 2) * math.sin(rz)
    inc_cy = cy + (-HL / 2) * math.cos(rz)
    incoming = generate_straight(piece_idx, navm_fid, inc_cx, inc_cy, cz, rz)

    # Outgoing arm: along +right direction (right turn = rz - pi/2)
    out_rz = rz - math.pi / 2  # rotated 90 degrees right
    right_x = math.cos(rz)
    right_y = -math.sin(rz)
    out_cx = cx + (-HL / 2) * right_x   # +right direction
    out_cy = cy + (-HL / 2) * right_y
    outgoing = generate_straight(piece_idx, navm_fid + 1, out_cx, out_cy, cz, out_rz)

    # Rename boundaries for the turn
    incoming.boundaries = {"back": incoming.boundaries["back"], "junction_front": incoming.boundaries["front"]}
    outgoing.boundaries = {"junction_back": outgoing.boundaries["back"], "right": outgoing.boundaries["front"]}

    # Add edge links between incoming front and outgoing back
    inc_front = incoming.boundaries["junction_front"]
    out_back = outgoing.boundaries["junction_back"]
    for tri_idx, edge_idx in inc_front:
        incoming.triangles[tri_idx].set_edge(edge_idx, -1)
        incoming.edge_links.append(EdgeLink(0, outgoing.navm_formid, out_back[0][0], out_back[0][1]))
    for tri_idx, edge_idx in out_back:
        outgoing.triangles[tri_idx].set_edge(edge_idx, -1)
        outgoing.edge_links.append(EdgeLink(0, incoming.navm_formid, inc_front[0][0], inc_front[0][1]))

    return [incoming, outgoing]


# ---------------------------------------------------------------------------
# Connectivity helpers
# ---------------------------------------------------------------------------

def get_navmesh_boundary_center(nm, boundary_name):
    """Get the center point of a boundary edge in world space from a PieceNavmesh.

    Uses the navmesh's own center_x/center_y/rz to compute the boundary
    position, which works correctly for both standalone pieces and turn arms
    (which have shifted centers and rotated orientations).
    """
    if boundary_name == "front":
        return to_world(HALF_LEN, 0, nm.center_x, nm.center_y, nm.rz)
    elif boundary_name == "back":
        return to_world(-HALF_LEN, 0, nm.center_x, nm.center_y, nm.rz)
    elif boundary_name == "left":
        # Left = forward along the outgoing arm's direction (its "front")
        return to_world(HALF_LEN, 0, nm.center_x, nm.center_y, nm.rz)
    elif boundary_name == "right":
        # Right = forward along the outgoing arm's direction (its "front")
        return to_world(HALF_LEN, 0, nm.center_x, nm.center_y, nm.rz)
    return nm.center_x, nm.center_y


def get_external_boundary_names(nm):
    """Return the external (non-junction) boundary names for a navmesh."""
    return [name for name in nm.boundaries if not name.startswith("junction_")]


# ---------------------------------------------------------------------------
# Island navmesh reading from ESM
# ---------------------------------------------------------------------------

VERT_SIZE = 12
TRI_SIZE = 21
EDGE_LINK_SIZE = 11


def read_island_navmeshes(esm_path, formids):
    """Read and parse NVNM data from Fallout4.esm for the given NAVM FormIDs.

    Returns a dict mapping FormID -> IslandNavmesh.
    Uses a splice approach: we parse vertices/triangles/edge_links for analysis,
    but also track raw byte boundaries so we can insert new edge links and copy
    the rest (door links, covers, waypoints, grid) verbatim.
    """
    target_fids = set(formids)
    results = {}
    cell_bytes = struct.pack("<I", TARGET_CELL)

    print(f"  Reading island navmeshes from {esm_path.name}...")

    with open(esm_path, "rb") as f:
        data = f.read()

    pos = 0
    while pos < len(data) - 24:
        if data[pos:pos + 4] != b"NAVM":
            # Skip non-NAVM records quickly
            if data[pos:pos + 4] == b"GRUP":
                pos += 24  # enter group
            elif data[pos:pos + 4] in (b"TES4",):
                ds = struct.unpack_from("<I", data, pos + 4)[0]
                pos += 24 + ds
            else:
                # Try as a record
                if pos + 8 <= len(data):
                    ds = struct.unpack_from("<I", data, pos + 4)[0]
                    if ds < len(data) and pos + 24 + ds <= len(data):
                        pos += 24 + ds
                    else:
                        pos += 1
                else:
                    pos += 1
            continue

        data_size = struct.unpack_from("<I", data, pos + 4)[0]
        flags = struct.unpack_from("<I", data, pos + 8)[0]
        form_id = struct.unpack_from("<I", data, pos + 12)[0]
        record_end = pos + 24 + data_size

        if form_id not in target_fids:
            pos = record_end
            continue

        # Decompress if needed
        compressed = (flags & 0x00040000) != 0
        if compressed:
            decomp_size = struct.unpack_from("<I", data, pos + 24)[0]
            comp_data = data[pos + 28:record_end]
            record_data = zlib.decompress(comp_data)
        else:
            record_data = data[pos + 24:record_end]

        # Find NVNM subrecord
        spos = 0
        while spos < len(record_data) - 6:
            sub_type = record_data[spos:spos + 4]
            sub_size = struct.unpack_from("<H", record_data, spos + 4)[0]
            if sub_type == b"NVNM":
                nvnm = record_data[spos + 6:spos + 6 + sub_size]
                island = _parse_island_nvnm(nvnm, form_id, flags)
                if island is not None:
                    results[form_id] = island
                    target_fids.discard(form_id)
                    print(f"    NAVM 0x{form_id:08X}: {len(island.vertices)} verts, "
                          f"{len(island.triangles)} tris, {len(island.edge_links)} existing edge links")
                break
            spos += 6 + sub_size

        if not target_fids:
            break
        pos = record_end

    if target_fids:
        print(f"  WARNING: Could not find NAVMs: {', '.join(f'0x{f:08X}' for f in target_fids)}")

    return results


def _parse_island_nvnm(nvnm, form_id, flags):
    """Parse an NVNM subrecord and return an IslandNavmesh.

    Tracks raw byte offsets for the splice approach:
    - nvnm_pre_edgelinks: bytes from start of NVNM up to (but not including) edge link count
    - nvnm_post_edgelinks: bytes after the existing edge links to end of NVNM
    """
    off = 0

    # Header: version(4) + CRC(4) + worldspace(4) + cell(4) = 16 bytes
    if len(nvnm) < 20:
        return None
    off = 16

    # Vertices
    num_verts = struct.unpack_from("<I", nvnm, off)[0]
    off += 4
    vertices = []
    for _ in range(num_verts):
        x, y, z = struct.unpack_from("<fff", nvnm, off)
        vertices.append((x, y, z))
        off += VERT_SIZE

    # Triangles
    num_tris = struct.unpack_from("<I", nvnm, off)[0]
    off += 4
    triangles = []
    for _ in range(num_tris):
        v0, v1, v2 = struct.unpack_from("<HHH", nvnm, off)
        e01, e12, e20 = struct.unpack_from("<hhh", nvnm, off + 6)
        height = struct.unpack_from("<f", nvnm, off + 12)[0]
        unk = nvnm[off + 16]
        tri_flags = struct.unpack_from("<H", nvnm, off + 17)[0]
        cover_flags = struct.unpack_from("<H", nvnm, off + 19)[0]
        triangles.append({
            "verts": (v0, v1, v2),
            "edges": (e01, e12, e20),
            "height": height,
            "unk": unk,
            "flags": tri_flags,
            "cover_flags": cover_flags,
        })
        off += TRI_SIZE

    # Mark the byte offset just before edge link count - everything before this
    # is the "pre" section we'll copy verbatim
    edgelink_count_offset = off

    # Edge links
    num_edge_links = struct.unpack_from("<I", nvnm, off)[0]
    off += 4
    edge_links = []
    for _ in range(num_edge_links):
        link_type = struct.unpack_from("<I", nvnm, off)[0]
        link_mesh = struct.unpack_from("<I", nvnm, off + 4)[0]
        link_tri = struct.unpack_from("<h", nvnm, off + 8)[0]
        link_edge = nvnm[off + 10]
        edge_links.append(EdgeLink(
            link_type=link_type,
            navmesh_formid=link_mesh,
            triangle=link_tri,
            edge_index=link_edge,
        ))
        off += EDGE_LINK_SIZE

    # Everything after edge links is the "post" section (door links, covers,
    # waypoints, grid) - copy verbatim
    post_edgelinks_offset = off

    return IslandNavmesh(
        navm_formid=form_id,
        vertices=vertices,
        triangles=triangles,
        edge_links=edge_links,
        new_edge_links=[],
        nvnm_pre_edgelinks=bytes(nvnm[:edgelink_count_offset]),
        nvnm_post_edgelinks=bytes(nvnm[post_edgelinks_offset:]),
        original_flags=flags,
    )


def find_closest_boundary_edge(vertices, triangles, point_x, point_y):
    """Find the closest boundary edge on an island navmesh to a given XY point.

    A boundary edge has edge value == -1 (no internal neighbor).

    Returns (tri_idx, edge_idx) where edge_idx is 0=edge01, 1=edge12, 2=edge20,
    or None if no boundary edge found.
    """
    best_dist = float("inf")
    best_result = None

    for tri_idx, tri in enumerate(triangles):
        v0i, v1i, v2i = tri["verts"]
        edges = tri["edges"]
        tri_verts = [
            vertices[v0i], vertices[v1i], vertices[v2i]
        ]

        # Check each of the 3 edges
        for edge_idx, (va_idx, vb_idx) in enumerate([(0, 1), (1, 2), (2, 0)]):
            edge_val = edges[edge_idx]
            if edge_val != -1:
                continue  # Not a boundary edge

            va = tri_verts[va_idx]
            vb = tri_verts[vb_idx]
            # Midpoint of this edge in XY
            mid_x = (va[0] + vb[0]) / 2.0
            mid_y = (va[1] + vb[1]) / 2.0
            dist = math.sqrt((mid_x - point_x) ** 2 + (mid_y - point_y) ** 2)

            if dist < best_dist:
                best_dist = dist
                best_result = (tri_idx, edge_idx)

    return best_result


# ---------------------------------------------------------------------------
# Build per-piece navmeshes with edge links
# ---------------------------------------------------------------------------

def build_all_navmeshes():
    """Generate NAVM records for all pieces with edge links for adjacency.

    Straight/ramp pieces produce 1 NAVM; turn pieces produce 2 NAVMs
    (incoming arm + outgoing arm).  FormIDs are allocated sequentially.

    Also reads island navmeshes from Fallout4.esm and creates bidirectional
    edge links between bridge endpoints and nearby island boundary edges.

    Returns (flat list of PieceNavmesh, dict of FormID -> IslandNavmesh).
    """
    # Step 1: Generate geometry for each piece with sequential FormID allocation
    all_navmeshes = []         # flat list of all PieceNavmesh objects
    piece_to_navmeshes = {}    # piece_index -> list of PieceNavmesh

    fid_counter = NAVM_BASE_FORMID
    for i, (ptype, _ref_fid, x, y, z, rz) in enumerate(PIECES):
        if ptype in ("Straight", "Start", "RampUp", "RampDown"):
            nm = generate_straight(i, fid_counter, x, y, z, rz)
            all_navmeshes.append(nm)
            piece_to_navmeshes[i] = [nm]
            fid_counter += 1
        elif ptype == "TurnLeft":
            nms = generate_turn_left(i, fid_counter, x, y, z, rz)
            all_navmeshes.extend(nms)
            piece_to_navmeshes[i] = nms
            fid_counter += 2
        elif ptype == "TurnRight":
            nms = generate_turn_right(i, fid_counter, x, y, z, rz)
            all_navmeshes.extend(nms)
            piece_to_navmeshes[i] = nms
            fid_counter += 2
        else:
            raise ValueError(f"Unknown piece type: {ptype}")

    total_verts = sum(len(nm.vertices) for nm in all_navmeshes)
    total_tris = sum(len(nm.triangles) for nm in all_navmeshes)
    print(f"  Generated geometry: {total_verts} vertices, {total_tris} triangles "
          f"across {len(all_navmeshes)} NAVMs ({len(PIECES)} pieces)")

    # Step 2: Find adjacent navmeshes and create edge links
    # Compare all pairs of navmeshes from DIFFERENT pieces.  For each pair,
    # find the closest external boundary pair and link them if close enough.
    total_links = 0
    for a_idx in range(len(all_navmeshes)):
        nm_a = all_navmeshes[a_idx]
        for b_idx in range(a_idx + 1, len(all_navmeshes)):
            nm_b = all_navmeshes[b_idx]

            # Skip navmeshes from the same piece (already internally linked)
            if nm_a.piece_index == nm_b.piece_index:
                continue

            # Quick distance pre-filter on navmesh centers
            dist = math.sqrt(
                (nm_a.center_x - nm_b.center_x) ** 2 +
                (nm_a.center_y - nm_b.center_y) ** 2 +
                (nm_a.center_z - nm_b.center_z) ** 2
            )
            if dist > CONNECT_DIST:
                continue

            # Find which external boundaries face each other
            best_dist = float("inf")
            best_a_bound = None
            best_b_bound = None

            for ba_name in get_external_boundary_names(nm_a):
                bax, bay = get_navmesh_boundary_center(nm_a, ba_name)
                for bb_name in get_external_boundary_names(nm_b):
                    bbx, bby = get_navmesh_boundary_center(nm_b, bb_name)
                    d = math.sqrt((bax - bbx) ** 2 + (bay - bby) ** 2)
                    if d < best_dist:
                        best_dist = d
                        best_a_bound = ba_name
                        best_b_bound = bb_name

            if best_a_bound is None or best_b_bound is None:
                continue

            if best_a_bound not in nm_a.boundaries or best_b_bound not in nm_b.boundaries:
                continue

            # Create edge links in both directions
            for (tri_idx_a, edge_idx_a) in nm_a.boundaries[best_a_bound]:
                nm_a.triangles[tri_idx_a].set_edge(edge_idx_a, -1)

                other_tri_idx = nm_b.boundaries[best_b_bound][0][0]
                other_edge_idx = nm_b.boundaries[best_b_bound][0][1]

                nm_a.edge_links.append(EdgeLink(
                    link_type=0,
                    navmesh_formid=nm_b.navm_formid,
                    triangle=other_tri_idx,
                    edge_index=other_edge_idx,
                ))

            for (tri_idx_b, edge_idx_b) in nm_b.boundaries[best_b_bound]:
                nm_b.triangles[tri_idx_b].set_edge(edge_idx_b, -1)

                other_tri_idx = nm_a.boundaries[best_a_bound][0][0]
                other_edge_idx = nm_a.boundaries[best_a_bound][0][1]

                nm_b.edge_links.append(EdgeLink(
                    link_type=0,
                    navmesh_formid=nm_a.navm_formid,
                    triangle=other_tri_idx,
                    edge_index=other_edge_idx,
                ))

            total_links += 1

    total_edge_links = sum(len(nm.edge_links) for nm in all_navmeshes)
    print(f"  Adjacent pairs: {total_links}")
    print(f"  Total edge links: {total_edge_links}")

    # Step 3: Island-bridge connections
    island_overrides = {}
    if ESM_PATH.exists():
        island_overrides = _build_island_connections(all_navmeshes, piece_to_navmeshes)
    else:
        print(f"  WARNING: ESM not found at {ESM_PATH}, skipping island connections")

    # Step 4: Connectivity check via BFS on piece graph
    # Build adjacency from edge links, mapping piece_index -> set of piece_index
    all_navm_fids = {nm.navm_formid: nm for nm in all_navmeshes}
    adj = {i: set() for i in range(len(PIECES))}
    for nm in all_navmeshes:
        for el in nm.edge_links:
            target_nm = all_navm_fids.get(el.navmesh_formid)
            if target_nm is not None and target_nm.piece_index != nm.piece_index:
                adj[nm.piece_index].add(target_nm.piece_index)

    visited = set()
    components = []
    for start in range(len(PIECES)):
        if start in visited:
            continue
        comp = set()
        queue = [start]
        while queue:
            node = queue.pop()
            if node in visited:
                continue
            visited.add(node)
            comp.add(node)
            for neighbor in adj[node]:
                if neighbor not in visited:
                    queue.append(neighbor)
        components.append(comp)

    print(f"  Connected components: {len(components)}")
    for ci, comp in enumerate(components):
        if len(components) <= 20 or len(comp) > 1:
            print(f"    Component {ci}: {len(comp)} pieces")

    return all_navmeshes, island_overrides


def _build_island_connections(all_navmeshes, piece_to_navmeshes):
    """Read island navmeshes and create bidirectional edge links to/from bridges.

    Returns dict of FormID -> IslandNavmesh with new_edge_links populated.
    Also adds reverse edge links to the bridge PieceNavmesh objects.
    """
    print()
    print("  --- Island-Bridge Connections ---")

    # Read island navmeshes from ESM
    islands = read_island_navmeshes(ESM_PATH, ISLAND_NAVM_FORMIDS)
    if not islands:
        print("  WARNING: No island navmeshes found, skipping connections")
        return {}

    # Process each connection point
    connections_made = 0
    for bridge_idx, endpoint_name, (ep_x, ep_y), island_fid in ISLAND_CONNECTIONS:
        if island_fid not in islands:
            print(f"    SKIP: Island 0x{island_fid:08X} not found for bridge[{bridge_idx}]")
            continue

        island = islands[island_fid]

        # Find the correct navmesh arm for this endpoint
        # For turn pieces, "back" is on the incoming arm, "left"/"right" on the outgoing arm
        bridge_nms = piece_to_navmeshes[bridge_idx]
        bridge_nm = None
        boundary_name = endpoint_name
        for nm in bridge_nms:
            if boundary_name in nm.boundaries:
                bridge_nm = nm
                break

        if bridge_nm is None:
            print(f"    SKIP: Bridge[{bridge_idx}] has no boundary '{boundary_name}' on any arm")
            continue

        bridge_fid = bridge_nm.navm_formid

        # Find closest boundary edge on the island
        result = find_closest_boundary_edge(island.vertices, island.triangles, ep_x, ep_y)
        if result is None:
            print(f"    SKIP: No boundary edge found on island 0x{island_fid:08X} "
                  f"near ({ep_x}, {ep_y})")
            continue

        island_tri_idx, island_edge_idx = result

        bridge_tri_idx, bridge_edge_idx = bridge_nm.boundaries[boundary_name][0]

        # Add edge link: island -> bridge
        island.new_edge_links.append(EdgeLink(
            link_type=0,
            navmesh_formid=bridge_fid,  # Our bridge NAVM (master index 01)
            triangle=bridge_tri_idx,
            edge_index=bridge_edge_idx,
        ))

        # Add reverse edge link: bridge -> island
        # The island FormID uses master index 00 (Fallout4.esm)
        bridge_nm.edge_links.append(EdgeLink(
            link_type=0,
            navmesh_formid=island_fid,  # Island NAVM (master index 00)
            triangle=island_tri_idx,
            edge_index=island_edge_idx,
        ))

        # Make sure the bridge boundary edge is marked as external (-1)
        bridge_tri = bridge_nm.triangles[bridge_tri_idx]
        if bridge_edge_idx == 0:
            bridge_tri.edge01 = -1
        elif bridge_edge_idx == 1:
            bridge_tri.edge12 = -1
        elif bridge_edge_idx == 2:
            bridge_tri.edge20 = -1

        # Report the connection
        island_tri = island.triangles[island_tri_idx]
        v0i, v1i, v2i = island_tri["verts"]
        edge_verts_map = [(v0i, v1i), (v1i, v2i), (v2i, v0i)]
        va_idx, vb_idx = edge_verts_map[island_edge_idx]
        va = island.vertices[va_idx]
        vb = island.vertices[vb_idx]
        mid_x = (va[0] + vb[0]) / 2.0
        mid_y = (va[1] + vb[1]) / 2.0
        dist = math.sqrt((mid_x - ep_x) ** 2 + (mid_y - ep_y) ** 2)

        print(f"    Bridge[{bridge_idx}] {endpoint_name} -> Island 0x{island_fid:08X} "
              f"tri {island_tri_idx} edge {island_edge_idx} "
              f"(edge midpoint: {mid_x:.0f},{mid_y:.0f}, dist={dist:.0f})")
        connections_made += 1

    # Filter to only islands that actually got new edge links
    modified_islands = {fid: isl for fid, isl in islands.items() if isl.new_edge_links}

    print(f"  Island connections made: {connections_made}")
    print(f"  Island NAVMs to override: {len(modified_islands)}")
    for fid, isl in modified_islands.items():
        print(f"    0x{fid:08X}: {len(isl.edge_links)} existing + "
              f"{len(isl.new_edge_links)} new edge links")

    return modified_islands


# ---------------------------------------------------------------------------
# Binary serialization
# ---------------------------------------------------------------------------

def pack_subrecord(tag: bytes, data: bytes) -> bytes:
    """Pack a subrecord: tag(4) + size(2) + data."""
    return tag + struct.pack("<H", len(data)) + data


def pack_record_header(rec_type: bytes, data_size: int, flags: int, formid: int) -> bytes:
    """Pack a record header (24 bytes)."""
    return struct.pack("<4sIIIIHH",
                       rec_type,
                       data_size,
                       flags,
                       formid,
                       0,  # vc1 (timestamp)
                       FO4_FORM_VERSION,
                       0)  # vc2


def pack_group_header(group_size: int, label: bytes, group_type: int) -> bytes:
    """Pack a group header (24 bytes)."""
    return struct.pack("<4sI4sIII",
                       b"GRUP",
                       group_size,
                       label,
                       group_type,
                       0,  # stamp
                       0)  # unknown


def build_nvnm(nm: PieceNavmesh) -> bytes:
    """Build the NVNM subrecord data for one piece's navmesh."""
    buf = bytearray()

    # Header
    buf += struct.pack("<I", NVNM_VERSION)
    buf += struct.pack("<I", NVNM_CRC)
    buf += struct.pack("<I", 0x00000000)  # worldspace (interior = null)
    buf += struct.pack("<I", TARGET_CELL)

    # Vertices
    buf += struct.pack("<I", len(nm.vertices))
    for v in nm.vertices:
        buf += struct.pack("<fff", v.x, v.y, v.z)

    # Triangles (21 bytes each)
    buf += struct.pack("<I", len(nm.triangles))
    for tri in nm.triangles:
        buf += struct.pack("<HHH", tri.v0, tri.v1, tri.v2)
        buf += struct.pack("<hhh", tri.edge01, tri.edge12, tri.edge20)
        avg_z = (nm.vertices[tri.v0].z + nm.vertices[tri.v1].z + nm.vertices[tri.v2].z) / 3.0
        buf += struct.pack("<f", avg_z)
        buf += struct.pack("<B", 0)     # unknown byte
        buf += struct.pack("<H", tri.flags)
        buf += struct.pack("<H", tri.cover_flags)

    # Edge links (11 bytes each)
    buf += struct.pack("<I", len(nm.edge_links))
    for el in nm.edge_links:
        buf += struct.pack("<I", el.link_type)
        buf += struct.pack("<I", el.navmesh_formid)
        buf += struct.pack("<h", el.triangle)
        buf += struct.pack("<B", el.edge_index)

    # Door links (count = 0)
    buf += struct.pack("<I", 0)

    # Cover array (count = 0)
    buf += struct.pack("<I", 0)

    # Cover triangle mappings (count = 0)
    buf += struct.pack("<I", 0)

    # Waypoints (count = 0)
    buf += struct.pack("<I", 0)

    # Navmesh Grid: divisor(u32) + gridSize(2 floats) + bounds min(3f) + bounds max(3f)
    # Then grid cells array (count based on divisor)
    # Compute bounding box from vertices
    min_x = min(v.x for v in nm.vertices)
    max_x = max(v.x for v in nm.vertices)
    min_y = min(v.y for v in nm.vertices)
    max_y = max(v.y for v in nm.vertices)
    min_z = min(v.z for v in nm.vertices)
    max_z = max(v.z for v in nm.vertices)

    # Use divisor=1 for simple grid (1x1 = 1 cell containing all triangles)
    divisor = 1
    grid_x = max_x - min_x if max_x > min_x else 1.0
    grid_y = max_y - min_y if max_y > min_y else 1.0

    buf += struct.pack("<I", divisor)  # Divisor
    buf += struct.pack("<ff", grid_x, grid_y)  # Grid size
    buf += struct.pack("<fff", min_x, min_y, min_z)  # Bounds min
    buf += struct.pack("<fff", max_x, max_y, max_z)  # Bounds max

    # Grid cells: divisor * divisor cells, each cell is an array of triangle indices
    # With divisor=1: just 1 cell containing all triangles
    num_cells = divisor * divisor
    for _ in range(num_cells):
        # Each cell: count(u32) + triangle indices (u16 each)
        num_tris = len(nm.triangles)
        buf += struct.pack("<I", num_tris)
        for ti in range(num_tris):
            buf += struct.pack("<H", ti)

    return bytes(buf)


def build_navm_record(nm: PieceNavmesh) -> bytes:
    """Build a complete NAVM record with compressed data."""
    nvnm_data = build_nvnm(nm)
    nvnm_subrecord = pack_subrecord(b"NVNM", nvnm_data)

    record_data = nvnm_subrecord
    decompressed_size = len(record_data)
    compressed_data = zlib.compress(record_data)

    compressed_payload = struct.pack("<I", decompressed_size) + compressed_data

    header = pack_record_header(
        b"NAVM",
        len(compressed_payload),
        COMPRESSED_FLAG,
        nm.navm_formid,
    )

    return header + compressed_payload


def build_island_nvnm(island: IslandNavmesh) -> bytes:
    """Build a modified NVNM for an island navmesh override.

    Uses the splice approach:
    1. Copy original data up to the edge link count (vertices, triangles, etc.)
    2. Write merged edge links (original + new)
    3. Copy the rest verbatim (door links, covers, waypoints, grid)
    """
    buf = bytearray()

    # Part 1: everything before edge links (header, vertices, triangles)
    buf += island.nvnm_pre_edgelinks

    # Part 2: merged edge links
    all_links = island.edge_links + island.new_edge_links
    buf += struct.pack("<I", len(all_links))
    for el in all_links:
        buf += struct.pack("<I", el.link_type)
        buf += struct.pack("<I", el.navmesh_formid)
        buf += struct.pack("<h", el.triangle)
        buf += struct.pack("<B", el.edge_index)

    # Part 3: everything after edge links (door links, covers, waypoints, grid)
    buf += island.nvnm_post_edgelinks

    return bytes(buf)


def build_island_navm_record(island: IslandNavmesh) -> bytes:
    """Build a complete NAVM override record for an island navmesh."""
    nvnm_data = build_island_nvnm(island)
    nvnm_subrecord = pack_subrecord(b"NVNM", nvnm_data)

    record_data = nvnm_subrecord
    decompressed_size = len(record_data)
    compressed_data = zlib.compress(record_data)

    compressed_payload = struct.pack("<I", decompressed_size) + compressed_data

    header = pack_record_header(
        b"NAVM",
        len(compressed_payload),
        COMPRESSED_FLAG,
        island.navm_formid,  # Same FormID as original (master index 00)
    )

    return header + compressed_payload


def build_cell_record() -> bytes:
    """Build a minimal CELL override record."""
    edid_data = CELL_EDID.encode("ascii") + b"\x00"
    edid_sub = pack_subrecord(b"EDID", edid_data)

    # FULL - display name
    full_sub = pack_subrecord(b"FULL", b"Memories\x00")

    # DATA subrecord - interior cell flags:
    # 0x0001 = Is Interior Cell, 0x0080 = Show Sky, 0x2000 = Player Followers Can't Travel Here
    data_sub = pack_subrecord(b"DATA", struct.pack("<H", 0x2081))

    # LTMP - Lighting Template (NULL = no template, use default)
    ltmp_sub = pack_subrecord(b"LTMP", struct.pack("<I", 0x00000000))

    record_data = edid_sub + full_sub + data_sub + ltmp_sub

    header = pack_record_header(
        b"CELL",
        len(record_data),
        0,
        CELL_OVERRIDE_FORMID,
    )

    return header + record_data


def build_tes4_record(num_new_records: int, next_obj_id: int) -> bytes:
    """Build the TES4 plugin header record."""
    subrecords = bytearray()

    # HEDR: version(float) + numRecords(uint32) + nextObjectID(uint32)
    hedr_data = struct.pack("<fII", 0.95, num_new_records, next_obj_id)
    subrecords += pack_subrecord(b"HEDR", hedr_data)

    # CNAM: author
    subrecords += pack_subrecord(b"CNAM", b"Fallout4Access\x00")

    # MAST: master file name
    subrecords += pack_subrecord(b"MAST", b"Fallout4.esm\x00")

    # DATA: 8 zero bytes (required after each MAST)
    subrecords += pack_subrecord(b"DATA", b"\x00" * 8)

    # INCC: Interior Cell Count
    subrecords += pack_subrecord(b"INCC", struct.pack("<I", 1))

    record_data = bytes(subrecords)

    header = pack_record_header(
        b"TES4",
        len(record_data),
        0,
        0,  # TES4 formID is always 0
    )

    return header + record_data


def build_esp(navmeshes, island_overrides=None):
    """Build the complete ESP file with bridge NAVMs and island overrides."""
    if island_overrides is None:
        island_overrides = {}

    # Total new records: 1 CELL override + N bridge NAVMs + M island NAVM overrides
    num_navm = len(navmeshes) + len(island_overrides)
    num_records = 1 + num_navm

    # Next object ID: one past the highest bridge NAVM FormID
    max_bridge_fid = max(nm.navm_formid for nm in navmeshes) if navmeshes else NAVM_BASE_FORMID
    next_obj_id = max_bridge_fid + 1

    tes4 = build_tes4_record(num_records, next_obj_id)
    cell_record = build_cell_record()

    # Build all NAVM records (bridges first, then island overrides)
    navm_records = bytearray()
    for nm in navmeshes:
        navm_records += build_navm_record(nm)
    for island in island_overrides.values():
        navm_records += build_island_navm_record(island)

    # Build group structure from inside out:
    # GRUP Temporary Children (type 9) contains all NAVMs
    temp_content = bytes(navm_records)
    temp_size = GROUP_HEADER_SIZE + len(temp_content)
    temp_group = pack_group_header(
        temp_size,
        struct.pack("<I", TARGET_CELL),
        9,
    ) + temp_content

    # GRUP Cell Children (type 6)
    cell_children_content = temp_group
    cell_children_size = GROUP_HEADER_SIZE + len(cell_children_content)
    cell_children_group = pack_group_header(
        cell_children_size,
        struct.pack("<I", TARGET_CELL),
        6,
    ) + cell_children_content

    # Interior Cell Sub-Block (type 3)
    sub_block_content = cell_record + cell_children_group
    sub_block_size = GROUP_HEADER_SIZE + len(sub_block_content)
    sub_block_group = pack_group_header(
        sub_block_size,
        struct.pack("<I", SUB_BLOCK_NUM),
        3,
    ) + sub_block_content

    # Interior Cell Block (type 2)
    block_content = sub_block_group
    block_size = GROUP_HEADER_SIZE + len(block_content)
    block_group = pack_group_header(
        block_size,
        struct.pack("<I", BLOCK_NUM),
        2,
    ) + block_content

    # Top CELL group (type 0)
    top_content = block_group
    top_size = GROUP_HEADER_SIZE + len(top_content)
    top_group = pack_group_header(
        top_size,
        b"CELL",
        0,
    ) + top_content

    return tes4 + top_group


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    # Count how many NAVMs we'll generate
    num_turn = sum(1 for ptype, *_ in PIECES if ptype in ("TurnLeft", "TurnRight"))
    num_non_turn = len(PIECES) - num_turn
    total_navms = num_non_turn + num_turn * 2
    max_fid = NAVM_BASE_FORMID + total_navms - 1

    print(f"Generating navmesh ESP for {len(PIECES)} bridge pieces ({total_navms} NAVMs)...")
    print(f"  Target cell: 0x{TARGET_CELL:08X} ({CELL_EDID})")
    print(f"  Output: {OUTPUT_PATH}")
    print(f"  NAVM FormIDs: 0x{NAVM_BASE_FORMID:08X} - 0x{max_fid:08X}")
    print()

    navmeshes, island_overrides = build_all_navmeshes()
    print()

    # Build and write ESP
    esp_data = build_esp(navmeshes, island_overrides)

    OUTPUT_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(OUTPUT_PATH, "wb") as f:
        f.write(esp_data)

    file_size = OUTPUT_PATH.stat().st_size
    print(f"Wrote {file_size:,} bytes to {OUTPUT_PATH}")

    # Summary
    total_bridge_verts = sum(len(nm.vertices) for nm in navmeshes)
    total_bridge_tris = sum(len(nm.triangles) for nm in navmeshes)
    total_bridge_links = sum(len(nm.edge_links) for nm in navmeshes)
    total_island_links = sum(
        len(isl.edge_links) + len(isl.new_edge_links)
        for isl in island_overrides.values()
    )

    print()
    print("=== Summary ===")
    print(f"  Bridge NAVM records: {len(navmeshes)}")
    print(f"  Island NAVM overrides: {len(island_overrides)}")
    print(f"  Total NAVM records: {len(navmeshes) + len(island_overrides)}")
    print(f"  Bridge vertices: {total_bridge_verts}")
    print(f"  Bridge triangles: {total_bridge_tris}")
    print(f"  Bridge edge links: {total_bridge_links}")
    print(f"  Island edge links (total): {total_island_links}")
    print(f"  File size: {file_size:,} bytes")

    # Verification: decompress each NAVM and check
    expected_navm_count = len(navmeshes) + len(island_overrides)
    with open(OUTPUT_PATH, "rb") as f:
        data = f.read()

    print()
    print("=== Verification ===")
    pos = 0
    navm_count = 0
    while pos < len(data) - 24:
        if data[pos:pos + 4] == b"NAVM":
            ds = struct.unpack_from("<I", data, pos + 4)[0]
            flags = struct.unpack_from("<I", data, pos + 8)[0]
            fid = struct.unpack_from("<I", data, pos + 12)[0]
            is_island = fid in ISLAND_NAVM_FORMIDS
            label = "island" if is_island else "bridge"
            if flags & COMPRESSED_FLAG:
                decomp_size = struct.unpack_from("<I", data, pos + 24)[0]
                comp = data[pos + 28:pos + 24 + ds]
                try:
                    decomp = zlib.decompress(comp)
                    assert len(decomp) == decomp_size, f"Size mismatch for NAVM 0x{fid:08X}"
                    # Parse NVNM subrecord
                    nvnm_tag = decomp[0:4]
                    nvnm_size = struct.unpack_from("<H", decomp, 4)[0]
                    nvnm = decomp[6:6 + nvnm_size]
                    nv = struct.unpack_from("<I", nvnm, 16)[0]
                    nt_off = 20 + nv * 12
                    nt = struct.unpack_from("<I", nvnm, nt_off)[0]
                    ne_off = nt_off + 4 + nt * 21
                    ne = struct.unpack_from("<I", nvnm, ne_off)[0]
                    if is_island or navm_count < 3:
                        print(f"  NAVM 0x{fid:08X} ({label}): {nv} verts, {nt} tris, {ne} edge links - OK")
                    elif navm_count == 3:
                        print(f"  ... (skipping remaining bridge NAVMs)")
                    navm_count += 1
                except Exception as e:
                    print(f"  NAVM 0x{fid:08X} ({label}): VERIFY FAILED - {e}")
            pos += 24 + ds
        elif data[pos:pos + 4] == b"GRUP":
            pos += 24  # skip group header, parse contents
        elif data[pos:pos + 4] in (b"TES4", b"CELL"):
            ds = struct.unpack_from("<I", data, pos + 4)[0]
            pos += 24 + ds
        else:
            pos += 1

    if navm_count == expected_navm_count:
        print(f"  All {navm_count} NAVM records verified OK")
    else:
        print(f"  WARNING: Found {navm_count} NAVM records, expected {expected_navm_count}")

    print()
    print("Done.")


if __name__ == "__main__":
    main()
