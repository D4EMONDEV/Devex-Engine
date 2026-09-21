"""Generates the sample assets of the sandbox project and the test data of the importers.

Everything is procedural, so the repository needs no third-party art or sound. Run from any
directory:

    python scripts/generate_sample_assets.py [--audio-only]

Sounds are synthesized as WAV files; the compressed ones (Ogg Vorbis, MP3, FLAC) are encoded with
ffmpeg when it is on the PATH, and left as they are otherwise.

The .dvxmeta files next to the sandbox assets are not written here: the asset database creates
them on the first import, and they are versioned so that identifiers stay stable.
"""

import base64
import json
import math
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SANDBOX_ASSETS = ROOT / "samples" / "sandbox" / "assets"
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

    def values(self, items, kind, component_type=5126, target=None, bounds=False):
        """Any other accessor: skinning attributes, inverse bind matrices, animation keys."""
        formats = {5126: "<f", 5123: "<H"}
        data = b"".join(struct.pack(formats[component_type], v) for item in items for v in item)
        accessor = {"bufferView": self._view(data, target), "componentType": component_type,
                    "count": len(items), "type": kind}
        if bounds:
            accessor["min"] = [min(item[i] for item in items) for i in range(len(items[0]))]
            accessor["max"] = [max(item[i] for item in items) for i in range(len(items[0]))]
        self.accessors.append(accessor)
        return len(self.accessors) - 1

    def times(self, seconds):
        return self.values([(value,) for value in seconds], "SCALAR", bounds=True)

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


def hdr_bytes(width, height, pixel):
    """Encodes a Radiance .hdr image without run-length compression; pixel(x, y) returns linear RGB."""
    header = "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y {} +X {}\n".format(height, width).encode("ascii")

    def encode_channel(values):
        # Runs of four or more equal bytes, and literal spans of at most 128 bytes otherwise.
        out = bytearray()
        index = 0
        while index < len(values):
            run = 1
            while index + run < len(values) and run < 127 and values[index + run] == values[index]:
                run += 1
            if run >= 4:
                out.extend((128 + run, values[index]))
                index += run
                continue
            start = index
            index += 1
            while index < len(values) and index - start < 128:
                ahead = 1
                while index + ahead < len(values) and ahead < 4 and values[index + ahead] == values[index]:
                    ahead += 1
                if ahead >= 4:
                    break
                index += 1
            out.append(index - start)
            out.extend(values[start:index])
        return out

    data = bytearray()
    for y in range(height):
        channels = [bytearray(), bytearray(), bytearray(), bytearray()]
        for x in range(width):
            r, g, b = pixel(x, y)
            brightest = max(r, g, b)
            rgbe = (0, 0, 0, 0)
            if brightest >= 1e-32:
                mantissa, exponent = math.frexp(brightest)
                scale = mantissa * 256.0 / brightest
                rgbe = (int(r * scale), int(g * scale), int(b * scale), exponent + 128)
            for channel, value in zip(channels, rgbe):
                channel.append(value)
        data.extend((2, 2, width >> 8, width & 0xFF))
        for channel in channels:
            data.extend(encode_channel(channel))
    return header + bytes(data)


def sky(width, height, sun_direction):
    """A clear daylight sky: a gradient from the horizon to the zenith, a glow around the sun and
    a dim ground. The sun itself is a directional light in the scene, so it is not in the image."""
    sun = [c / math.sqrt(sum(v * v for v in sun_direction)) for c in sun_direction]

    def pixel(x, y):
        # Inverse of the engine's equirectangular mapping: the image center looks along -Z.
        phi = (x + 0.5) / width * 2.0 * math.pi - math.pi
        theta = (y + 0.5) / height * math.pi
        direction = (math.sin(theta) * math.sin(phi), math.cos(theta), -math.sin(theta) * math.cos(phi))
        up = direction[1]
        if up < 0.0:
            fade = min(1.0, -up * 8.0)
            horizon = (0.55, 0.6, 0.65)
            ground = (0.3, 0.28, 0.25)
            return tuple(h * (1.0 - fade) + g * fade for h, g in zip(horizon, ground))
        zenith = (0.06, 0.18, 0.7)
        horizon = (0.6, 0.72, 0.95)
        t = math.pow(up, 0.5)
        color = [h * (1.0 - t) + z * t for h, z in zip(horizon, zenith)]
        glow = max(0.0, sum(d * s for d, s in zip(direction, sun)))
        color = [c + 0.6 * math.pow(glow, 8.0) * w for c, w in zip(color, (1.0, 0.9, 0.7))]
        return tuple(color)
    return pixel


# ---------------------------------------------------------------------------------------------
# Sounds

def wav_bytes(channels, rate):
    """Encodes 16-bit PCM; channels holds one list of samples in [-1, 1] per channel."""
    frames = len(channels[0])
    data = bytearray()
    for frame in range(frames):
        for channel in channels:
            data += struct.pack("<h", max(-32767, min(32767, int(round(channel[frame] * 32767)))))
    header = b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVE"
    header += b"fmt " + struct.pack("<IHHIIHH", 16, 1, len(channels), rate, rate * 2 * len(channels), 2 * len(channels), 16)
    return header + b"data" + struct.pack("<I", len(data)) + bytes(data)


def envelope(t, attack, length):
    """Rises over the attack, then falls to zero at the end."""
    if t < attack:
        return t / attack
    return max(0.0, 1.0 - (t - attack) / max(length - attack, 1e-6))


def whoosh(rate=44100, length=0.35):
    """A throw: noise whose brightness rises and falls."""
    rng = random.Random(11)
    samples, low = [], 0.0
    for index in range(int(rate * length)):
        t = index / rate
        brightness = 0.02 + 0.25 * math.sin(math.pi * t / length)
        low += (rng.uniform(-1.0, 1.0) - low) * brightness
        samples.append(low * 1.3 * envelope(t, 0.06, length))
    return samples


def ding(rate=44100, length=0.9):
    """A target hit: a bell, partials decaying at their own speed."""
    partials = [(880.0, 0.5, 5.0), (1320.0, 0.25, 7.0), (2217.0, 0.12, 11.0), (3520.0, 0.05, 16.0)]
    samples = []
    for index in range(int(rate * length)):
        t = index / rate
        value = sum(amplitude * math.exp(-decay * t) * math.sin(2 * math.pi * frequency * t)
                    for frequency, amplitude, decay in partials)
        samples.append(value * min(1.0, t / 0.002))
    return samples


def slide(rate=44100, length=1.1):
    """A sliding door: a low rumble with a motor hum."""
    rng = random.Random(23)
    samples, brown = [], 0.0
    for index in range(int(rate * length)):
        t = index / rate
        brown = max(-1.0, min(1.0, brown + rng.uniform(-1.0, 1.0) * 0.04)) * 0.995
        hum = 0.25 * math.sin(2 * math.pi * 55 * t) + 0.12 * math.sin(2 * math.pi * 110 * t)
        fade = min(1.0, t / 0.08, (length - t) / 0.25)
        samples.append((brown * 0.6 + hum) * max(fade, 0.0) * 0.8)
    return samples


def hum(rate=44100, length=2.0):
    """A loop: whole periods of every partial and of the tremolo, so that it joins seamlessly."""
    samples = []
    for index in range(int(rate * length)):
        t = index / rate
        tremolo = 0.75 + 0.25 * math.sin(2 * math.pi * 1.0 * t)
        value = 0.35 * math.sin(2 * math.pi * 110 * t) + 0.18 * math.sin(2 * math.pi * 220 * t) + \
            0.06 * math.sin(2 * math.pi * 330 * t)
        samples.append(value * tremolo)
    return samples


def ambience(rate=22050, length=16.0):
    """Stereo music that loops: pads of four chords, crossfaded around the loop, and soft plucks."""
    chords = [(261.63, 329.63, 392.00), (220.00, 261.63, 329.63), (174.61, 220.00, 261.63), (196.00, 246.94, 293.66)]
    arpeggio = [0, 1, 2, 1]
    span = length / len(chords)
    left, right = [], []
    for index in range(int(rate * length)):
        t = index / rate
        pad_left = pad_right = 0.0
        for number, chord in enumerate(chords):
            # A raised cosine centered on its chord, wrapping around the loop.
            distance = (t - (number + 0.5) * span + length / 2) % length - length / 2
            weight = math.cos(math.pi * distance / (2 * span)) ** 2 if abs(distance) < span else 0.0
            for frequency in chord:
                pad_left += weight * math.sin(2 * math.pi * frequency * t)
                pad_right += weight * math.sin(2 * math.pi * frequency * 1.003 * t)
        chord = chords[int(t // span) % len(chords)]
        step = t % 0.5
        note = chord[arpeggio[int(t / 0.5) % len(arpeggio)]] * 2
        pluck = 0.22 * math.exp(-6.0 * step) * math.sin(2 * math.pi * note * step) * min(1.0, step / 0.004)
        left.append(0.09 * pad_left + pluck * 0.8)
        right.append(0.09 * pad_right + pluck * 1.0)
    return [left, right]


def tone(rate=22050, length=0.25, frequency=440.0):
    """A sine at half amplitude, as the tests expect it."""
    return [0.5 * math.sin(2 * math.pi * frequency * index / rate) for index in range(int(rate * length))]


def encode(wav, path, codec_arguments):
    """Encodes WAV bytes with ffmpeg, when it is available."""
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg is None:
        print("ffmpeg is not on the PATH:", path.relative_to(ROOT), "is left as it is")
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as directory:
        source = Path(directory) / "source.wav"
        source.write_bytes(wav)
        subprocess.run([ffmpeg, "-y", "-loglevel", "error", "-i", str(source), *codec_arguments,
                        "-map_metadata", "-1", str(path)], check=True)
    print("wrote", path.relative_to(ROOT))


def audio_assets():
    sounds = SANDBOX_ASSETS / "audio"
    write(sounds / "throw.wav", wav_bytes([whoosh()], 44100))
    write(sounds / "hit.wav", wav_bytes([ding()], 44100))
    write(sounds / "door.wav", wav_bytes([slide()], 44100))
    write(sounds / "hum.wav", wav_bytes([hum()], 44100))
    encode(wav_bytes(ambience(), 22050), sounds / "ambience.ogg", ["-c:a", "libvorbis", "-q:a", "3"])

    # Importer tests: one tone in every format.
    tone_wav = wav_bytes([tone()], 22050)
    write(TEST_DATA / "audio" / "tone.wav", tone_wav)
    encode(tone_wav, TEST_DATA / "audio" / "tone.ogg", ["-c:a", "libvorbis", "-q:a", "3"])
    encode(tone_wav, TEST_DATA / "audio" / "tone.mp3", ["-c:a", "libmp3lame", "-b:a", "64k"])
    encode(tone_wav, TEST_DATA / "audio" / "tone.flac", ["-c:a", "flac"])


# ---------------------------------------------------------------------------------------------
# A rigged character

# Joints of the robot: name, parent, translation from the parent, and the box of its body part,
# given as the half extents and the center, both in the space of the joint at bind time.
ROBOT_JOINTS = [
    ("Hips", None, (0.0, 0.95, 0.0), (0.16, 0.1, 0.11), (0.0, 0.0, 0.0)),
    ("Spine", "Hips", (0.0, 0.1, 0.0), (0.19, 0.24, 0.13), (0.0, 0.24, 0.0)),
    ("Head", "Spine", (0.0, 0.52, 0.0), (0.13, 0.13, 0.13), (0.0, 0.08, 0.0)),
    ("ArmLeft", "Spine", (0.25, 0.4, 0.0), (0.06, 0.17, 0.06), (0.0, -0.15, 0.0)),
    ("ForearmLeft", "ArmLeft", (0.0, -0.32, 0.0), (0.05, 0.16, 0.05), (0.0, -0.14, 0.0)),
    ("ArmRight", "Spine", (-0.25, 0.4, 0.0), (0.06, 0.17, 0.06), (0.0, -0.15, 0.0)),
    ("ForearmRight", "ArmRight", (0.0, -0.32, 0.0), (0.05, 0.16, 0.05), (0.0, -0.14, 0.0)),
    ("LegLeft", "Hips", (0.1, -0.12, 0.0), (0.07, 0.21, 0.07), (0.0, -0.19, 0.0)),
    ("ShinLeft", "LegLeft", (0.0, -0.42, 0.0), (0.06, 0.21, 0.09), (0.0, -0.19, 0.02)),
    ("LegRight", "Hips", (-0.1, -0.12, 0.0), (0.07, 0.21, 0.07), (0.0, -0.19, 0.0)),
    ("ShinRight", "LegRight", (0.0, -0.42, 0.0), (0.06, 0.21, 0.09), (0.0, -0.19, 0.02)),
]

# The parts drawn with the dark material; the others take the painted one.
ROBOT_TRIM = {"ForearmLeft", "ForearmRight", "ShinLeft", "ShinRight", "Hips"}


def robot_bind_positions():
    """Where each joint stands in the model when nothing is animated."""
    positions = {}
    for name, parent, translation, _half, _center in ROBOT_JOINTS:
        base = positions[parent] if parent else (0.0, 0.0, 0.0)
        positions[name] = tuple(base[axis] + translation[axis] for axis in range(3))
    return positions


def quaternion(axis, angle):
    """x, y, z, w, as glTF stores rotations."""
    half = angle / 2
    sine = math.sin(half)
    return (axis[0] * sine, axis[1] * sine, axis[2] * sine, math.cos(half))


def robot_skin_parts(positions):
    """The boxes of the body, split by material, with the joint each vertex follows."""
    parts = {False: ([], [], [], [], [], []), True: ([], [], [], [], [], [])}
    for index, (name, _parent, _translation, half, center) in enumerate(ROBOT_JOINTS):
        origin = positions[name]
        box_positions, box_normals, box_uvs, box_indices = box(*half)
        target = parts[name in ROBOT_TRIM]
        offset = len(target[0])
        for vertex in box_positions:
            target[0].append(tuple(origin[axis] + center[axis] + vertex[axis] for axis in range(3)))
        target[1].extend(box_normals)
        target[2].extend(box_uvs)
        target[3].extend(offset + value for value in box_indices)
        target[4].extend([(index, 0, 0, 0)] * len(box_positions))
        target[5].extend([(1.0, 0.0, 0.0, 0.0)] * len(box_positions))
    return parts


def robot_animation(name, duration, tracks, builder):
    """One animation: tracks map a joint to its rotation or translation keys."""
    channels, samplers = [], []
    for joint, path, keys in tracks:
        times = [time for time, _value in keys]
        values = [value for _time, value in keys]
        kind = "VEC4" if path == "rotation" else "VEC3"
        samplers.append({"input": builder.times(times),
                         "output": builder.values(values, kind),
                         "interpolation": "LINEAR"})
        channels.append({"sampler": len(samplers) - 1,
                         "target": {"node": 1 + [j[0] for j in ROBOT_JOINTS].index(joint),
                                    "path": path}})
    return {"name": name, "duration": duration, "channels": channels, "samplers": samplers}


def robot_idle():
    """Breathing on the spot: the hips rise and fall, the arms sway a little."""
    keys = [0.0, 0.5, 1.0, 1.5, 2.0]
    hips = [(time, (0.0, 0.95 + 0.02 * math.sin(2 * math.pi * time / 2.0), 0.0)) for time in keys]
    sway = []
    for time in keys:
        angle = math.radians(5.0) * math.sin(2 * math.pi * time / 2.0)
        sway.append((time, quaternion((1.0, 0.0, 0.0), angle)))
    head = [(time, quaternion((0.0, 1.0, 0.0), math.radians(6.0) * math.sin(math.pi * time / 2.0)))
            for time in keys]
    return [("Hips", "translation", hips), ("ArmLeft", "rotation", sway),
            ("ArmRight", "rotation", sway), ("Head", "rotation", head)]


def robot_walk():
    """A one-second stride: legs and arms swing in opposition, the hips bob twice."""
    steps = 8
    keys = [step / steps for step in range(steps + 1)]
    tracks = []

    def swing(amplitude, phase, axis=(1.0, 0.0, 0.0)):
        return [(time, quaternion(axis, math.radians(amplitude) * math.sin(2 * math.pi * (time + phase))))
                for time in keys]

    def bend(amplitude, phase):
        # Knees only bend one way, so the sine is folded to stay negative.
        return [(time, quaternion((1.0, 0.0, 0.0),
                                  -math.radians(amplitude) * max(0.0, math.sin(2 * math.pi * (time + phase)))))
                for time in keys]

    tracks.append(("LegLeft", "rotation", swing(28.0, 0.0)))
    tracks.append(("LegRight", "rotation", swing(28.0, 0.5)))
    tracks.append(("ShinLeft", "rotation", bend(35.0, 0.25)))
    tracks.append(("ShinRight", "rotation", bend(35.0, 0.75)))
    tracks.append(("ArmLeft", "rotation", swing(22.0, 0.5)))
    tracks.append(("ArmRight", "rotation", swing(22.0, 0.0)))
    tracks.append(("ForearmLeft", "rotation", bend(18.0, 0.5)))
    tracks.append(("ForearmRight", "rotation", bend(18.0, 0.0)))
    hips = [(time, (0.0, 0.95 + 0.03 * abs(math.sin(2 * math.pi * time)), 0.0)) for time in keys]
    tracks.append(("Hips", "translation", hips))
    tracks.append(("Spine", "rotation", swing(4.0, 0.25, (0.0, 1.0, 0.0))))
    return tracks


def robot_wave():
    """The right arm rises and the forearm waves."""
    keys = [step / 8 for step in range(13)]
    arm, forearm = [], []
    for time in keys:
        rise = min(1.0, time / 0.3) * (1.0 if time < 1.2 else max(0.0, (1.5 - time) / 0.3))
        arm.append((time, quaternion((0.0, 0.0, 1.0), math.radians(-140.0) * rise)))
        wave = math.radians(22.0) * math.sin(2 * math.pi * 2.0 * time) * rise
        forearm.append((time, quaternion((0.0, 0.0, 1.0), wave)))
    head = [(time, quaternion((0.0, 1.0, 0.0), math.radians(-10.0) * min(1.0, time / 0.4)))
            for time in keys]
    return [("ArmRight", "rotation", arm), ("ForearmRight", "rotation", forearm),
            ("Head", "rotation", head)]


def robot(path):
    """A rigged robot as a .glb: boxes bound to eleven joints, with idle, walk and wave."""
    builder = GltfBuilder()
    positions = robot_bind_positions()
    parts = robot_skin_parts(positions)

    primitives = []
    for trim, (vertices, normals, uvs, indices, joints, weights) in sorted(parts.items()):
        primitives.append({
            "attributes": {
                "POSITION": builder.floats(vertices, "VEC3"),
                "NORMAL": builder.floats(normals, "VEC3"),
                "TEXCOORD_0": builder.floats(uvs, "VEC2"),
                "JOINTS_0": builder.values(joints, "VEC4", component_type=5123, target=34962),
                "WEIGHTS_0": builder.values(weights, "VEC4", target=34962),
            },
            "indices": builder.indices(indices),
            "material": 1 if trim else 0,
        })

    # A bind pose without rotations: the inverse is a translation back to the model origin.
    inverse_bind = []
    for name, _parent, _translation, _half, _center in ROBOT_JOINTS:
        origin = positions[name]
        inverse_bind.append((1.0, 0.0, 0.0, 0.0,
                             0.0, 1.0, 0.0, 0.0,
                             0.0, 0.0, 1.0, 0.0,
                             -origin[0], -origin[1], -origin[2], 1.0))

    nodes = [{"name": "Robot", "children": [1, 1 + len(ROBOT_JOINTS)]}]
    names = [joint[0] for joint in ROBOT_JOINTS]
    for index, (name, parent, translation, _half, _center) in enumerate(ROBOT_JOINTS):
        node = {"name": name, "translation": list(translation)}
        children = [1 + other for other, joint in enumerate(ROBOT_JOINTS) if joint[1] == name]
        if children:
            node["children"] = children
        nodes.append(node)
    nodes.append({"name": "Body", "mesh": 0, "skin": 0})

    animations = [robot_animation("Idle", 2.0, robot_idle(), builder),
                  robot_animation("Walk", 1.0, robot_walk(), builder),
                  robot_animation("Wave", 1.5, robot_wave(), builder)]
    for animation in animations:
        animation.pop("duration")

    document = {
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": nodes,
        "meshes": [{"name": "Robot", "primitives": primitives}],
        "skins": [{"name": "Robot", "skeleton": 1, "joints": list(range(1, 1 + len(ROBOT_JOINTS))),
                   "inverseBindMatrices": builder.values(inverse_bind, "MAT4")}],
        "materials": [
            {"name": "Robot paint",
             "pbrMetallicRoughness": {"baseColorFactor": [0.32, 0.55, 0.78, 1.0],
                                      "metallicFactor": 0.1, "roughnessFactor": 0.45}},
            {"name": "Robot trim",
             "pbrMetallicRoughness": {"baseColorFactor": [0.16, 0.17, 0.2, 1.0],
                                      "metallicFactor": 0.6, "roughnessFactor": 0.35}},
        ],
        "animations": animations,
    }
    glb(document, builder, path)


def material(path, lines):
    write(path, "[material format=1]\n" + "".join(line + "\n" for line in lines))


def devex_icon_pixel(size):
    """The Devex logo: a rounded square in a blue gradient with a white cube drawn in lines."""
    scale = size / 24.0
    top, bottom = (0x5e, 0xa8, 0xff), (0x3f, 0x5f, 0xe0)
    hexagon = [(12, 5.3), (17.8, 8.65), (17.8, 15.35), (12, 18.7), (6.2, 15.35), (6.2, 8.65)]
    outline = [(hexagon[i], hexagon[(i + 1) % 6], 1.0) for i in range(6)]
    inner = [((6.2, 8.65), (12, 12), 0.8), ((12, 12), (17.8, 8.65), 0.8), ((12, 12), (12, 18.7), 0.8)]
    segments = outline + inner
    half_width = 0.8

    def coverage(distance):
        return max(0.0, min(1.0, 0.5 - distance * scale))

    def rounded_square_distance(x, y):
        # Signed distance to the rect from 1 to 23 with corners of radius 5.5.
        cx, cy = abs(x - 12) - (11 - 5.5), abs(y - 12) - (11 - 5.5)
        outside = math.hypot(max(cx, 0.0), max(cy, 0.0))
        return outside + min(max(cx, cy), 0.0) - 5.5

    def segment_distance(x, y, a, b):
        ax, ay = a
        bx, by = b
        dx, dy = bx - ax, by - ay
        t = max(0.0, min(1.0, ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy)))
        return math.hypot(x - ax - t * dx, y - ay - t * dy)

    def pixel(px, py):
        x, y = (px + 0.5) / scale, (py + 0.5) / scale
        alpha = coverage(rounded_square_distance(x, y))
        if alpha <= 0.0:
            return (0, 0, 0, 0)
        mix = max(0.0, min(1.0, ((x - 2) + (y - 2)) / 40.0))
        color = [top[i] + (bottom[i] - top[i]) * mix for i in range(3)]
        for a, b, opacity in segments:
            stroke = coverage(segment_distance(x, y, a, b) - half_width) * opacity
            color = [c + (255 - c) * stroke for c in color]
        return (clamp_byte(color[0]), clamp_byte(color[1]), clamp_byte(color[2]), clamp_byte(alpha * 255))

    return pixel


def main():
    if "--audio-only" in sys.argv:
        audio_assets()
        return
    # Sandbox project
    write(SANDBOX_ASSETS / "textures" / "checker.png",
          png_bytes(256, 256, checker_pixel(16, 256, (196, 200, 206, 255), (150, 156, 166, 255))))
    # The icon of the exported game and of its window.
    write(SANDBOX_ASSETS / "textures" / "icon.png", png_bytes(256, 256, devex_icon_pixel(256)))
    crate(SANDBOX_ASSETS / "models" / "crate" / "crate.gltf")
    beacon(SANDBOX_ASSETS / "models" / "beacon.glb")
    # The sandbox sun travels along (-0.287, -0.866, -0.41).
    write(SANDBOX_ASSETS / "environments" / "daylight.hdr", hdr_bytes(1024, 512, sky(1024, 512, (0.287, 0.866, 0.41))))
    for index in range(5):
        roughness = 0.1 + 0.2 * index
        material(SANDBOX_ASSETS / "materials" / "pbr" / "gold_{}.dvxmat".format(index),
                 ["base_color = vec4(1, 0.766, 0.336, 1)", "metallic = 1", "roughness = {:g}".format(roughness)])
        material(SANDBOX_ASSETS / "materials" / "pbr" / "plastic_{}.dvxmat".format(index),
                 ["base_color = vec4(0.7, 0.05, 0.05, 1)", "metallic = 0", "roughness = {:g}".format(roughness)])

    # Importer tests
    write(TEST_DATA / "checker.png",
          png_bytes(64, 32, checker_pixel(4, 64, (255, 255, 255, 255), (0, 0, 0, 255))))
    textured_quad(TEST_DATA / "textured" / "quad.gltf")
    robot(SANDBOX_ASSETS / "models" / "robot.glb")
    robot(TEST_DATA / "animated" / "robot.glb")
    audio_assets()


if __name__ == "__main__":
    main()
