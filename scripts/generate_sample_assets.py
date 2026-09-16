"""Generates the sample assets of the sandbox project and the test data of the importers.

Everything is procedural, so the repository needs no third-party art. Run from any directory:

    python scripts/generate_sample_assets.py

The .dvxmeta files next to the sandbox assets are not written here: the asset database creates
them on the first import, and they are versioned so that identifiers stay stable.
"""

import base64
import json
import math
import random
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SANDBOX_ASSETS = ROOT / "apps" / "sandbox" / "project" / "assets"
TEST_DATA = ROOT / "tests" / "data"


def png_bytes(width, height, pixel):
    """Encodes an RGBA8 image; pixel(x, y) returns a tuple of four 0-255 integers."""
    rows = bytearray()
    for y in range(height):
        rows.append(0)  # no filter
        for x in range(width):
            rows.extend(pixel(x, y))
    chunks = []

    def chunk(kind, data):
        chunks.append(struct.pack(">I", len(data)) + kind + data +
                      struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF))

    chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
    chunk(b"IDAT", zlib.compress(bytes(rows), 9))
    chunk(b"IEND", b"")
    return b"\x89PNG\r\n\x1a\n" + b"".join(chunks)


def write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        path.write_text(data, encoding="utf-8", newline="\n")
    else:
        path.write_bytes(data)
    print("wrote", path.relative_to(ROOT))


def clamp_byte(value):
    return max(0, min(255, int(round(value))))


# ---------------------------------------------------------------------------------------------
# Textures

def checker_pixel(cells, size, light, dark):
    cell = size // cells

    def pixel(x, y):
        return light if ((x // cell) + (y // cell)) % 2 == 0 else dark
    return pixel


def crate_albedo(size):
    rng = random.Random(7)
    grain = [rng.uniform(-14, 14) for _ in range(size)]
    border = size // 10

    def pixel(x, y):
        # Planks run horizontally inside a darker frame.
        in_frame = x < border or y < border or x >= size - border or y >= size - border
        plank = (y // (size // 5)) % 2
        base = (122, 84, 48) if plank else (134, 94, 55)
        if in_frame:
            base = (86, 58, 34)
        shade = grain[(x * 3 + y // 2) % size] + 6 * math.sin(x * 0.21 + plank * 2.0)
        seam = (y % (size // 5)) < 2 and not in_frame
        factor = 0.55 if seam else 1.0
        return (clamp_byte((base[0] + shade) * factor), clamp_byte((base[1] + shade * 0.8) * factor),
                clamp_byte((base[2] + shade * 0.5) * factor), 255)
    return pixel


def crate_normal(size):
    border = size // 10

    def pixel(x, y):
        # The frame is raised: its inner edges tilt the normal outwards.
        nx, ny = 0.0, 0.0
        if abs(x - border) < 3 and border <= y < size - border:
            nx = -0.6
        elif abs(x - (size - border)) < 3 and border <= y < size - border:
            nx = 0.6
        if abs(y - border) < 3 and border <= x < size - border:
            ny = 0.6
        elif abs(y - (size - border)) < 3 and border <= x < size - border:
            ny = -0.6
        nz = math.sqrt(max(0.0, 1.0 - nx * nx - ny * ny))
        return (clamp_byte((nx * 0.5 + 0.5) * 255), clamp_byte((ny * 0.5 + 0.5) * 255),
                clamp_byte((nz * 0.5 + 0.5) * 255), 255)
    return pixel


# ---------------------------------------------------------------------------------------------
# glTF

class GltfBuilder:
    """Accumulates binary buffer views and accessors for one buffer."""

    def __init__(self):
        self.buffer = bytearray()
        self.views = []
        self.accessors = []

    def _view(self, data, target=None):
        while len(self.buffer) % 4:
            self.buffer.append(0)
        view = {"buffer": 0, "byteOffset": len(self.buffer), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        self.buffer.extend(data)
        self.views.append(view)
        return len(self.views) - 1

    def floats(self, values, kind):
        width = {"VEC2": 2, "VEC3": 3}[kind]
        data = b"".join(struct.pack("<f", v) for item in values for v in item)
        accessor = {"bufferView": self._view(data, 34962), "componentType": 5126,
                    "count": len(values), "type": kind}
        if kind == "VEC3":
            accessor["min"] = [min(item[i] for item in values) for i in range(width)]
            accessor["max"] = [max(item[i] for item in values) for i in range(width)]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def indices(self, values):
        data = b"".join(struct.pack("<I", v) for v in values)
        self.accessors.append({"bufferView": self._view(data, 34963), "componentType": 5125,
                               "count": len(values), "type": "SCALAR"})
        return len(self.accessors) - 1

    def image(self, data):
        return self._view(data)

    def primitive(self, mesh, material):
        positions, normals, uvs, indices = mesh
        return {
            "attributes": {"POSITION": self.floats(positions, "VEC3"),
                           "NORMAL": self.floats(normals, "VEC3"),
                           "TEXCOORD_0": self.floats(uvs, "VEC2")},
            "indices": self.indices(indices),
            "material": material,
        }


def box(half_x, half_y, half_z):
    faces = [((1, 0, 0), (0, 0, -1), (0, 1, 0)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)),
             ((0, 1, 0), (1, 0, 0), (0, 0, -1)), ((0, -1, 0), (1, 0, 0), (0, 0, 1)),
             ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (-1, 0, 0), (0, 1, 0))]
    half = (half_x, half_y, half_z)
    positions, normals, uvs, indices = [], [], [], []
    for normal, right, up in faces:
        base = len(positions)
        for u, v in ((0, 1), (1, 1), (1, 0), (0, 0)):
            sx, sy = u * 2 - 1, (1 - v) * 2 - 1
            positions.append(tuple((normal[i] + right[i] * sx + up[i] * sy) * half[i]
                                   for i in range(3)))
            normals.append(normal)
            uvs.append((u, v))
        indices += [base, base + 1, base + 2, base, base + 2, base + 3]
    return positions, normals, uvs, indices


def sphere(radius, center, segments=24, rings=12):
    positions, normals, uvs, indices = [], [], [], []
    for ring in range(rings + 1):
        theta = math.pi * ring / rings
        for segment in range(segments + 1):
            phi = 2 * math.pi * segment / segments
            n = (math.sin(theta) * math.cos(phi), math.cos(theta), -math.sin(theta) * math.sin(phi))
            normals.append(n)
            positions.append(tuple(center[i] + n[i] * radius for i in range(3)))
            uvs.append((segment / segments, ring / rings))
    for ring in range(rings):
        for segment in range(segments):
            a = ring * (segments + 1) + segment
            b = a + segments + 1
            indices += [a, b, a + 1, a + 1, b, b + 1]
    return positions, normals, uvs, indices


def finish(builder, document, path, embed_buffer=False, uri=None):
    while len(builder.buffer) % 4:
        builder.buffer.append(0)
    document["asset"] = {"version": "2.0", "generator": "Devex sample asset generator"}
    document["bufferViews"] = builder.views
    document["accessors"] = builder.accessors
    if embed_buffer:
        encoded = base64.b64encode(bytes(builder.buffer)).decode("ascii")
        document["buffers"] = [{"byteLength": len(builder.buffer),
                                "uri": "data:application/octet-stream;base64," + encoded}]
    else:
        document["buffers"] = [{"byteLength": len(builder.buffer), "uri": uri}]
        write(path.parent / uri, bytes(builder.buffer))
    write(path, json.dumps(document, indent=2) + "\n")


def glb(document, builder, path):
    while len(builder.buffer) % 4:
        builder.buffer.append(0)
    document["asset"] = {"version": "2.0", "generator": "Devex sample asset generator"}
    document["bufferViews"] = builder.views
    document["accessors"] = builder.accessors
    document["buffers"] = [{"byteLength": len(builder.buffer)}]
    text = json.dumps(document, separators=(",", ":")).encode("utf-8")
    while len(text) % 4:
        text += b" "
    body = bytes(builder.buffer)
    total = 12 + 8 + len(text) + 8 + len(body)
    data = (struct.pack("<III", 0x46546C67, 2, total) + struct.pack("<II", len(text), 0x4E4F534A) +
            text + struct.pack("<II", len(body), 0x004E4942) + body)
    write(path, data)


def crate(path):
    """A crate with external buffer and textures: base color and normal map."""
    builder = GltfBuilder()
    albedo = "crate_albedo.png"
    normal = "crate_normal.png"
    write(path.parent / albedo, png_bytes(256, 256, crate_albedo(256)))
    write(path.parent / normal, png_bytes(256, 256, crate_normal(256)))
    document = {
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Crate", "mesh": 0, "translation": [0, 0.5, 0]}],
        "meshes": [{"name": "Crate", "primitives": [builder.primitive(box(0.5, 0.5, 0.5), 0)]}],
        "materials": [{
            "name": "Wood",
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0,
                                     "roughnessFactor": 0.8},
            "normalTexture": {"index": 1},
        }],
        "textures": [{"source": 0}, {"source": 1}],
        "images": [{"uri": albedo}, {"uri": normal}],
    }
    finish(builder, document, path, uri="crate.bin")


def beacon(path):
    """A binary glTF with two materials in one mesh, one of them emissive, and a child node."""
    builder = GltfBuilder()
    stripes = builder.image(png_bytes(64, 64, lambda x, y: (230, 230, 235, 255)
                                      if ((x + y) // 16) % 2 == 0 else (40, 44, 52, 255)))
    document = {
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [
            {"name": "Beacon", "children": [1], "mesh": 0},
            {"name": "Cap", "mesh": 1, "translation": [0, 1.6, 0], "scale": [0.6, 0.15, 0.6]},
        ],
        "meshes": [
            {"name": "Pillar", "primitives": [
                builder.primitive(box(0.25, 0.7, 0.25), 0),
                builder.primitive(sphere(0.3, (0, 1.2, 0)), 1),
            ]},
            {"name": "Cap", "primitives": [builder.primitive(box(0.5, 0.5, 0.5), 0)]},
        ],
        "materials": [
            {"name": "Stripes", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0},
                                                          "metallicFactor": 0.2}},
            {"name": "Light", "pbrMetallicRoughness": {"baseColorFactor": [1, 0.55, 0.15, 1]},
             "emissiveFactor": [1, 0.45, 0.1]},
        ],
        "textures": [{"source": 0}],
        "images": [{"bufferView": stripes, "mimeType": "image/png", "name": "Stripes"}],
    }
    glb(document, builder, path)


def textured_quad(path):
    """Test data: a quad with an embedded buffer and an external base color texture."""
    builder = GltfBuilder()
    write(path.parent / "quad_color.png", png_bytes(8, 8, checker_pixel(2, 8, (255, 0, 0, 255),
                                                                        (0, 0, 255, 255))))
    quad = ([(-1, 0, -1), (1, 0, -1), (1, 0, 1), (-1, 0, 1)], [(0, 1, 0)] * 4,
            [(0, 0), (1, 0), (1, 1), (0, 1)], [0, 2, 1, 0, 3, 2])
    document = {
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Root", "children": [1]}, {"name": "Quad", "mesh": 0, "scale": [2, 2, 2]}],
        "meshes": [{"name": "Quad", "primitives": [builder.primitive(quad, 0)]}],
        "materials": [{"name": "Painted", "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}},
                       "alphaMode": "MASK", "doubleSided": True}],
        "textures": [{"source": 0}],
        "images": [{"uri": "quad_color.png"}],
    }
    finish(builder, document, path, embed_buffer=True)


def main():
    # Sandbox project
    write(SANDBOX_ASSETS / "textures" / "checker.png",
          png_bytes(256, 256, checker_pixel(16, 256, (196, 200, 206, 255), (150, 156, 166, 255))))
    crate(SANDBOX_ASSETS / "models" / "crate" / "crate.gltf")
    beacon(SANDBOX_ASSETS / "models" / "beacon.glb")

    # Importer tests
    write(TEST_DATA / "checker.png",
          png_bytes(64, 32, checker_pixel(4, 64, (255, 255, 255, 255), (0, 0, 0, 255))))
    textured_quad(TEST_DATA / "textured" / "quad.gltf")


if __name__ == "__main__":
    main()
