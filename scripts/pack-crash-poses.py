#!/usr/bin/env python3
"""Pack Crash OBJ poses into the compact big-endian N64 runtime format."""

from __future__ import annotations

import argparse
import math
import struct
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / ".local/assets/private/source/crash"
BODY_BASIS = SOURCE / "idle/01.obj"

TOPOLOGIES = {
    "body": {
        "source_vertices": 381,
        "clips": (
            (0x7496C0F3, SOURCE / "idle", 13),
            (0x7496C273, SOURCE / "animations/WiS4V", 25),
            (0x7496C2F3, SOURCE / "animations/WiS5V", 25),
            (0x7496C373, SOURCE / "animations/WiS6V", 20),
            (0x74952DF3, SOURCE / "animations/WiFrV", 11),
        ),
    },
    "spin": {
        "source_vertices": 470,
        "clips": ((0x7494AAF3, SOURCE / "animations/WiBlV", 8),),
    },
}

MAGIC = b"C64P"
VERSION = 1
MODEL_SCALE = 0.01 * 64.0


def round_away(value: float) -> int:
    return math.floor(value + 0.5) if value >= 0.0 else math.ceil(value - 0.5)


def parse_obj(path: Path):
    vertices = []
    normals = []
    faces = []
    for line in path.read_text(encoding="utf-8").splitlines():
        fields = line.split()
        if not fields:
            continue
        if fields[0] == "v":
            vertices.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "vn":
            normals.append(tuple(map(float, fields[1:4])))
        elif fields[0] == "f":
            faces.append(tuple(int(value.split("/")[0]) - 1 for value in fields[1:]))
    return vertices, normals, faces


def body_origin() -> tuple[float, float, float]:
    vertices, _, faces = parse_obj(BODY_BASIS)
    used = {vertex for face in faces for vertex in face}
    return (
        (min(vertices[index][0] for index in used) + max(vertices[index][0] for index in used))
        * 0.5,
        min(vertices[index][1] for index in used),
        (min(vertices[index][2] for index in used) + max(vertices[index][2] for index in used))
        * 0.5,
    )


def quantize_position(position, origin) -> tuple[int, int, int]:
    result = tuple(round_away((position[axis] - origin[axis]) * MODEL_SCALE) for axis in range(3))
    if any(value < -32768 or value > 32767 for value in result):
        raise RuntimeError(f"Position exceeds Tiny3D int16 range: {result}")
    return result


def pack_normal(normal) -> int:
    magnitude = math.sqrt(sum(value * value for value in normal))
    if magnitude == 0.0:
        normalized = (0.0, 0.0, 0.0)
    else:
        normalized = tuple(value / magnitude for value in normal)
    scales = (15.5, 31.5, 15.5)
    lows = (-16, -32, -16)
    highs = (15, 31, 15)
    values = [
        max(low, min(high, round_away(value * scale)))
        for value, scale, low, high in zip(normalized, scales, lows, highs, strict=True)
    ]
    return ((values[0] & 31) << 11) | ((values[1] & 63) << 5) | (values[2] & 31)


def read_t3dm_vertices(path: Path):
    data = path.read_bytes()
    magic, chunk_count, vertex_count = struct.unpack_from(">4sIH", data, 0)
    if magic[:3] != b"T3M":
        raise RuntimeError(f"Not a Tiny3D model: {path}")
    vertex_offset = None
    for index in range(chunk_count):
        chunk = struct.unpack_from(">I", data, 44 + index * 4)[0]
        if chunk >> 24 == ord("V"):
            vertex_offset = chunk & 0x00FFFFFF
            break
    if vertex_offset is None:
        raise RuntimeError(f"Tiny3D model has no vertex chunk: {path}")

    vertices = []
    for vertex_index in range(0, vertex_count, 2):
        offset = vertex_offset + (vertex_index // 2) * 32
        vertices.append(struct.unpack_from(">3hH", data, offset))
        vertices.append(struct.unpack_from(">3hH", data, offset + 8))
    return vertices[:vertex_count]


def build_render_map(model_vertices, basis_path: Path, source_vertex_count: int, origin):
    vertices, normals, _ = parse_obj(basis_path)
    if len(vertices) != source_vertex_count or len(normals) != source_vertex_count:
        raise RuntimeError(f"Unexpected basis geometry: {basis_path}")

    positions = [quantize_position(position, origin) for position in vertices]
    position_normals = list(zip(positions, map(pack_normal, normals), strict=True))
    positions_lookup = {}
    full_lookup = {}
    for index, position in enumerate(positions):
        positions_lookup.setdefault(position, []).append(index)
        full_lookup.setdefault(position_normals[index], []).append(index)

    render_map = []
    for render_index, packed in enumerate(model_vertices):
        position = packed[:3]
        normal = packed[3]
        candidates = full_lookup.get((position, normal), [])
        if len(candidates) != 1:
            candidates = positions_lookup.get(position, [])
        if len(candidates) != 1:
            raise RuntimeError(
                f"Cannot map render vertex {render_index}: {position}, normal {normal:#06x}, "
                f"candidates {candidates}"
            )
        render_map.append(candidates[0])
    return render_map


def load_frames(spec, origin):
    frames = []
    clips = []
    basis_faces = None
    source_vertex_count = spec["source_vertices"]
    for eid, directory, frame_count in spec["clips"]:
        first_frame = len(frames)
        for frame_index in range(1, frame_count + 1):
            path = directory / f"{frame_index:02d}.obj"
            vertices, normals, faces = parse_obj(path)
            if len(vertices) != source_vertex_count or len(normals) != source_vertex_count:
                raise RuntimeError(f"Unexpected pose geometry: {path}")
            if basis_faces is None:
                basis_faces = faces
            elif faces != basis_faces:
                raise RuntimeError(f"Pose topology changed: {path}")
            frames.append(
                [
                    (*quantize_position(position, origin), pack_normal(normal))
                    for position, normal in zip(vertices, normals, strict=True)
                ]
            )
        clips.append((eid, first_frame, frame_count))
    return clips, frames


def write_asset(path: Path, render_map, source_vertex_count: int, clips, frames) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as output:
        output.write(
            struct.pack(
                ">4s6H",
                MAGIC,
                VERSION,
                len(render_map),
                source_vertex_count,
                len(frames),
                len(clips),
                0,
            )
        )
        for eid, first_frame, frame_count in clips:
            output.write(struct.pack(">IHH", eid, first_frame, frame_count))
        output.write(struct.pack(f">{len(render_map)}H", *render_map))
        for frame in frames:
            for x, y, z, normal in frame:
                output.write(struct.pack(">3hH", x, y, z, normal))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--topology", choices=TOPOLOGIES, required=True)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    spec = TOPOLOGIES[args.topology]
    basis_path = spec["clips"][0][1] / "01.obj"
    origin = body_origin()
    model_vertices = read_t3dm_vertices(args.model)
    render_map = build_render_map(
        model_vertices,
        basis_path,
        spec["source_vertices"],
        origin,
    )
    clips, frames = load_frames(spec, origin)
    write_asset(args.output, render_map, spec["source_vertices"], clips, frames)
    print(
        {
            "topology": args.topology,
            "model": str(args.model),
            "output": str(args.output),
            "render_vertices": len(render_map),
            "source_vertices": spec["source_vertices"],
            "frames": len(frames),
            "clips": len(clips),
            "bytes": args.output.stat().st_size,
        }
    )


if __name__ == "__main__":
    main()
