#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Import the user's private magic-art handoff into a NEW local resource overlay.

Only this recipe belongs in Git. The resulting pixels are private derivatives
of purchaser assets, not redistributable New Horizons content. No archive code
is executed, original installations are untouched, and an existing destination
is never overwritten. Requires Pillow.
"""
import argparse
import hashlib
import io
import json
from pathlib import Path, PurePosixPath
import stat
import zipfile

from PIL import Image


SCHOOLS = ("light", "nature", "sorcery", "havoc", "chaos", "shadow")
FACTIONS = ("castle", "rampart", "tower", "inferno", "necropolis", "dungeon",
            "stronghold", "fortress", "conflux")
CAST_ROOT = "casting-colors/Data/casting-glow-all-classes-v3/"
IMAGE_ROOT = "Mods/new-horizons/Images/"
RESOURCE_ROOT = "NH_magic_assets/"


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


def prepare(archive):
    """Validate first, then return an allowlisted output map without writing."""
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
        for faction in FACTIONS:
            # The exact rectangular patch, not the cutout, erases the old ribbon.
            # It stays private because it contains the surrounding original room.
            image = png(read(f"mage-guild-bookmarks/{faction}.png"), (122, 153))
            image = image.convert("RGBa").resize((width, height), Image.Resampling.LANCZOS).convert("RGBA")
            buffer = io.BytesIO()
            image.save(buffer, format="PNG")
            resource = f"{RESOURCE_ROOT}guild/{faction}.png"
            outputs[IMAGE_ROOT + resource] = buffer.getvalue()
            config["guildBooks"][faction] = {"image": resource, "position": [x, y]}
        outputs["config/newHorizonsMagicAssets.json"] = (json.dumps(config, indent=2) + "\n").encode()
        receipt = {"privateOnly": True, "castingVersion": 6,
                   "sourceArchiveSha256": sha(Path(archive).read_bytes()),
                   "sourcePixelsCommitted": False, "outputSha256": {
                       name: sha(data) for name, data in sorted(outputs.items())}}
        outputs["PRIVATE_MAGIC_ASSETS.json"] = (json.dumps(receipt, indent=2) + "\n").encode()
        return outputs


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True,
                        help="NEW private overlay directory outside tracked content")
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
        outputs = prepare(args.archive)
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
