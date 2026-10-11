#!/usr/bin/env python3
"""Create a private, temporary Wisp gameplay/art overlay for the Linux preview.

The overlay is deliberately detached from Mods/new-horizons. It uses the
existing Psychic/Magic Elemental IDs as preview-only aliases so the supplied
Wisp art can be exercised before final creature rules are approved. The normal
module is read as input and never modified.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
BUILD_ROOT = ROOT / "build"
DEFAULT_PACKAGE = ROOT / "output/handoff-20261007/wisp/wisp-complete-handoff-v3"
DEFAULT_MOD_SOURCE = ROOT / "Mods/new-horizons"
DEFAULT_OUTPUT = BUILD_ROOT / "nh-wisp-preview-overlay"
HANDOFF_MANIFEST_SHA256 = "a600272fc868a2ff32e9176f3bd37d9f3c1ffbf6a3d27967b7c8e3835db9da9e"

SPRITE_SPECS = (
    ("Wisp.json", "base/art/Wisp.json", "base/art/Wisp"),
    ("WispMap.json", "base/art/WispMap.json", "base/art/Wisp/adventure/idle"),
    ("WispUpgrade.json", "upgraded/art/WispUpgrade.json", "upgraded/art/WispUpgrade"),
    (
        "WispUpgradeMap.json",
        "upgraded/art/WispUpgradeMap.json",
        "upgraded/art/WispUpgrade/adventure/idle",
    ),
)

PROJECTILE_SPECS = (
    ("WispProjectile.json", "base/projectile/WispProjectile.json", "base/projectile", "WispProjectile/"),
    (
        "WispUpgradeProjectile.json",
        "upgraded/projectile/WispUpgradeProjectile.json",
        "upgraded/projectile",
        "WispUpgradeProjectile/",
    ),
)

MISSILE_ANGLES = [90, 67.5, 45, 22.5, 0, -22.5, -45, -67.5, -90]

# Temporary low-cost Core placeholder, borrowed from the stock Gremlin line.
# These values are only to make the isolated art preview exercisable.
BORROWED_PROFILES = {
    "base": {
        "key": "core:psychicElemental",
        "name": {"singular": "Wisp (Preview)", "plural": "Wisps (Preview)"},
        "iconSmall": "NH_WispPreviewSmall.png",
        "iconLarge": "NH_WispPreview.png",
        "animation": "Wisp.def",
        "map": "WispMap.def",
        "projectile": "WispProjectile.def",
        "level": 1,
        "attack": 3,
        "defense": 3,
        "damage": {"min": 1, "max": 2},
        "hitPoints": 4,
        "speed": 4,
        "growth": 14,
        "cost": {"gold": 30},
        "fightValue": 55,
        "aiValue": 44,
        "leadership": 50,
        "sound": {
            "attack": "MAGICBLT.wav",
            "defend": "AGRMDFND.wav",
            "killed": "AGRMKILL.wav",
            "move": "AGRMMOVE.wav",
            "shoot": "MAGICBLT.wav",
            "wince": "AGRMWNCE.wav",
        },
        "upgrades": ["magicElemental"],
    },
    "upgrade": {
        "key": "core:magicElemental",
        "name": {"singular": "Wisp Upgrade (Preview)", "plural": "Wisp Upgrades (Preview)"},
        "iconSmall": "NH_WispUpgradePreviewSmall.png",
        "iconLarge": "NH_WispUpgradePreview.png",
        "animation": "WispUpgrade.def",
        "map": "WispUpgradeMap.def",
        "projectile": "WispUpgradeProjectile.def",
        "level": 1,
        "attack": 4,
        "defense": 4,
        "damage": {"min": 1, "max": 2},
        "hitPoints": 4,
        "speed": 5,
        "growth": 14,
        "cost": {"gold": 40},
        "fightValue": 55,
        "aiValue": 66,
        "leadership": 60,
        "sound": {
            "attack": "MAGICBLT.wav",
            "defend": "MGRMDFND.wav",
            "killed": "MGRMKILL.wav",
            "move": "MGRMMOVE.wav",
            "shoot": "MAGICBLT.wav",
            "wince": "MGRMWNCE.wav",
        },
        "upgrades": [],
    },
}


def _json_bytes(value):
    return (json.dumps(value, indent=2, ensure_ascii=False) + "\n").encode("utf-8")


def _read_json(path, label):
    if path.is_symlink() or not path.is_file():
        raise ValueError(f"{label} is missing or is a symlink: {path}")
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"could not parse {label}: {path}") from error
    if not isinstance(value, dict):
        raise ValueError(f"{label} must be a JSON object: {path}")
    return value


def _checked_relative(relative):
    path = Path(relative)
    if path.is_absolute() or ".." in path.parts or not path.parts:
        raise ValueError(f"unsafe relative path: {relative}")
    return path


def _load_handoff(package_root):
    if package_root.is_symlink() or not package_root.is_dir():
        raise ValueError("Wisp handoff must be an existing non-symlink directory")
    package_root = package_root.resolve()
    manifest_path = package_root / "HANDOFF_MANIFEST.json"
    if manifest_path.is_symlink() or not manifest_path.is_file():
        raise ValueError("Wisp handoff manifest is missing or is a symlink")
    manifest_bytes = manifest_path.read_bytes()
    actual_manifest_hash = hashlib.sha256(manifest_bytes).hexdigest()
    if actual_manifest_hash != HANDOFF_MANIFEST_SHA256:
        raise ValueError(
            "Wisp handoff manifest identity changed: expected "
            f"{HANDOFF_MANIFEST_SHA256}, got {actual_manifest_hash}"
        )
    manifest = json.loads(manifest_bytes)
    pins = manifest.get("files")
    if not isinstance(pins, dict):
        raise ValueError("Wisp handoff manifest has no file hash map")
    return package_root, pins, actual_manifest_hash


def _read_pinned(package_root, pins, relative):
    relative = _checked_relative(relative).as_posix()
    current = package_root
    for component in Path(relative).parts:
        current = current / component
        if current.is_symlink():
            raise ValueError(f"Wisp handoff source must not use symlinks: {relative}")
    try:
        current.resolve().relative_to(package_root)
    except ValueError as error:
        raise ValueError(f"Wisp handoff path escapes its root: {relative}") from error
    if not current.is_file():
        raise ValueError(f"required Wisp handoff file is missing: {relative}")
    pin = pins.get(relative)
    if not isinstance(pin, dict) or not isinstance(pin.get("sha256"), str):
        raise ValueError(f"Wisp handoff manifest has no hash for {relative}")
    data = current.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if digest != pin["sha256"] or len(data) != pin.get("bytes"):
        raise ValueError(f"Wisp handoff hash/size mismatch: {relative}")
    return data, digest


def _add_output(outputs, relative, data):
    relative = _checked_relative(relative).as_posix()
    if relative in outputs and outputs[relative] != data:
        raise ValueError(f"two Wisp resources target the same output path: {relative}")
    outputs[relative] = data


def _copy_sprite_descriptor(package_root, pins, outputs, descriptor_name, source_descriptor, source_asset_root):
    descriptor_bytes, _ = _read_pinned(package_root, pins, source_descriptor)
    try:
        descriptor = json.loads(descriptor_bytes)
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid Wisp sprite descriptor: {source_descriptor}") from error
    basepath = descriptor.get("basepath")
    sequences = descriptor.get("sequences")
    if not isinstance(basepath, str) or not isinstance(sequences, list) or not sequences:
        raise ValueError(f"invalid Wisp sprite descriptor shape: {source_descriptor}")
    if descriptor_name in {"Wisp.json", "WispUpgrade.json"}:
        if len(sequences) != 32:
            raise ValueError(f"{descriptor_name} must provide all 32 battle animation groups")
        for sequence in sequences:
            sequence["generateOverlay"] = 1
    output_base = _checked_relative(basepath)
    for sequence in sequences:
        frames = sequence.get("frames")
        if not isinstance(frames, list) or not frames:
            raise ValueError(f"empty Wisp animation sequence: {source_descriptor}")
        for frame in frames:
            frame_path = _checked_relative(frame)
            asset_rel = (Path(source_asset_root) / frame_path).as_posix()
            data, _ = _read_pinned(package_root, pins, asset_rel)
            output_image = Path("Images") / output_base / frame_path
            _add_output(outputs, output_image, data)
    _add_output(outputs, Path("Content/sprites") / descriptor_name, _json_bytes(descriptor))
    return descriptor


def _copy_projectile(package_root, pins, outputs, descriptor_name, source_descriptor, source_asset_root, output_basepath):
    descriptor_bytes, _ = _read_pinned(package_root, pins, source_descriptor)
    try:
        source_descriptor_data = json.loads(descriptor_bytes)
    except json.JSONDecodeError as error:
        raise ValueError(f"invalid Wisp projectile descriptor: {source_descriptor}") from error
    sequences = source_descriptor_data.get("sequences")
    if not isinstance(sequences, list):
        raise ValueError(f"invalid Wisp projectile descriptor: {source_descriptor}")
    group_ids = [sequence.get("group") for sequence in sequences]
    if group_ids != [0, 1, 2, 3]:
        raise ValueError(f"unexpected Wisp projectile source groups: {group_ids}")
    default_sequence = sequences[0]
    frames = default_sequence.get("frames")
    if not isinstance(frames, list) or len(frames) != 9:
        raise ValueError("Wisp default projectile must provide exactly nine angle frames")
    if any(not frame.startswith("bolt-phase-00-angle-") for frame in frames):
        raise ValueError("Wisp default projectile descriptor must select phase 00")

    # The engine reserves group 1 for generated mirror frames. Install only
    # group 0; keep all supplied phase PNGs intact in the immutable handoff.
    descriptor = {"sequences": [default_sequence], "basepath": output_basepath}
    for frame in frames:
        frame_path = _checked_relative(frame)
        data, _ = _read_pinned(package_root, pins, (Path(source_asset_root) / frame_path).as_posix())
        _add_output(outputs, Path("Images") / output_basepath / frame_path, data)
    _add_output(outputs, Path("Content/sprites") / descriptor_name, _json_bytes(descriptor))
    return descriptor


def _preview_creature(profile, *, attack_offset_x=31, y_offset=-29):
    return {
        "name": profile["name"],
        "level": profile["level"],
        "attack": profile["attack"],
        "defense": profile["defense"],
        "damage": profile["damage"],
        "hitPoints": profile["hitPoints"],
        "speed": profile["speed"],
        "growth": profile["growth"],
        "cost": profile["cost"],
        "fightValue": profile["fightValue"],
        "aiValue": profile["aiValue"],
        "shots": 8,
        "doubleWide": False,
        "upgrades": profile["upgrades"],
        # The key suffix requests whole-object replacement during VCMI's JSON
        # mod merge; it discards the aliased Elemental/Magic abilities.
        "abilities#override": {"shooter": {"type": "SHOOTER"}},
        "graphics": {
            "animation": profile["animation"],
            "map": profile["map"],
            "mapAttackFromLeft": profile["map"] + ":0:0",
            "mapAttackFromRight": profile["map"] + ":0:0",
            "iconSmall": profile["iconSmall"],
            "iconLarge": profile["iconLarge"],
            "animationTime": {"walk": 0.75, "attack": 1.0, "idle": 1.0},
            "missile": {
                "projectile": profile["projectile"],
                "frameAngles": MISSILE_ANGLES,
                "attackClimaxFrame": 3,
                # Derived provisional origin: BattleAnimationClasses starts
                # at (222,265) and applies (-25 + x,y); the authored core is
                # (196,236), so x=31,y=-29 starts 32px ahead at core height.
                "offset": {
                    "upperX": attack_offset_x,
                    "upperY": y_offset,
                    "middleX": attack_offset_x,
                    "middleY": y_offset,
                    "lowerX": attack_offset_x,
                    "lowerY": y_offset,
                },
            },
        },
        "sound": profile["sound"],
    }


def _build_configuration(mod_source):
    mod = _read_json(mod_source / "mod.json", "New Horizons module")
    creature_config = _read_json(
        mod_source / "Content/config/creatures/conflux.json", "Conflux creature override"
    )
    faction_config = _read_json(
        mod_source / "Content/config/factions/confluxCreatureRanks.json", "Conflux faction override"
    )

    creature_config["core:pixie"] = {"upgrades": ["sprite"]}
    creature_config[BORROWED_PROFILES["base"]["key"]] = _preview_creature(BORROWED_PROFILES["base"])
    creature_config[BORROWED_PROFILES["upgrade"]["key"]] = _preview_creature(BORROWED_PROFILES["upgrade"])

    conflux = faction_config["core:conflux"]["town"]
    # Keep the stock seven dwelling rows: Pixie -> Sprite is one Core line,
    # while row six now resolves through the two temporary Wisp aliases.
    conflux["creatures"] = {"modify@1": ["pixie", "sprite"]}
    conflux["hallSlots"]["modify@4"]["modify@1"] = ["dwellingLvl1", "dwellingUpLvl1"]
    conflux["hallSlots"].pop("modify@5", None)
    conflux["structures"].pop("dwellingLvl8", None)
    conflux["structures"].pop("dwellingUpLvl1", None)
    conflux["structures"].pop("horde1Upgr", None)
    conflux["buildings"].pop("dwellingLvl8", None)
    conflux["buildings"].pop("dwellingUpLvl1", None)
    conflux["buildings"].pop("horde1Upgr", None)
    # The previous custom Garden bonus targeted the now-removed eighth row.
    # Let the stock first-tier horde building data apply in this preview.
    conflux["buildings"].pop("horde1", None)

    settings = mod["settings"]
    categories = settings["creatures"]["newHorizonsCategories"]
    category_creatures = categories["creatures"]
    category_creatures[BORROWED_PROFILES["base"]["key"]] = "core"
    category_creatures[BORROWED_PROFILES["upgrade"]["key"]] = "core"

    growth_lines = categories["growthLines"]
    growth_lines["core:pixie"]["members"] = ["core:pixie", "core:sprite"]
    growth_lines.pop("core:sprite", None)
    wisp_line = growth_lines["core:psychicElemental"]
    wisp_line["weeklyBaseGrowth"] = BORROWED_PROFILES["base"]["growth"]
    wisp_line["members"] = ["core:psychicElemental", "core:magicElemental"]

    requirements = settings["heroes"]["newHorizonsCapabilities"]["leadership"]["creatureRequirements"]
    requirements["core:psychicElemental"] = BORROWED_PROFILES["base"]["leadership"]
    requirements["core:magicElemental"] = BORROWED_PROFILES["upgrade"]["leadership"]

    mod["name"] = "New Horizons — Wisp Preview"
    mod["description"] += (
        " TEMPORARY PRIVATE ART PREVIEW ONLY: core:psychicElemental and "
        "core:magicElemental are Wisp aliases with borrowed Gremlin/Master Gremlin "
        "combat profiles. Do not use existing saves or treat these values as final."
    )
    return {
        "mod.json": _json_bytes(mod),
        "Content/config/creatures/conflux.json": _json_bytes(creature_config),
        "Content/config/factions/confluxCreatureRanks.json": _json_bytes(faction_config),
    }


def build_overlay(package_root=DEFAULT_PACKAGE, mod_source=DEFAULT_MOD_SOURCE):
    package_root, pins, manifest_hash = _load_handoff(Path(package_root))
    outputs = {}
    for descriptor_name, source_descriptor, source_asset_root in SPRITE_SPECS:
        _copy_sprite_descriptor(
            package_root,
            pins,
            outputs,
            descriptor_name,
            source_descriptor,
            source_asset_root,
        )

    for descriptor_name, source_descriptor, source_asset_root, output_basepath in PROJECTILE_SPECS:
        _copy_projectile(
            package_root,
            pins,
            outputs,
            descriptor_name,
            source_descriptor,
            source_asset_root,
            output_basepath,
        )

    icon_sources = (
        ("base/art/Wisp/icons/wisp-icon-32.png", "NH_WispPreviewSmall.png"),
        ("base/art/Wisp/icons/wisp-icon-58x64.png", "NH_WispPreview.png"),
        ("upgraded/icons/icon-32x32.png", "NH_WispUpgradePreviewSmall.png"),
        ("upgraded/icons/icon-58x64.png", "NH_WispUpgradePreview.png"),
    )
    for source, destination in icon_sources:
        data, _ = _read_pinned(package_root, pins, source)
        _add_output(outputs, Path("Images") / destination, data)

    for relative, data in _build_configuration(Path(mod_source)).items():
        _add_output(outputs, relative, data)

    # The composer consumes this exact subdirectory and merges it into the
    # private candidate payload. Keep the repository's existing lower-case
    # `Content/sprites` spelling so no case-only duplicate descriptor appears.
    outputs = {f"Mods/new-horizons/{path}": data for path, data in outputs.items()}
    hashes = {path: hashlib.sha256(data).hexdigest() for path, data in sorted(outputs.items())}
    metadata = {
        "status": "Temporary isolated Linux art/gameplay preview; not final Wisp rules",
        "sourceHandoffManifestSha256": manifest_hash,
        "roster": {
            "coreLine1": ["core:pixie", "core:sprite"],
            "coreLine2PreviewAliases": ["core:psychicElemental", "core:magicElemental"],
            "eliteLines": [
                ["core:airElemental", "core:stormElemental"],
                ["core:waterElemental", "core:iceElemental"],
                ["core:fireElemental", "core:energyElemental"],
                ["core:earthElemental", "core:magmaElemental"],
            ],
            "championLine": ["core:firebird", "core:phoenix"],
            "psychicAndMagicElementalRecruitment": "not present; old IDs are Wisp aliases in this private overlay",
        },
        "borrowedPreviewProfile": {
            "base": "core:gremlin", "upgrade": "core:masterGremlin",
            "leadership": [50, 60], "shooter": True, "shots": 8,
            "flightOrElementalAbilities": False,
        },
        "projectiles": {
            "runtimeGroups": [0],
            "framesPerForm": 9,
            "unusedPhaseGroups": "retained unchanged in the source handoff; not installed",
            "offsetPixels": {"x": 31, "y": -29, "status": "derived provisional; renderer validation pending"},
        },
        "gameplayAndSaveWarning": "Use a fresh preview map/profile only; do not load existing saves or treat borrowed values as canonical.",
        "outputHashes": hashes,
    }
    return outputs, metadata


PREVIEW_README = """# Private Wisp preview overlay

This detached overlay is for the approved Linux art preview only. It does not
change the normal `Mods/new-horizons` tree. It temporarily maps
`core:psychicElemental` to the supplied base Wisp and
`core:magicElemental` to the supplied upgraded Wisp. The preview borrows the
stock Gremlin/Master Gremlin Core stat, shooter, and Leadership profile;
both attack modes use the Magic Arrow sound, not the borrowed Gremlin sounds.
the borrowed values are not approved final creature rules. Do not load an
existing save with this overlay.

Roster in the overlay: Pixie -> Sprite as Core line 1; Wisp aliases as Core line
2; Air/Water/Fire/Earth Elemental lines as Elite; Phoenix as Champion. The town
still has its seven normal dwelling rows: use Conflux dwelling 6 to recruit the
base Wisp and its upgrade dwelling for the upgraded Wisp. The aliases mean old
Psychic/Magic Elemental identities are intentionally not available as such in
this isolated preview.

The Wisp shooter uses the supplied phase-00 projectile, group 0 only (the
engine reserves group 1 for generated mirror frames). The provisional projectile
origin is derived from the authored core anchor and the renderer's default
origin; it still needs rendered validation. Unused projectile phases remain in
the handoff source and are not installed by the overlay.

Build with:

```sh
python3 tools/build_new_horizons_wisp_preview.py
```

Pass `--overlay build/nh-wisp-preview-overlay` to the private creature-preview
composer. Use a fresh preview map/profile, not a normal save.
"""


def _safe_output(output_root, *, must_exist=False):
    output_root = Path(output_root)
    if BUILD_ROOT.is_symlink():
        raise ValueError("repository build directory must not be a symlink")
    if output_root.is_symlink():
        raise ValueError("preview output root must not be a symlink")
    if must_exist and not output_root.is_dir():
        raise ValueError("preview output does not exist for check mode")
    build_root = BUILD_ROOT.resolve()
    candidate = output_root.resolve(strict=False)
    try:
        candidate.relative_to(build_root)
    except ValueError as error:
        raise ValueError("preview output must stay below the repository build directory") from error
    if output_root.exists() and not must_exist:
        raise ValueError("refusing to overwrite an existing preview output")
    return candidate


def write_overlay(package_root=DEFAULT_PACKAGE, output_root=DEFAULT_OUTPUT, *, check=False):
    output_root = _safe_output(output_root, must_exist=check)
    outputs, metadata = build_overlay(package_root)
    readme_data = PREVIEW_README.encode("utf-8")
    manifest_data = _json_bytes(metadata)
    expected = {Path(path): data for path, data in outputs.items()}
    expected[Path("PREVIEW-README.md")] = readme_data
    expected[Path("candidate-manifest.json")] = manifest_data

    if check:
        actual_files = {
            path.relative_to(output_root): path.read_bytes()
            for path in output_root.rglob("*")
            if path.is_file() and not path.is_symlink()
        }
        if actual_files != expected:
            raise ValueError("preview output differs from deterministic Wisp overlay")
        return len(expected)

    output_root.mkdir(parents=True)
    for relative, data in expected.items():
        destination = output_root / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
    return len(expected)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--package", type=Path, default=DEFAULT_PACKAGE)
    parser.add_argument("--output", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--check", action="store_true", help="verify an existing deterministic output")
    args = parser.parse_args(argv)
    print(f"Wrote/verified {write_overlay(args.package, args.output, check=args.check)} private overlay files")


if __name__ == "__main__":
    main()
