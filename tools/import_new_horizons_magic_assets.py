#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Import the user's private magic-art handoff into a NEW local resource overlay.

The complete output includes purchaser-derived Guild background composites
and remains a private overlay. The standalone casting glows were separately
authorized for redistribution on 2026-10-08; see
assets/new-horizons/magic-assets/README.md. Do not extend that authorization to
original-game backgrounds. No archive code is executed, original installations
are untouched, and an existing destination is never overwritten. Requires Pillow.
"""
import argparse
from collections import deque
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import stat
import sys
import zipfile

from PIL import Image, ImageFilter


SCHOOLS = ("light", "nature", "sorcery", "havoc", "chaos", "shadow")
FACTIONS = ("castle", "rampart", "tower", "inferno", "necropolis", "dungeon",
            "stronghold", "fortress", "conflux")
CAST_ROOT = "casting-colors/Data/casting-glow-all-classes-v3/"
IMAGE_ROOT = "Mods/new-horizons/Images/"
RESOURCE_ROOT = "NH_magic_assets/"
GUILD_CLEAN_PLATE_SHA256 = "c6d76b5f2cfc195e58e76e111125922e2c65f4993bdbb17aa1a2969e5a60cd9b"
GUILD_NATIVE_BACKGROUND_SHA256 = "c4f59d612ed096d7f84ff7f6f764430fea6fb27254812e6a4a3fcafd3b9d4030"
GUILD_SIZE = (800, 600)
GUILD_BOOK_SIZE = (122, 153)
GUILD_RESTORE_SOURCE_BBOX = (15, 24, 31, 67)
GUILD_RESTORE_BBOX = (391, 366, 411, 413)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def native_rect(rect):
    x, y, width, height = rect
    left, top = round(x * 100 / 181), round(y * 100 / 181)
    right, bottom = round((x + width) * 100 / 181), round((y + height) * 100 / 181)
    return left, top, right - left, bottom - top


def png(data, required_size=None, require_alpha=False):
    with Image.open(io.BytesIO(data)) as source:
        if source.format != "PNG" or getattr(source, "is_animated", False):
            raise ValueError("Only single-frame PNGs are accepted")
        if source.width > 2048 or source.height > 2048:
            raise ValueError("Image dimensions exceed the private handoff limit")
        if required_size is not None and source.size != required_size:
            raise ValueError(f"Unexpected PNG dimensions: {source.size}")
        if require_alpha and source.mode != "RGBA":
            raise ValueError("Casting overlays must preserve RGBA alpha")
        source.load()
        return source.copy()


def _largest_red_component(image):
    """Return a binary mask and bounds for the largest saturated red component."""
    hsv = image.convert("RGB").convert("HSV")
    width, height = hsv.size
    candidates = bytearray(
        1 if (h <= 6 or h >= 250) and saturation >= 150 and value >= 100 else 0
        for h, saturation, value in hsv.get_flattened_data())
    visited = bytearray(len(candidates))
    largest = []
    largest_bounds = None
    for start, candidate in enumerate(candidates):
        if not candidate or visited[start]:
            continue
        pending = deque([start])
        visited[start] = 1
        component = []
        left, top, right, bottom = width, height, -1, -1
        while pending:
            index = pending.popleft()
            x, y = index % width, index // width
            component.append(index)
            left, top = min(left, x), min(top, y)
            right, bottom = max(right, x), max(bottom, y)
            for neighbor_y in range(max(0, y - 1), min(height, y + 2)):
                for neighbor_x in range(max(0, x - 1), min(width, x + 2)):
                    neighbor = neighbor_y * width + neighbor_x
                    if candidates[neighbor] and not visited[neighbor]:
                        visited[neighbor] = 1
                        pending.append(neighbor)
        if len(component) > len(largest):
            largest = component
            largest_bounds = (left, top, right + 1, bottom + 1)

    if not largest:
        raise ValueError("No old guild ribbon found in the pinned native background")
    mask = Image.new("L", (width, height), 0)
    pixels = bytearray(len(candidates))
    for index in largest:
        pixels[index] = 255
    mask.putdata(pixels)
    return mask, largest_bounds, len(largest)


def _guild_ribbon_restore_mask(native_background, position):
    if native_background.size != GUILD_SIZE:
        raise ValueError("Native guild background must be 800x600")
    x, y = position[:2]
    width, height = GUILD_BOOK_SIZE
    patch = native_background.convert("RGB").crop((x, y, x + width, y + height))
    component, component_bbox, _ = _largest_red_component(patch)
    if component_bbox != GUILD_RESTORE_SOURCE_BBOX:
        raise ValueError(f"Unexpected old ribbon footprint: {component_bbox}")
    # The small dilation restores only the old ribbon's antialiased edge.
    restored_patch = component.filter(ImageFilter.MaxFilter(5))
    mask = Image.new("L", GUILD_SIZE, 0)
    mask.paste(restored_patch, (x, y))
    if mask.getbbox() != GUILD_RESTORE_BBOX:
        raise ValueError(f"Unexpected dilated ribbon footprint: {mask.getbbox()}")
    return mask


def _compose_clean_plate_book(native_background, clean_native, restore_mask, book_cutout, position):
    x, y, width, height = position
    # The native resource remains the engine's 67x85 overlay, not a full-screen plate.
    # Only the ribbon repair and opaque/antialiased bookmark pixels carry alpha.
    local_mask = restore_mask.crop((x, y, x + width, y + height))
    clean_patch = clean_native.crop((x, y, x + width, y + height))
    original_patch = native_background.convert("RGB").crop((x, y, x + width, y + height))
    restored_patch = Image.composite(clean_patch, original_patch, local_mask).convert("RGBA")
    restored_patch.putalpha(local_mask)
    cutout = (book_cutout.convert("RGBa")
              .resize((width, height), Image.Resampling.LANCZOS)
              .convert("RGBA"))
    result = Image.new("RGBA", (width, height), (0, 0, 0, 0))
    result.alpha_composite(restored_patch)
    result.alpha_composite(cutout)
    pixels = bytearray(result.tobytes())
    for offset in range(0, len(pixels), 4):
        if pixels[offset + 3] == 0:
            pixels[offset:offset + 3] = b"\0\0\0"
    result = Image.frombytes("RGBA", result.size, bytes(pixels))
    return result


def _load_private_guild_png(path, expected_sha, dimensions, label):
    data = Path(path).read_bytes()
    if sha(data) != expected_sha:
        raise ValueError(f"Unexpected private {label} SHA-256")
    image = png(data, dimensions)
    return image, sha(data)


def prepare(archive, guild_clean_plate=None, guild_native_background=None):
    """Validate first, then return an allowlisted output map without writing."""
    if (guild_clean_plate is None) != (guild_native_background is None):
        raise ValueError("Clean-plate mode requires both private input images")
    clean_plate = native_background = None
    clean_plate_sha = native_background_sha = None
    if guild_clean_plate is not None:
        clean_plate, clean_plate_sha = _load_private_guild_png(
            guild_clean_plate, GUILD_CLEAN_PLATE_SHA256, (1448, 1086), "guild clean plate")
        native_background, native_background_sha = _load_private_guild_png(
            guild_native_background, GUILD_NATIVE_BACKGROUND_SHA256, GUILD_SIZE,
            "native guild background")
    with zipfile.ZipFile(archive) as bundle:
        infos = bundle.infolist()
        names = [item.filename for item in infos]
        if len(set(names)) != len(names) or len(names) > 5000:
            raise ValueError("Duplicate entries or oversized archive inventory")
        if sum(item.file_size for item in infos) > 256 * 1024 * 1024:
            raise ValueError("Archive exceeds uncompressed size limit")
        for item in infos:
            path = PurePosixPath(item.filename)
            mode = item.external_attr >> 16
            if (path.is_absolute() or ".." in path.parts or "\\" in item.filename
                    or ":" in item.filename or stat.S_ISLNK(mode)):
                raise ValueError("Unsafe archive entry")
        manifest = json.loads(bundle.read("collection-manifest.json"))
        if manifest.get("casting_version") != 6:
            raise ValueError("Requires the corrected v6 Elementalist handoff")
        checksums = manifest["files_sha256"]

        def read(name):
            data = bundle.read(name)
            if checksums.get(name) != sha(data):
                raise ValueError(f"Handoff checksum mismatch: {name}")
            return data

        outputs = {}
        config = {"castingGlows": {}, "guildBooks": {}}
        # Use actual sprite directories. CH010 etc. are intentional names.
        sprite_names = sorted({name[len(CAST_ROOT):].split("/")[1]
                               for name in names if name.startswith(CAST_ROOT + "sprites/")})
        if len(sprite_names) != 18 or any(not name.startswith("CH") or not name[2:].isdigit()
                                        for name in sprite_names):
            raise ValueError("Expected eighteen native casting sprite directories")
        for sprite in sprite_names:
            config["castingGlows"][sprite] = {}
            for school in SCHOOLS:
                frames = []
                dimensions = None
                for index in range(8):
                    source = f"{CAST_ROOT}sprites/{sprite}/{school}/glow_overlays/frame_{index:02}.png"
                    data = read(source)
                    image = png(data, dimensions, require_alpha=True)
                    dimensions = image.size
                    resource = f"{RESOURCE_ROOT}casting/{sprite}/{school}/frame_{index:02}.png"
                    outputs[IMAGE_ROOT + resource] = data
                    frames.append(resource)
                config["castingGlows"][sprite][school] = {
                    "frames": frames, "dimensions": list(dimensions)}
        book_manifest = json.loads(read("mage-guild-bookmarks/manifest.json"))
        if (book_manifest["preview_size"] != [1448, 1086]
                or book_manifest["native_guild_size"] != [800, 600]
                or book_manifest["crop_xywh"] != [684, 623, 122, 153]):
            raise ValueError("Unknown book placement geometry")
        x, y, width, height = native_rect(book_manifest["crop_xywh"])
        guild_source_inputs = {}
        restore_mask = None
        clean_native = None
        if clean_plate is not None:
            restore_mask = _guild_ribbon_restore_mask(native_background, (x, y, width, height))
            clean_native = clean_plate.convert("RGB").resize(GUILD_SIZE, Image.Resampling.LANCZOS)
        for faction in FACTIONS:
            if clean_plate is None:
                # Legacy private mode: an opaque rectangular patch. Kept for reproducibility,
                # but the clean-plate mode is the visually reviewed route.
                opaque_data = read(f"mage-guild-bookmarks/{faction}.png")
                image = png(opaque_data, GUILD_BOOK_SIZE)
                image = image.convert("RGBa").resize((width, height), Image.Resampling.LANCZOS).convert("RGBA")
                guild_source_inputs[faction] = {"opaqueCropSha256": sha(opaque_data)}
                resource_data = image
            else:
                transparent_data = read(f"mage-guild-bookmarks/transparent/{faction}.png")
                mask_data = read(f"mage-guild-bookmarks/masks/{faction}.png")
                image = png(transparent_data, GUILD_BOOK_SIZE, require_alpha=True)
                alpha_mask = png(mask_data, GUILD_BOOK_SIZE)
                if (alpha_mask.mode != "L" or alpha_mask.getextrema() != (0, 255)
                        or any(value not in (0, 255) for value in alpha_mask.get_flattened_data())):
                    raise ValueError(f"Invalid binary guild mask for {faction}")
                if alpha_mask.tobytes() != image.getchannel("A").tobytes():
                    raise ValueError(f"Transparent guild cutout/mask mismatch for {faction}")
                resource_data = _compose_clean_plate_book(
                    native_background, clean_native, restore_mask, image, (x, y, width, height))
                guild_source_inputs[faction] = {
                    "transparentSha256": sha(transparent_data),
                    "maskSha256": sha(mask_data),
                }
            buffer = io.BytesIO()
            resource_data.save(buffer, format="PNG")
            resource = f"{RESOURCE_ROOT}guild/{faction}.png"
            outputs[IMAGE_ROOT + resource] = buffer.getvalue()
            config["guildBooks"][faction] = {"image": resource, "position": [x, y]}
        outputs["config/newHorizonsMagicAssets.json"] = (json.dumps(config, indent=2) + "\n").encode()
        receipt = {"privateOnly": True, "castingVersion": 6,
                   "sourceArchiveSha256": sha(Path(archive).read_bytes()),
                   "sourcePixelsCommitted": False, "outputSha256": {
                       name: sha(data) for name, data in sorted(outputs.items())}}
        if clean_plate is None:
            receipt["guildImportMode"] = "legacyOpaqueCropNotVisuallyAccepted"
        else:
            receipt["guildImportMode"] = "cleanPlateMasked"
            receipt["guildPrivateInputs"] = {
                "cleanPlateSha256": clean_plate_sha,
                "nativeBackgroundSha256": native_background_sha,
            }
            receipt["guildRestorationMaskNative"] = {
                "bbox": list(restore_mask.getbbox()),
                "pixelCount": sum(value > 0 for value in restore_mask.get_flattened_data()),
            }
            receipt["guildSourceInputs"] = guild_source_inputs
        receipt["generatedPngCount"] = sum(name.lower().endswith(".png") for name in outputs)
        receipt["outputFileCountIncludingReceipt"] = len(outputs) + 1
        outputs["PRIVATE_MAGIC_ASSETS.json"] = (json.dumps(receipt, indent=2) + "\n").encode()
        return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True,
                        help="NEW private overlay directory outside tracked content")
    parser.add_argument("--guild-clean-plate", type=Path,
                        help="Pinned private full-room clean plate for masked guild import")
    parser.add_argument("--guild-native-background", type=Path,
                        help="Pinned private 800x600 native guild background; required with clean plate")
    args = parser.parse_args()
    output = args.output.resolve()
    repository = Path(__file__).resolve().parents[1]
    if output.exists() or output.is_symlink():
        parser.error("Output must not already exist")
    if output == repository or repository in output.parents:
        relative = output.relative_to(repository)
        if not relative.parts or relative.parts[0] not in ("build", "output"):
            parser.error("Private pixels may only enter ignored build/output directories")
    try:
        if (args.guild_clean_plate is None) != (args.guild_native_background is None):
            parser.error("--guild-clean-plate and --guild-native-background must be used together")
        outputs = prepare(args.archive, args.guild_clean_plate, args.guild_native_background)
        if args.guild_clean_plate is None:
            print("WARNING: legacy opaque guild crops are not visually accepted; use both clean-plate inputs for the reviewed masked route.", file=sys.stderr)
        output.mkdir(parents=True, exist_ok=False)
        for name, data in outputs.items():
            destination = output / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(data)
    except (OSError, ValueError, KeyError, zipfile.BadZipFile) as error:
        parser.exit(1, f"Private magic-art import failed: {error}\n")
    print(f"Imported {len(outputs) - 2} private PNG assets; no game launched.")


if __name__ == "__main__":
    main()
