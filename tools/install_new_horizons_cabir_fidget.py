#!/usr/bin/env python3
"""Stage the user-approved base Cabir fidget as an optional private overlay.

Copies four approved native frames byte-for-byte and adds only animation group1
(MOUSEON/random fidget). Existing groups, HOLDING and all bindings are preserved.
No Master Cabir animation is synthesized. Compose this overlay with the complete
handoff privately; this script never installs it into live gameplay resources.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
from pathlib import Path, PurePosixPath

from PIL import Image


DESCRIPTOR = Path("Mods/new-horizons/Content/sprites/NH_CabirCompleteHandoff.json")
IMAGE_ROOT = Path("Mods/new-horizons/Images")
EXPECTED_BASEPATH = "cabir-complete-handoff/cabir/battle/"
FRAME_HASHES = (
    "d9b7e5a81f90673384a106cd8f3c83b14de0e9be1a7e87e5c275146b485c0d55",
    "2821056bbd4f6b62c0420acc202a761e645c979fe3b55587509d53beda6d4d70",
    "6035865c06d5e75c5975cdba6234a7da4fbcb32c62a489897ceac4b49a4f280c",
    "e0201f50030a953dc0f05dd0289a2f2fbc06b5cd5407eb5079059aa7f654bdd0",
)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def relative(value: str) -> Path:
    path = PurePosixPath(value)
    if path.is_absolute() or ".." in path.parts or "\\" in value:
        raise ValueError(f"unsafe frame reference: {value}")
    return Path(path)


def compose_approved_fidget(descriptor: dict, approved_frames: Path) -> tuple[dict, dict, list, dict]:
    """Validate/copy approved bytes in memory; never write or approve an installation."""
    if descriptor.get("basepath") != EXPECTED_BASEPATH:
        raise ValueError("expected the complete base Cabir handoff descriptor")
    sequences = descriptor.get("sequences", [])
    groups = [sequence["group"] for sequence in sequences]
    if len(groups) != len(set(groups)) or 2 not in groups:
        raise ValueError("descriptor requires unique groups and original HOLDING group2")
    if 1 in groups:
        raise ValueError("source already contains group1; preserve it and use the pristine handoff")
    inputs = {}
    new_frames = []
    payloads = {}
    frame_receipts = []
    for index, expected in enumerate(FRAME_HASHES):
        name = f"frame-{index:02d}.png"
        path = approved_frames / name
        data = path.read_bytes()
        if digest(data) != expected:
            raise ValueError(f"approved frame hash mismatch: {name}")
        with Image.open(path) as image:
            if image.mode != "RGBA" or image.size != (450, 400) or getattr(image, "is_animated", False):
                raise ValueError(f"approved frame must be static RGBA450x400: {name}")
            alpha = image.getchannel("A")
            meaningful_bounds = alpha.point(lambda value: 255 if value >= 128 else 0).getbbox()
            if meaningful_bounds is None or meaningful_bounds[3] != 267:
                raise ValueError(f"approved frame feet no longer end at baseline267: {name}")
            frame_receipts.append({"frame": name, "sha256": expected,
                                   "alphaBounds": list(alpha.getbbox()), "alpha128Bounds": list(meaningful_bounds)})
        inputs[str(path)] = expected
        frame_reference = f"fidget-approved-v1/{name}"
        new_frames.append(frame_reference)
        payloads[IMAGE_ROOT / EXPECTED_BASEPATH / frame_reference] = data

    result = copy.deepcopy(descriptor)
    result["sequences"].append({"group": 1, "generateOverlay": 1, "frames": new_frames})
    if result["sequences"][:-1] != sequences:
        raise AssertionError("original descriptor groups changed")
    return result, payloads, frame_receipts, inputs


def stage(source_mount: Path, approved_frames: Path, output: Path) -> dict:
    source_mount = source_mount.resolve()
    approved_frames = approved_frames.resolve()
    output = output.resolve()
    if output == source_mount or output.is_relative_to(source_mount) or source_mount.is_relative_to(output):
        raise ValueError("output must be separate from source mount")
    if output == approved_frames or output.is_relative_to(approved_frames) or approved_frames.is_relative_to(output):
        raise ValueError("output must be separate from approved input frames")
    descriptor_path = source_mount / DESCRIPTOR
    descriptor_bytes = descriptor_path.read_bytes()
    descriptor = json.loads(descriptor_bytes)
    result, payloads, frame_receipts, inputs = compose_approved_fidget(descriptor, approved_frames)
    inputs[str(descriptor_path)] = digest(descriptor_bytes)
    # Pin original frames; they remain in the base mount, not this sparse overlay.
    for sequence in descriptor["sequences"]:
        for frame in sequence["frames"]:
            path = source_mount / IMAGE_ROOT / EXPECTED_BASEPATH / relative(frame)
            inputs[str(path)] = digest(path.read_bytes())
    payloads[DESCRIPTOR] = (json.dumps(result, indent=2) + "\n").encode()
    for path, expected in inputs.items():
        if digest(Path(path).read_bytes()) != expected:
            raise AssertionError(f"input changed during export: {path}")
    receipt = {
        "status": "Approved base art staged privately; runtime hover/random fidget unverified",
        "method": "byte-exact approved450x400 copies; group1 only; no pixel or feet edits",
        "group": 1, "generateOverlay": 1, "baselineAlpha128": 267,
        "masterIncluded": False, "holdingGroupUnchanged": True, "allExistingGroupsUnchanged": True,
        "inputs": inputs, "inputsUnchanged": True, "frames": frame_receipts,
        "outputs": {str(path): digest(data) for path, data in payloads.items()},
    }
    payloads[Path("manifest.json")] = (json.dumps(receipt, indent=2) + "\n").encode()
    # Exact re-execution is a no-op. Reject a changed/foreign output rather than
    # overwrite it, including extra files that might belong to another task.
    if output.exists():
        existing = {path.relative_to(output) for path in output.rglob("*") if path.is_file()}
        if existing != set(payloads) or any((output / path).read_bytes() != data for path, data in payloads.items()):
            raise ValueError("existing overlay differs; choose a new output directory")
        return receipt
    output.mkdir(parents=True)
    for path, data in payloads.items():
        destination = output / path
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    return receipt


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-mount", type=Path, required=True)
    parser.add_argument("--approved-frames", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    receipt = stage(args.source_mount, args.approved_frames, args.output)
    print(json.dumps({"output": str(args.output), "group": receipt["group"],
                      "frames": len(receipt["frames"]), "inputsUnchanged": receipt["inputsUnchanged"]}))


if __name__ == "__main__":
    main()
