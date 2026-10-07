#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose private Wisp portraits using purchaser-supplied Conflux scenery.

Large portraits use TPCASELE, the actual configured Conflux creature scenery,
mechanically reduced with nearest-neighbour sampling. Small portraits preserve
the stock CPRSMALL transparent-cutout policy. Source foregrounds are never
resized, cropped, recolored or modified. Original resources and composites must
remain private; this tool does not install assets or change creature bindings.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

from PIL import Image, ImageDraw


FORMS = {
    "wisp": ("Wisp/icons/wisp-icon-58x64.png", "Wisp/icons/wisp-icon-32.png"),
    "greater-wisp": ("WispUpgrade/icons/icon-58x64.png", "WispUpgrade/icons/icon-32x32.png"),
}


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_lod_entry(archive: Path, name: str) -> bytes:
    data = archive.read_bytes()
    if len(data) < 92 or data[:3] != b"LOD":
        raise ValueError("invalid LOD header")
    count = struct.unpack_from("<I", data, 8)[0]
    if 92 + count * 32 > len(data):
        raise ValueError("truncated LOD directory")
    for index in range(count):
        entry = 92 + index * 32
        current = data[entry:entry + 16].split(b"\0", 1)[0].decode("ascii")
        if current.casefold() != name.casefold():
            continue
        offset, size, _, compressed = struct.unpack_from("<IIII", data, entry + 16)
        stored = compressed or size
        if offset + stored > len(data):
            raise ValueError("truncated LOD payload")
        payload = data[offset:offset + stored]
        if compressed:
            payload = zlib.decompress(payload)
        if len(payload) != size:
            raise ValueError("incorrect expanded LOD payload size")
        return payload
    raise ValueError(f"resource not found: {name}")


def decode_pcx(data: bytes) -> Image.Image:
    if len(data) < 12:
        raise ValueError("truncated H3 PCX header")
    size, width, height = struct.unpack_from("<III", data)
    if not (0 < width <= 4096 and 0 < height <= 4096):
        raise ValueError("invalid H3 PCX dimensions")
    if size == width * height and len(data) == 12 + size + 768:
        image = Image.frombytes("P", (width, height), data[12:12 + size])
        image.putpalette(data[-768:])
        return image.convert("RGBA")
    if size == width * height * 3 and len(data) == 12 + size:
        return Image.frombytes("RGB", (width, height), data[12:], "raw", "BGR").convert("RGBA")
    raise ValueError("invalid H3 PCX payload")


def decode_portrait_reference(data: bytes, frame: int, transparent: bool) -> Image.Image:
    """Read the original portrait DEF's format-1 frame for private comparison."""
    if len(data) < 800:
        raise ValueError("truncated portrait DEF")
    blocks = struct.unpack_from("<I", data, 12)[0]
    offset = 784
    frame_offset = None
    for _ in range(blocks):
        group, count = struct.unpack_from("<II", data, offset)
        offset += 16 + count * 13
        if group == 0 and 0 <= frame < count:
            frame_offset = struct.unpack_from("<I", data, offset + 4 * frame)[0]
        offset += count * 4
    if frame_offset is None:
        raise ValueError("missing portrait DEF frame")
    _, fmt, full_w, full_h, width, height, left, top = struct.unpack_from("<IIIIIIii", data, frame_offset)
    if fmt != 1 or not (0 <= left and 0 <= top and left + width <= full_w and top + height <= full_h):
        raise ValueError("unsupported portrait DEF geometry/format")
    pixels = bytearray(full_w * full_h)
    for y in range(height):
        cursor = frame_offset + 32 + struct.unpack_from("<I", data, frame_offset + 32 + 4 * y)[0]
        row = bytearray()
        while len(row) < width:
            code, length = data[cursor:cursor + 2]
            cursor += 2
            length += 1
            if code == 255:
                row.extend(data[cursor:cursor + length])
                cursor += length
            else:
                row.extend([code] * length)
        if len(row) != width:
            raise ValueError("invalid portrait DEF row")
        start = (top + y) * full_w + left
        pixels[start:start + width] = row
    image = Image.frombytes("P", (full_w, full_h), bytes(pixels))
    image.putpalette(data[16:784])
    if transparent:
        image.info["transparency"] = 0
    return image.convert("RGBA")


def composite_exact(foreground: Image.Image, backdrop: Image.Image) -> Image.Image:
    if foreground.mode != "RGBA" or foreground.size != backdrop.size:
        raise ValueError("foreground must be native RGBA matching the backdrop")
    if foreground.getchannel("A").getbbox() is None:
        raise ValueError("empty Wisp foreground")
    return Image.alpha_composite(backdrop.convert("RGBA"), foreground)


def export(source_images: Path, data_dir: Path, output: Path) -> dict:
    if output.exists():
        raise ValueError("output already exists")
    bitmap_archive = data_dir / "H3ab_bmp.lod"
    sprite_archive = data_dir / "H3ab_spr.lod"
    before = {path: digest(path.read_bytes()) for path in
              [bitmap_archive, sprite_archive] + [source_images / name for names in FORMS.values() for name in names]}
    resource_names = ("TPCASELE.pcx", "CRBKGELE.pcx")
    payloads = {name: read_lod_entry(bitmap_archive, name) for name in resource_names}
    scenery = {name: decode_pcx(payload) for name, payload in payloads.items()}
    large_backdrop = scenery["TPCASELE.pcx"].resize((58, 64), Image.Resampling.NEAREST)
    large_def = read_lod_entry(sprite_archive, "TWCRPORT.def")
    small_def = read_lod_entry(sprite_archive, "CPRSMALL.def")
    # Stock Sprite and Psychic Elemental illustrate both Conflux portrait roles.
    refs = {f"stock-{name}-{role}": decode_portrait_reference(payload, frame, role == "small")
    for name, frame in (("sprite", 121), ("psychic-elemental", 122))
            for role, payload in (("large", large_def), ("small", small_def))}
    images = {"conflux-backdrop-58x64": large_backdrop,
              "conflux-TPCASELE-native": scenery["TPCASELE.pcx"],
              "conflux-CRBKGELE-native": scenery["CRBKGELE.pcx"], **refs}
    records = {}
    for form, paths in FORMS.items():
        for role, relative, size in zip(("large", "small"), paths, ((58, 64), (32, 32))):
            path = source_images / relative
            with Image.open(path) as source:
                if source.format != "PNG" or source.mode != "RGBA" or source.size != size:
                    raise ValueError(f"unexpected supplied {form} {role} format or dimensions")
                foreground = source.copy()
            images[f"{form}-{role}-foreground"] = foreground
            images[f"{form}-{role}"] = (composite_exact(foreground, large_backdrop)
                                       if role == "large" else foreground.copy())
            composite = images[f"{form}-{role}"]
            if role == "large":
                foreground_bytes, composite_bytes, backdrop_bytes = (image.tobytes() for image in (foreground, composite, large_backdrop))
                for index in range(0, len(foreground_bytes), 4):
                    pixel, actual, background = (raw[index:index + 4] for raw in (foreground_bytes, composite_bytes, backdrop_bytes))
                    if pixel[3] == 255 and actual != pixel:
                        raise AssertionError("opaque foreground pixel changed")
                    if pixel[3] == 0 and actual != background:
                        raise AssertionError("scenery outside foreground changed")
            records[f"{form}-{role}.png"] = {
                "source": relative, "size": list(size), "sourcePixelSha256": digest(foreground.tobytes()),
                "foregroundPixelsUnchanged": True, "sourceAlphaBounds": foreground.getchannel("A").getbbox(),
                "sourceGeometryUnchanged": True, "opaqueForegroundPixelsExact": True,
                "fractionalAlphaPolicy": "standard source-over composition; source RGBA retained separately",
                "method": "native alpha composition over nearest-reduced TPCASELE" if role == "large" else "native transparent source; no backdrop",
            }
    if any(digest(path.read_bytes()) != value for path, value in before.items()):
        raise AssertionError("source inputs changed")
    output.mkdir(parents=True)
    for name, image in images.items():
        image.save(output / f"{name}.png")
    # Retain supplied small PNG bytes, including original metadata, exactly.
    for form, (_, small) in FORMS.items():
        (output / f"{form}-small.png").write_bytes((source_images / small).read_bytes())
    for name, record in records.items():
        record["sha256"] = digest((output / name).read_bytes())
    sheet = Image.new("RGB", (540, 174), (35, 35, 35))
    draw = ImageDraw.Draw(sheet)
    for row, form in enumerate(FORMS):
        for col, name in enumerate((f"{form}-large-foreground", f"{form}-large", f"{form}-small",
                                    "stock-sprite-large", "stock-psychic-elemental-large", "stock-sprite-small")):
            image = images[name]
            x, y = 90 * col + 4, 87 * row + 18
            draw.text((x, y - 13), name.replace("greater-wisp", "greater")[:15], fill=(238, 225, 192))
            sheet.paste(image, (x, y), image)
    sheet.save(output / "comparison-native.png")
    sheet.resize((2160, 696), Image.Resampling.NEAREST).save(output / "comparison-4x.png")
    manifest = {
        "acceptance": "private mechanical candidate; no runtime/UI acceptance",
        "provenance": "purchaser-original H3ab_bmp.lod/TPCASELE.pcx; configured Conflux120px scenery",
        "license": "contains purchaser-original scenery; private only; do not commit or redistribute outputs",
        "inputs": {str(path): value for path, value in before.items()}, "inputsUnchanged": True,
        "resources": {name: {"sha256": digest(payload), "size": list(scenery[name].size)} for name, payload in payloads.items()},
        "referenceFrames": {"sprite": 121, "psychicElemental": 122},
        "outputs": records,
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-images", type=Path, required=True)
    parser.add_argument("--data-dir", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    manifest = export(args.source_images, args.data_dir, args.output)
    print(json.dumps({"output": str(args.output), "inputsUnchanged": manifest["inputsUnchanged"], "outputs": manifest["outputs"]}))


if __name__ == "__main__":
    main()
