#!/usr/bin/env python3

"""Generate deterministic, minimal GLB fixtures for gltfio_ext tests."""

import argparse
import hashlib
import json
import os
import struct
from pathlib import Path


COMPONENT_FLOAT = 5126
COMPONENT_UNSIGNED_SHORT = 5123
ARRAY_BUFFER = 34962
ELEMENT_ARRAY_BUFFER = 34963


class GlbBuilder:
    def __init__(self):
        self.binary = bytearray()
        self.buffer_views = []
        self.accessors = []

    def add_accessor(self, data, component_type, count, accessor_type, *, target=None,
                     minimum=None, maximum=None):
        while len(self.binary) % 4:
            self.binary.append(0)
        offset = len(self.binary)
        self.binary.extend(data)
        view = {"buffer": 0, "byteOffset": offset, "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        view_index = len(self.buffer_views)
        self.buffer_views.append(view)

        accessor = {
            "bufferView": view_index,
            "componentType": component_type,
            "count": count,
            "type": accessor_type,
        }
        if minimum is not None:
            accessor["min"] = minimum
        if maximum is not None:
            accessor["max"] = maximum
        accessor_index = len(self.accessors)
        self.accessors.append(accessor)
        return accessor_index


def pack_floats(values):
    return struct.pack(f"<{len(values)}f", *values)


def pack_ushorts(values):
    return struct.pack(f"<{len(values)}H", *values)


def create_nodes(joint_count, include_mesh):
    nodes = []
    for index in range(joint_count):
        node = {"name": f"Bone_{index:03d}"}
        if index == 0 and joint_count > 1:
            node["children"] = list(range(1, joint_count))
        nodes.append(node)
    if include_mesh:
        nodes.append({"name": "FixtureMesh", "mesh": 0, "skin": 0})
    return nodes


def create_animations(builder, target_node, names, duration):
    times = builder.add_accessor(
        pack_floats([0.0, duration]), COMPONENT_FLOAT, 2, "SCALAR",
        minimum=[0.0], maximum=[duration])
    animations = []
    for index, name in enumerate(names, start=1):
        distance = index * 0.01
        translations = builder.add_accessor(
            pack_floats([0.0, 0.0, 0.0, distance, 0.0, 0.0]),
            COMPONENT_FLOAT, 2, "VEC3")
        animations.append({
            "name": name,
            "samplers": [{"input": times, "output": translations, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": target_node, "path": "translation"}}],
        })
    return animations


def create_glb(joint_count, animation_names, duration, *, include_mesh):
    builder = GlbBuilder()
    document = {
        "asset": {"version": "2.0", "generator": "XMuscle gltfio_ext fixture generator"},
        "scene": 0,
        "scenes": [{"nodes": [0] + ([joint_count] if include_mesh else [])}],
        "nodes": create_nodes(joint_count, include_mesh),
    }

    if include_mesh:
        positions = builder.add_accessor(
            pack_floats([0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0, 0.0]),
            COMPONENT_FLOAT, 3, "VEC3", target=ARRAY_BUFFER,
            minimum=[0.0, 0.0, 0.0], maximum=[1.0, 1.0, 0.0])
        joints = builder.add_accessor(
            pack_ushorts([0, 0, 0, 0] * 3), COMPONENT_UNSIGNED_SHORT, 3, "VEC4",
            target=ARRAY_BUFFER)
        weights = builder.add_accessor(
            pack_floats([1.0, 0.0, 0.0, 0.0] * 3), COMPONENT_FLOAT, 3, "VEC4",
            target=ARRAY_BUFFER)
        indices = builder.add_accessor(
            pack_ushorts([0, 1, 2]), COMPONENT_UNSIGNED_SHORT, 3, "SCALAR",
            target=ELEMENT_ARRAY_BUFFER, minimum=[0], maximum=[2])
        identity = [
            1.0, 0.0, 0.0, 0.0,
            0.0, 1.0, 0.0, 0.0,
            0.0, 0.0, 1.0, 0.0,
            0.0, 0.0, 0.0, 1.0,
        ]
        inverse_bind_matrices = builder.add_accessor(
            pack_floats(identity * joint_count), COMPONENT_FLOAT, joint_count, "MAT4")
        document["meshes"] = [{
            "name": "FixtureTriangle",
            "primitives": [{
                "attributes": {"POSITION": positions, "JOINTS_0": joints, "WEIGHTS_0": weights},
                "indices": indices,
            }],
        }]
        document["skins"] = [{
            "name": "FixtureSkin",
            "inverseBindMatrices": inverse_bind_matrices,
            "joints": list(range(joint_count)),
            "skeleton": 0,
        }]

    if animation_names:
        document["animations"] = create_animations(
            builder, 1 if joint_count > 1 else 0, animation_names, duration)

    document["bufferViews"] = builder.buffer_views
    document["accessors"] = builder.accessors
    document["buffers"] = [{"byteLength": len(builder.binary)}]

    json_chunk = json.dumps(
        document, ensure_ascii=True, separators=(",", ":"), sort_keys=True).encode("utf-8")
    json_chunk += b" " * ((-len(json_chunk)) % 4)
    binary_chunk = bytes(builder.binary)
    binary_chunk += b"\0" * ((-len(binary_chunk)) % 4)

    total_length = 12 + 8 + len(json_chunk) + 8 + len(binary_chunk)
    return b"".join([
        struct.pack("<4sII", b"glTF", 2, total_length),
        struct.pack("<I4s", len(json_chunk), b"JSON"),
        json_chunk,
        struct.pack("<I4s", len(binary_chunk), b"BIN\0"),
        binary_chunk,
    ])


def generate_fixture(entry):
    return create_glb(
        entry["joint_count"],
        entry.get("animations", []),
        entry.get("animation_duration_seconds", 2.08333325),
        include_mesh=entry["include_mesh"],
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path)
    parser.add_argument("--print-hashes", action="store_true")
    args = parser.parse_args()

    manifest = json.loads(args.manifest.read_text(encoding="utf-8"))
    generated = []
    for entry in manifest["fixtures"]:
        data = generate_fixture(entry)
        digest = hashlib.sha256(data).hexdigest()
        generated.append((entry, data, digest))
        if args.print_hashes:
            print(f"{digest}  {entry['file']}")
            continue
        if digest != entry["sha256"]:
            raise SystemExit(
                f"SHA-256 mismatch for {entry['file']}: expected {entry['sha256']}, got {digest}")

    if args.print_hashes:
        return
    if args.output_dir is None:
        parser.error("--output-dir is required unless --print-hashes is used")

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for entry, data, _ in generated:
        destination = args.output_dir / entry["file"]
        temporary = destination.with_name(f".{destination.name}.{os.getpid()}.tmp")
        temporary.write_bytes(data)
        temporary.replace(destination)
        print(f"staged {destination} ({len(data)} bytes)")


if __name__ == "__main__":
    main()
