#!/usr/bin/env python3
"""Generate JSON-only Magi palette aliases from explicitly reviewed RGB maps.

The generator uses installed DEF names and frame-count metadata only. It never
opens or exports original sprite pixels. Supply a JSON object with exactly the
`archBattle`, `mageProjectile`, and `archPortrait` mappings, then direct the
three generated descriptors to a private output directory for review.
"""

import argparse
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[1]

# Read-only DEF census metadata. These counts select frames; no sprite pixels
# or palette values are inferred from them.
ARCH_MAGE_GROUP_FRAME_COUNTS = {
    0: 8,
    1: 11,
    2: 8,
    3: 6,
    4: 11,
    5: 8,
    7: 2,
    8: 2,
    9: 2,
    10: 2,
    11: 10,
    12: 10,
    13: 10,
    14: 13,
    15: 13,
    16: 13,
    20: 2,
    21: 2,
}
MAGE_PROJECTILE_GROUP_FRAME_COUNTS = {0: 9}
ARCH_MAGE_PORTRAIT_SOURCE_GROUP = 0
ARCH_MAGE_PORTRAIT_SOURCE_FRAME = 37

MAP_NAMES = ("archBattle", "mageProjectile", "archPortrait")
OUTPUT_NAMES = (
    "NH_ArchMageGrey.json",
    "NH_MageRedProjectile.json",
    "NH_ArchMageGreyPortrait.json",
)


def _unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate JSON object key: {key}")
        result[key] = value
    return result


def load_authored_maps(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as stream:
        value = json.load(stream, object_pairs_hook=_unique_object)
    if not isinstance(value, dict) or set(value) != set(MAP_NAMES):
        raise ValueError("maps JSON must contain exactly archBattle, mageProjectile, and archPortrait")
    return {name: validate_palette_map(value[name], name) for name in MAP_NAMES}


def validate_palette_map(value: object, name: str) -> dict[str, list[int]]:
    if not isinstance(value, dict) or not value:
        raise ValueError(f"{name} must be a non-empty palette-index to RGB object")

    result = {}
    for key, rgb in value.items():
        if not isinstance(key, str) or re.fullmatch(r"(?:[8-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])", key) is None:
            raise ValueError(f"{name} palette indices must be canonical decimal strings from 8 to 255")
        if not isinstance(rgb, list) or len(rgb) != 3:
            raise ValueError(f"{name}[{key}] must be an RGB list of exactly three integers")
        if any(type(channel) is not int or channel < 0 or channel > 255 for channel in rgb):
            raise ValueError(f"{name}[{key}] RGB channels must be integers from 0 to 255")
        result[key] = list(rgb)

    return {key: result[key] for key in sorted(result, key=int)}


def _frame_entry(alias_group: int, alias_frame: int, def_file: str, def_group: int, def_frame: int) -> dict:
    return {
        "group": alias_group,
        "frame": alias_frame,
        "defFile": def_file,
        "defGroup": def_group,
        "defFrame": def_frame,
    }


def build_descriptors(maps: dict) -> dict[str, dict]:
    if not isinstance(maps, dict) or set(maps) != set(MAP_NAMES):
        raise ValueError("palette maps must contain exactly archBattle, mageProjectile, and archPortrait")
    validated = {name: validate_palette_map(maps[name], name) for name in MAP_NAMES}

    arch_battle_frames = [
        _frame_entry(group, frame, "CAMAGE.DEF", group, frame)
        for group, count in ARCH_MAGE_GROUP_FRAME_COUNTS.items()
        for frame in range(count)
    ]
    mage_projectile_frames = [
        _frame_entry(0, frame, "PMAGEX.DEF", 0, frame)
        for frame in range(MAGE_PROJECTILE_GROUP_FRAME_COUNTS[0])
    ]
    portrait_frames = [
        _frame_entry(0, 0, "TWCRPORT.DEF", ARCH_MAGE_PORTRAIT_SOURCE_GROUP, ARCH_MAGE_PORTRAIT_SOURCE_FRAME)
    ]

    return {
        OUTPUT_NAMES[0]: {"paletteRemap": validated["archBattle"], "images": arch_battle_frames},
        OUTPUT_NAMES[1]: {"paletteRemap": validated["mageProjectile"], "images": mage_projectile_frames},
        OUTPUT_NAMES[2]: {"paletteRemap": validated["archPortrait"], "images": portrait_frames},
    }


def descriptor_bytes(descriptor: dict) -> bytes:
    return (json.dumps(descriptor, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _safe_output_directory(output_dir: Path, *, allow_runtime_read: bool = False) -> Path:
    requested = output_dir.absolute()
    resolved = output_dir.resolve()
    runtime_root = ROOT / "Mods"
    runtime_roots = (runtime_root.absolute(), runtime_root.resolve())
    targets_runtime = any(
        requested == root or root in requested.parents or resolved == root or root in resolved.parents
        for root in runtime_roots
    )
    if targets_runtime and not allow_runtime_read:
        raise ValueError("output directory must not be inside Mods; generate aliases to a private review directory")
    return resolved


def write_or_check(output_dir: Path, descriptors: dict[str, dict], check: bool) -> None:
    output_dir = _safe_output_directory(output_dir, allow_runtime_read=check)
    expected = {name: descriptor_bytes(value) for name, value in descriptors.items()}

    if check:
        missing = [name for name in expected if not (output_dir / name).is_file()]
        mismatched = [name for name, data in expected.items()
                      if (output_dir / name).is_file() and (output_dir / name).read_bytes() != data]
        if missing or mismatched:
            details = []
            if missing:
                details.append("missing: " + ", ".join(missing))
            if mismatched:
                details.append("mismatched: " + ", ".join(mismatched))
            raise ValueError("generated alias check failed (" + "; ".join(details) + ")")
        return

    output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in expected.items():
        target = output_dir / name
        if target.exists():
            if not target.is_file() or target.read_bytes() != data:
                raise ValueError(f"refusing to overwrite existing alias descriptor: {target}")
            continue
        target.write_bytes(data)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--maps", required=True, type=Path,
                        help="JSON file containing the three explicitly authored RGB maps")
    parser.add_argument("--output-dir", required=True, type=Path,
                        help="private directory for generated JSON descriptors; never a Mods directory")
    parser.add_argument("--check", action="store_true", help="verify exact existing descriptor bytes without writing")
    args = parser.parse_args(argv)

    try:
        maps = load_authored_maps(args.maps)
        descriptors = build_descriptors(maps)
        write_or_check(args.output_dir, descriptors, args.check)
    except (OSError, json.JSONDecodeError, ValueError) as error:
        parser.error(str(error))

    print(f"{'verified' if args.check else 'generated'} {len(descriptors)} Magi palette alias descriptors in {args.output_dir}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
