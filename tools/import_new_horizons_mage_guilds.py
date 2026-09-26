#!/usr/bin/env python3
"""Reproduce runtime assets from the retained user-supplied Mage Guild archives.

PNG bytes are preserved. Only descriptor basepath separators are normalized:
VCMI concatenates basepath and frame filenames without inserting a slash.
Faction rules are deliberately not imported from these standalone mod packages.
"""
import json
from pathlib import Path, PurePosixPath
from zipfile import ZipFile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "assets/new-horizons/Mage Guilds"
CONTENT = ROOT / "Mods/new-horizons/Content"
PACKAGES = (
    "castle-mage-guild-level5",
    "fortress-mage-guild-v8",
    "stronghold-mage-guild-ridge-swap",
)


def assets():
    for package in PACKAGES:
        with ZipFile(SOURCES / (package + ".zip")) as archive:
            for name in archive.namelist():
                path = PurePosixPath(name)
                if path.is_absolute() or ".." in path.parts:
                    raise ValueError(f"Unsafe archive path: {name}")
                parts = path.parts
                if (name.endswith("/") or len(parts) < 4
                        or parts[:2] != (package, "Content")
                        or parts[2] not in ("data", "sprites")):
                    continue
                data = archive.read(name)
                if path.suffix.lower() == ".json":
                    descriptor = json.loads(data)
                    if descriptor.get("basepath"):
                        descriptor["basepath"] = descriptor["basepath"].rstrip("/") + "/"
                    data = (json.dumps(descriptor, indent=2) + "\n").encode()
                yield Path(*parts[2:]), data


if __name__ == "__main__":
    count = 0
    for relative, data in assets():
        destination = CONTENT / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        count += 1
    print(f"Imported {count} runtime assets; faction gameplay rules unchanged.")
