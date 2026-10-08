#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Export private native map derivatives; never change approved handoff inputs.

Read purchaser DEFs in memory for geometry only. No original pixels leave the
reader. Output is a new detached overlay and review bundle, not installation.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import statistics
import struct

from PIL import Image, ImageDraw

from fit_new_horizons_wisp_portraits import read_lod_entry


PINS = {
    "cabir": [
        "ec3f5de74740f7106774e3dd9f22e9743a5e7047eda4f6c7ba8182e750ee3a66",
        "8550e52303a3542c3c61ca400106dd2e0418b94f0c1168ed3d92217bb7613dc1",
        "11edf01095a07dbf2b764a97f930664eb907c756ccc391109fca32d8b4c95e00",
        "a3a9d4f7bfbe0387ee016d62a9b46836276fc9395f9780d2b0a249e185c20dad",
        "cf97f4b8c8787ce53abb806ace6729bb89c515689f36b5febc61b524ef83a187",
        "1ade65862672be437a5ae77694b7856b0a893a22d45c0af3036c59016467bd35",
        "817b08ae50e8e3681367139c76a7b56deb07ee4238d10029f9b5bf38d0f4d136",
    ],
    "cabir-master": [
        "1eaa5c1d1915fc4ed6b1ab063aa6df943a431ca7ac1f6d132ceda32eabf3ca5c",
        "a0704f2986c15e786cb8869031f08e9555d96e95b2243f6b8599a1eacface9a3",
        "eb185aef4a2a39fed6f61886e9aae112151f2371d40fba6c4de38d5b0520f711",
        "b65beee196f15792f68be7a5baa6e9837bf2adedc48d310c48ef3eff3851b0bb",
        "12d83a5cc90ae8fa16a03889c3b6e6daf0130214ddd4047f82c37d7374fb4d2f",
        "cb8c87a3849b467c23eb5812e9d733117548ba59ad4e5ff439b394926d18b435",
        "e0fde807b5678ddf64c4c56b4cffe6b86ee32c173afeac01f4dba05ff2a63650",
    ],
}
DESCRIPTORS = {"cabir": "NH_CabirHandoffNativeMap.json", "cabir-master": "NH_CabirMasterHandoffNativeMap.json"}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def reference_geometry(data):
    """Decode format-3 indices only; discard transparency/shadow indices 0..7."""
    if len(data) < 800 or struct.unpack_from("<I", data, 12)[0] != 1:
        raise ValueError("unsupported reference DEF group layout")
    group, count = struct.unpack_from("<II", data, 784)
    if group != 0 or count != 8:
        raise ValueError("reference must have eight group-zero frames")
    offsets = struct.unpack_from("<" + "I" * count, data, 800 + 13 * count)
    records = []
    for offset in offsets:
        _, fmt, fw, fh, width, height, left, top = struct.unpack_from("<IIIIIIii", data, offset)
        if fmt != 3 or (fw, fh) != (64, 64) or width != 64 or not (0 <= top <= 64 - height) or left != 0:
            raise ValueError("unsupported native reference geometry")
        mask = Image.new("L", (fw, fh))
        pixels = mask.load()
        base = offset + 32
        for y in range(height):
            cursor = base + struct.unpack_from("<H", data, base + y * 2 * (width // 32))[0]
            row = []
            while len(row) < width:
                segment = data[cursor]
                cursor += 1
                code, length = segment // 32, (segment & 31) + 1
                if code == 7:
                    row.extend(data[cursor:cursor + length])
                    cursor += length
                else:
                    row.extend([code] * length)
            if len(row) != width:
                raise ValueError("invalid reference row length")
            for x, value in enumerate(row):
                pixels[x, top + y] = 255 if value >= 8 else 0
        bounds = mask.getbbox()
        if not bounds:
            raise ValueError("empty colored reference body")
        contact = [x for x in range(64) if pixels[x, bounds[3] - 1]]
        records.append({"canvas": [64, 64], "bodyBounds": list(bounds),
                        "foot": [(min(contact) + max(contact) + 1) / 2, bounds[3]]})
    return records


def native_contract(archive):
    resources = {}
    for name in ("AVWgrem0.def", "AVWgrex0.def"):
        data = read_lod_entry(archive, name)
        resources[name] = {"sha256": digest(data), "frames": reference_geometry(data)}
    frames = [frame for resource in resources.values() for frame in resource["frames"]]
    height = math.floor(statistics.median(f["bodyBounds"][3] - f["bodyBounds"][1] for f in frames) + 0.5)
    anchor = [statistics.median(f["foot"][0] for f in frames), statistics.median(f["foot"][1] for f in frames)]
    return {"resources": resources, "targetBodyHeight": height, "targetFoot": anchor,
            "policy": "combined median colored-body height and bottom-row contact; indices 0..7 excluded"}


def fit_frames(images, contract):
    bounds = [im.getchannel("A").getbbox() for im in images]
    if any(bound is None for bound in bounds):
        raise ValueError("empty source frame")
    union = (min(b[0] for b in bounds), min(b[1] for b in bounds),
             max(b[2] for b in bounds), max(b[3] for b in bounds))
    contacts = []
    for image, bound in zip(images, bounds):
        alpha = image.getchannel("A")
        xs = [x for x in range(image.width) if alpha.getpixel((x, bound[3] - 1))]
        contacts.append(((min(xs) + max(xs) + 1) / 2, bound[3]))
    pivot = (statistics.median(p[0] for p in contacts), statistics.median(p[1] for p in contacts))
    scale = contract["targetBodyHeight"] / (union[3] - union[1])
    size = (max(1, round((union[2] - union[0]) * scale)), max(1, round((union[3] - union[1]) * scale)))
    # Integer dimensions inevitably round; apply the same transform to all poses.
    placement = (round(contract["targetFoot"][0] - (pivot[0] - union[0]) * size[0] / (union[2] - union[0])),
                 round(contract["targetFoot"][1] - (pivot[1] - union[1]) * size[1] / (union[3] - union[1])))
    if placement[0] < 0 or placement[1] < 0 or placement[0] + size[0] > 64 or placement[1] + size[1] > 64:
        raise ValueError("native transform would clip source content")
    results = []
    for image in images:
        subject = image.crop(union)  # Only shared, fully transparent padding removed.
        resized = subject.resize(size, Image.Resampling.LANCZOS)
        canvas = Image.new("RGBA", (64, 64))
        canvas.alpha_composite(resized, placement)
        results.append(canvas)
    return results, {"sourceUnion": list(union), "sourceFoot": list(pivot), "uniformScale": scale,
                     "roundedSize": list(size), "placement": list(placement),
                     "canvas": [64, 64], "targetFoot": contract["targetFoot"],
                     "resampling": "Pillow LANCZOS, one common transform across both forms"}


def save_reviews(originals, corrected, destination):
    destination.mkdir(parents=True, exist_ok=True)
    matte = (56, 49, 43, 255)
    sheet = Image.new("RGBA", (7 * 128, 2 * 196), matte)
    draw = ImageDraw.Draw(sheet)
    for index, (original, result) in enumerate(zip(originals, corrected)):
        x, y = (index % 7) * 128, (index // 7) * 196
        sheet.alpha_composite(original, (x, y))
        sheet.alpha_composite(result, (x + 32, y + 112))
        draw.text((x + 2, y + 178), f"{index % 7}: supplied / native", fill="white")
    sheet.save(destination / "comparison-native.png")
    sheet.resize((sheet.width * 4, sheet.height * 4), Image.Resampling.NEAREST).save(destination / "comparison-4x.png")
    for form_index, form in enumerate(PINS):
        for factor in (1, 4):
            frames = []
            for result in corrected[form_index * 7:(form_index + 1) * 7]:
                frame = Image.new("RGBA", (64, 64), matte)
                frame.alpha_composite(result)
                frames.append(frame.resize((64 * factor, 64 * factor), Image.Resampling.NEAREST).convert("RGB"))
            frames[0].save(destination / f"{form}-walk-{factor}x.gif", save_all=True, append_images=frames[1:],
                           duration=160, loop=0, disposal=2, optimize=False)


def export(source_images, archive, output, review):
    if output.exists() or review.exists():
        raise ValueError("output and review directories must be new")
    source_files, images, pins = [], [], {}
    for form, expected in PINS.items():
        for index, pin in enumerate(expected):
            relative = f"cabir-handoff/{form}/map/walk/frame-{index:02}.png"
            path = source_images / relative
            data = path.read_bytes()
            if digest(data) != pin:
                raise ValueError(f"approved source pin mismatch: {relative}")
            image = Image.open(path).convert("RGBA")
            if image.size != (128, 112):
                raise ValueError("source canvas must be 128x112")
            images.append(image)
            source_files.append(path)
            pins[relative] = pin
    contract = native_contract(archive)
    corrected, transform = fit_frames(images, contract)
    # Versioned map-only paths keep existing encounter references and battle art intact.
    files = {}
    for form_index, form in enumerate(PINS):
        base = f"cabir-handoff-map-native-v1/{form}/"
        for index, image in enumerate(corrected[form_index * 7:(form_index + 1) * 7]):
            relative = f"Mods/new-horizons/Images/{base}walk/frame-{index:02}.png"
            target = output / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            image.save(target)
            files[relative] = digest(target.read_bytes())
        target = output / f"Mods/new-horizons/Content/sprites/{DESCRIPTORS[form]}"
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(json.dumps({"basepath": base, "sequences": [{"group": 0,
            "frames": [f"walk/frame-{index:02}.png" for index in range(7)]}]}, indent=2) + "\n", encoding="utf-8")
        files[str(target.relative_to(output))] = digest(target.read_bytes())
    save_reviews(images, corrected, review)
    bindings = {"core:gremlin": {"graphics": {"map": DESCRIPTORS["cabir"].replace(".json", ".def")}},
                "core:masterGremlin": {"graphics": {"map": DESCRIPTORS["cabir-master"].replace(".json", ".def")}}}
    # Root merges only graphics.map into a frozen candidate's existing registry.
    # Replacing the old map descriptor would also alter its encounter-frame aliases.
    (output / "map-only-bindings.json").write_text(json.dumps(bindings, indent=2) + "\n", encoding="utf-8")
    manifest = {"status": "private mechanical map candidate; no runtime promotion or visual approval",
                "sourcePins": pins, "originalReferenceGeometryOnly": contract,
                "transform": transform, "files": files, "mapOnlyBindings": bindings,
                "outputBounds": [list(im.getchannel("A").getbbox()) for im in corrected],
                "preservedRoles": ["battle", "encounter", "portraits", "projectile"]}
    for path, expected in zip(source_files, pins.values()):
        if digest(path.read_bytes()) != expected:
            raise ValueError("source changed during export")
    text = json.dumps(manifest, indent=2) + "\n"
    (output / "manifest.json").write_text(text, encoding="utf-8")
    (review / "manifest.json").write_text(text, encoding="utf-8")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-images", type=Path, required=True)
    parser.add_argument("--sprite-archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--review", type=Path, required=True)
    args = parser.parse_args()
    export(args.source_images, args.sprite_archive, args.output, args.review)


if __name__ == "__main__":
    main()
