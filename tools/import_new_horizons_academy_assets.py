#!/usr/bin/env python3
"""Import the reviewed Academy art handoff without importing original game pixels.

The archive is treated as data only. This importer validates ZIP paths and file
types, copies the authored masters/provenance and approved native exports, and
mechanically creates the registered PNG/JSON runtime assets. It deliberately
omits the package's original-color gate, map-shadow, and built-badge composites.

Usage:
    python3 tools/import_new_horizons_academy_assets.py --archive /path/to/handoff.zip
    python3 tools/import_new_horizons_academy_assets.py --archive /path/to/handoff.zip --check

The generated all-built town comparison is written under ignored build/ and is
not a runtime acceptance test.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import stat
import sys
import zipfile
from pathlib import Path, PurePosixPath

try:
    from PIL import Image
except ImportError as error:  # pragma: no cover - environment diagnostic
    raise SystemExit("Pillow is required for mechanical canvas registration") from error


ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOT = Path("assets/new-horizons/academy")
IMAGE_ROOT = Path("Mods/new-horizons/Images")
ICON_REVISION_ROOT = SOURCE_ROOT / "icon-revisions/v2"
ICON_REVISION_MANIFEST = ICON_REVISION_ROOT / "manifest.json"
# Filled only after the reviewed v2 manifest and all four exports are installed.
# A non-hash sentinel intentionally makes import/check fail closed in the meantime.
APPROVED_ICON_REVISION_MANIFEST_SHA256 = "03bd00c50cdb07b488764512a81cf3e1a2fabb0510b70b5660374ca912b46775"

PROVENANCE_FILES = (
    "README.md",
    "PACKAGE-MANIFEST.json",
    "VALIDATION.json",
    "integration/VCMI-CHECKLIST.md",
    "integration/academy-assets.json",
    "integration/export-validation.json",
    "integration/icon-mapping.json",
    "integration/prompts.json",
    "integration/puzzle-layout.json",
    "integration/town-layout.json",
    "integration/town-validation.json",
)

# These exports contain or explicitly retain source-game color pixels. Keep
# them out of both the source tree and the installed module. Their consumers
# use external original resources, or generated-only runtime composition.
EXCLUDED_NATIVE = {
    "native/adventure/avctowr0.png",
    "native/adventure/avctowx0.png",
    "native/adventure/avctowz0.png",
    "native/siege/sgtwdrw1.png",
    "native/siege/sgtwdrw2.png",
    "native/siege/sgtwdrw3.png",
    "native/siege/sgtwdrwc.png",
    "native/ui/icons/fort-large-built.png",
    "native/ui/icons/fort-small-built.png",
    "native/ui/icons/village-large-built.png",
    "native/ui/icons/village-small-built.png",
}

MAP_BODY_REGISTRATIONS = {
    "masters/adventure/avctowr0.png": {
        "resource": "NH_ACADEMY_VILLAGE_BODY",
        "png": "NH_academy_village_body.png",
        "original": "AVCTOWR0",
        "templates": ("village",),
    },
    "masters/adventure/avctowx0.png": {
        "resource": "NH_ACADEMY_FORT_BODY",
        "png": "NH_academy_fort_body.png",
        "original": "AVCTOWX0",
        "templates": ("fort", "citadel", "castle"),
    },
    "masters/adventure/avctowz0.png": {
        "resource": "NH_ACADEMY_CAPITOL_BODY",
        "png": "NH_academy_capitol_body.png",
        "original": "AVCTOWZ0",
        "templates": ("capitol",),
    },
}

ICON_NORMALS = {
    "native/ui/icons/fort-large-normal.png": "NH_academy_fort_large_normal.png",
    "native/ui/icons/fort-small-normal.png": "NH_academy_fort_small_normal.png",
    "native/ui/icons/village-large-normal.png": "NH_academy_village_large_normal.png",
    "native/ui/icons/village-small-normal.png": "NH_academy_village_small_normal.png",
}

ICON_REVISION_MASTERS = {
    "fort": "masters/fort.png",
    "village": "masters/village.png",
}

ICON_REVISION_SLOTS = {
    "village.large.normal": {
        "source": "native/ui/icons/village-large-normal.png",
        "master": "village",
        "export": "exports/NH_academy_village_large_normal.png",
        "runtime": "NH_academy_village_large_normal.png",
        "dimensions": [58, 64],
    },
    "village.small.normal": {
        "source": "native/ui/icons/village-small-normal.png",
        "master": "village",
        "export": "exports/NH_academy_village_small_normal.png",
        "runtime": "NH_academy_village_small_normal.png",
        "dimensions": [48, 32],
    },
    "fort.large.normal": {
        "source": "native/ui/icons/fort-large-normal.png",
        "master": "fort",
        "export": "exports/NH_academy_fort_large_normal.png",
        "runtime": "NH_academy_fort_large_normal.png",
        "dimensions": [58, 64],
    },
    "fort.small.normal": {
        "source": "native/ui/icons/fort-small-normal.png",
        "master": "fort",
        "export": "exports/NH_academy_fort_small_normal.png",
        "runtime": "NH_academy_fort_small_normal.png",
        "dimensions": [48, 32],
    },
}

SEMANTIC_STRUCTURE_ASSETS = {
    # New Horizons keeps Genie at creature level 4 and Mage at level 5, while
    # preserving the art's original on-screen positions.
    "dwellingLvl4": {"resource": "NH_ACADEMY_TBTWDW_4", "x": 511, "y": 75, "bonus": "botgen1.png"},
    "dwellingLvl5": {"resource": "NH_ACADEMY_TBTWDW_3", "x": 613, "y": 95, "bonus": "botmag1.png"},
    "dwellingUpLvl4": {"resource": "NH_ACADEMY_TBTWUP_4", "x": 511, "y": 8, "bonus": "botgen2.png"},
    "dwellingUpLvl5": {"resource": "NH_ACADEMY_TBTWUP_3", "x": 613, "y": 74, "bonus": "botmag2.png"},
}


def compact_json(value: object) -> bytes:
    return (json.dumps(value, indent="\t", ensure_ascii=False) + "\n").encode("utf-8")


def sha256_hex(payload: bytes) -> str:
    return hashlib.sha256(payload).hexdigest()


def _reject_duplicate_json_keys(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON key in Academy icon revision manifest: {key!r}")
        result[key] = value
    return result


def _revision_file(root: Path, relative: str) -> Path:
    if not isinstance(relative, str) or "\\" in relative:
        raise ValueError(f"Unsafe Academy icon revision path: {relative!r}")
    path = PurePosixPath(relative)
    if path.is_absolute() or path.anchor or not path.parts or any(part in ("", ".", "..") for part in path.parts):
        raise ValueError(f"Unsafe Academy icon revision path: {relative!r}")

    base = root / ICON_REVISION_ROOT
    if base.is_symlink():
        raise ValueError("Symlinks are not accepted for the Academy icon revision directory")
    candidate = base.joinpath(*path.parts)
    current = base
    for part in path.parts:
        current = current / part
        if current.is_symlink():
            raise ValueError(f"Symlinks are not accepted in Academy icon revision files: {relative}")
    try:
        candidate.resolve(strict=True).relative_to(base.resolve(strict=True))
    except (OSError, ValueError) as error:
        raise ValueError(f"Academy icon revision file is missing or escapes its revision directory: {relative}") from error
    if not candidate.is_file():
        raise ValueError(f"Academy icon revision path is not a file: {relative}")
    return candidate


def load_icon_revision(root: Path, expected_manifest_sha256: str) -> dict:
    """Load only the explicitly pinned Academy icon revision and exact exports."""
    source_routes = {
        record["source"]: record["runtime"]
        for record in ICON_REVISION_SLOTS.values()
    }
    if source_routes != ICON_NORMALS:
        raise RuntimeError("Academy icon revision slots drifted from the importer source/runtime mapping")
    if not re.fullmatch(r"[0-9a-f]{64}", expected_manifest_sha256 or ""):
        raise RuntimeError("Academy v2 icon revision is not enabled: importer manifest SHA-256 pin is unset")

    manifest_path = _revision_file(root, "manifest.json")
    raw_manifest = manifest_path.read_bytes()
    actual_manifest_sha256 = sha256_hex(raw_manifest)
    if actual_manifest_sha256 != expected_manifest_sha256:
        raise RuntimeError(
            "Academy v2 icon revision manifest changed: "
            f"expected {expected_manifest_sha256}, got {actual_manifest_sha256}"
        )

    try:
        manifest = json.loads(raw_manifest.decode("utf-8"), object_pairs_hook=_reject_duplicate_json_keys)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise ValueError(f"Academy icon revision manifest is not valid UTF-8 JSON: {manifest_path}") from error
    if not isinstance(manifest, dict) or set(manifest) != {
        "schemaVersion", "revision", "builtFallbackPolicy", "masters", "icons", "provenance"
    }:
        raise ValueError("Academy icon revision manifest has unexpected top-level fields")
    if type(manifest["schemaVersion"]) is not int or manifest["schemaVersion"] != 1 or manifest["revision"] != "v2":
        raise ValueError("Academy icon revision manifest must declare schemaVersion 1 and revision v2")
    if manifest["builtFallbackPolicy"] != "byte-identical-to-active-normal":
        raise ValueError("Academy built-icon fallbacks must remain byte-identical to their normal icons")

    masters = manifest["masters"]
    if not isinstance(masters, dict) or set(masters) != set(ICON_REVISION_MASTERS):
        raise ValueError("Academy icon revision must register exactly the fort and village masters")
    for name, expected_path in ICON_REVISION_MASTERS.items():
        record = masters[name]
        if not isinstance(record, dict) or set(record) != {"path", "sha256"} or record["path"] != expected_path:
            raise ValueError(f"Academy icon revision has an invalid {name} master record")
        if not isinstance(record["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", record["sha256"]):
            raise ValueError(f"Academy icon revision has an invalid {name} master hash")
        master_path = _revision_file(root, record["path"])
        master_bytes = master_path.read_bytes()
        if sha256_hex(master_bytes) != record["sha256"]:
            raise ValueError(f"Academy icon revision master bytes do not match the manifest: {record['path']}")
        try:
            with Image.open(master_path) as image:
                image.verify()
        except OSError as error:
            raise ValueError(f"Academy icon revision master is not a valid image: {record['path']}") from error

    provenance = manifest["provenance"]
    if not isinstance(provenance, dict) or set(provenance) != {"prompt", "sha256"} or provenance["prompt"] != "PROMPT.md":
        raise ValueError("Academy icon revision must retain PROMPT.md provenance")
    if not isinstance(provenance["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", provenance["sha256"]):
        raise ValueError("Academy icon revision has an invalid prompt provenance hash")
    prompt_bytes = _revision_file(root, provenance["prompt"]).read_bytes()
    if sha256_hex(prompt_bytes) != provenance["sha256"]:
        raise ValueError("Academy icon revision prompt provenance does not match the manifest")

    icons = manifest["icons"]
    if not isinstance(icons, dict) or set(icons) != set(ICON_REVISION_SLOTS):
        raise ValueError("Academy icon revision must register exactly the four normal town-icon slots")
    exports_by_runtime = {}
    for slot, expected in ICON_REVISION_SLOTS.items():
        record = icons[slot]
        if not isinstance(record, dict) or set(record) != {
            "source", "master", "export", "runtime", "dimensions", "sha256"
        }:
            raise ValueError(f"Academy icon revision has an invalid {slot} record")
        for key in ("source", "master", "export", "runtime", "dimensions"):
            if record[key] != expected[key]:
                raise ValueError(f"Academy icon revision changed the approved {slot} {key} mapping")
        if not isinstance(record["sha256"], str) or not re.fullmatch(r"[0-9a-f]{64}", record["sha256"]):
            raise ValueError(f"Academy icon revision has an invalid {slot} export hash")
        export_path = _revision_file(root, record["export"])
        export_bytes = export_path.read_bytes()
        if sha256_hex(export_bytes) != record["sha256"]:
            raise ValueError(f"Academy icon revision export bytes do not match the manifest: {record['export']}")
        try:
            with Image.open(export_path) as image:
                if list(image.size) != expected["dimensions"]:
                    raise ValueError(
                        f"Academy icon revision export has wrong native dimensions: {record['export']} "
                        f"({image.width}x{image.height})"
                    )
                image.verify()
        except OSError as error:
            raise ValueError(f"Academy icon revision export is not a valid image: {record['export']}") from error
        exports_by_runtime[record["runtime"]] = export_bytes

    return {
        "manifest": manifest,
        "manifest_sha256": actual_manifest_sha256,
        "exports_by_runtime": exports_by_runtime,
    }


def validate_archive(archive: zipfile.ZipFile) -> dict[str, zipfile.ZipInfo]:
    result: dict[str, zipfile.ZipInfo] = {}
    for info in archive.infolist():
        raw = info.filename
        if "\\" in raw or raw.startswith("/"):
            raise ValueError(f"Unsafe ZIP member path: {raw!r}")
        path = PurePosixPath(raw)
        if any(part in ("", ".", "..") for part in path.parts):
            raise ValueError(f"Unsafe ZIP member path: {raw!r}")
        if path.is_absolute() or path.anchor:
            raise ValueError(f"Absolute ZIP member path: {raw!r}")
        mode = (info.external_attr >> 16) & 0xFFFF
        if stat.S_ISLNK(mode):
            raise ValueError(f"ZIP symlinks are not accepted: {raw!r}")
        if info.is_dir():
            continue
        if raw in result:
            raise ValueError(f"Duplicate ZIP member path: {raw!r}")
        result[raw] = info

    required = {"integration/academy-assets.json", "integration/town-layout.json"}
    missing = sorted(required - result.keys())
    if missing:
        raise ValueError(f"Archive is missing required registration metadata: {missing}")

    roots = {PurePosixPath(name).parts[0] for name in result}
    if not roots <= {"README.md", "PREVIEW.html", "PACKAGE-MANIFEST.json", "VALIDATION.json", "masters", "native", "integration", "previews"}:
        raise ValueError(f"Unexpected top-level archive entries: {sorted(roots)}")
    return result


def archive_json(archive: zipfile.ZipFile, names: dict[str, zipfile.ZipInfo], path: str):
    return json.loads(archive.read(names[path]).decode("utf-8"))


_JSONC_COMMENT = re.compile(r'"(?:\\.|[^"\\])*"|//[^\r\n]*|/\*[\s\S]*?\*/')
_JSONC_TRAILING_COMMA = re.compile(r'"(?:\\.|[^"\\])*"|,(?=\s*[}\]])')


def load_jsonc(path: Path):
    """Read existing JSON-with-comments config without altering string contents."""
    source = path.read_text(encoding="utf-8")
    source = _JSONC_COMMENT.sub(lambda match: match.group(0) if match.group(0).startswith('"') else "", source)
    source = _JSONC_TRAILING_COMMA.sub(lambda match: match.group(0) if match.group(0).startswith('"') else "", source)
    return json.loads(source)


def safe_write(root: Path, relative: str | Path, payload: bytes, check_only: bool, *, replace_vanilla_hall_alias: bool = False):
    destination = root / relative
    if destination.is_file():
        old = destination.read_bytes()
        if old == payload:
            return
        if replace_vanilla_hall_alias:
            try:
                current = json.loads(old.decode("utf-8"))
                images = current["images"]
                is_expected = (
                    len(images) == 44
                    and [entry.get("frame") for entry in images] == list(range(44))
                    and all(entry.get("defFile", "").upper() == "HALLTOWR.DEF" for entry in images)
                )
            except (UnicodeDecodeError, json.JSONDecodeError, KeyError, TypeError):
                is_expected = False
            if not is_expected:
                raise RuntimeError(f"Refusing to replace unexpected existing file: {destination}")
            if check_only:
                raise RuntimeError(f"Generated file is stale: {destination}")
        else:
            raise RuntimeError(f"Refusing to overwrite changed file: {destination}")
    elif check_only:
        raise RuntimeError(f"Expected generated file is missing: {destination}")

    if not check_only:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)


def install_curated_icon(root: Path, runtime_name: str, payload: bytes, legacy_package_bytes: bytes, check_only: bool):
    """Install a pinned icon export, accepting only the exact prior package bytes."""
    if Path(runtime_name).name != runtime_name or not runtime_name.endswith("_normal.png"):
        raise ValueError(f"Unexpected Academy normal icon runtime name: {runtime_name}")
    destination = root / IMAGE_ROOT / runtime_name
    built_name = runtime_name.replace("_normal.png", "_built.png")
    built_destination = root / IMAGE_ROOT / built_name

    def preflight(path: Path, label: str) -> bool:
        if path.is_symlink():
            raise RuntimeError(f"Refusing to replace symlinked Academy {label}: {path}")
        if not path.exists():
            if check_only:
                raise RuntimeError(f"Reviewed Academy {label} is missing: {path}")
            return True
        if not path.is_file():
            raise RuntimeError(f"Academy {label} destination is not a regular file: {path}")
        current = path.read_bytes()
        if current == payload:
            return False
        if current != legacy_package_bytes:
            raise RuntimeError(f"Refusing to replace unrecognized Academy {label} pixels: {path}")
        if check_only:
            raise RuntimeError(f"Reviewed Academy {label} is not installed: {path}")
        return True

    # Validate both active names before writing either, so an unexpected
    # built fallback cannot leave only the normal path updated.
    write_normal = preflight(destination, "icon")
    write_built = preflight(built_destination, "built-icon fallback")
    if write_normal:
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(payload)
    if write_built:
        built_destination.parent.mkdir(parents=True, exist_ok=True)
        built_destination.write_bytes(payload)


def aliased_animation(resource: str, image_path: str, frame_count: int = 1) -> bytes:
    if frame_count < 1:
        raise ValueError(f"Animation {resource} has invalid frame count {frame_count}")
    return compact_json({
        "images": [
            {"group": 0, "frame": frame, "file": image_path}
            for frame in range(frame_count)
        ]
    })


def academy_patch(root: Path, core_tower: dict, rank_patch: dict) -> dict:
    patch_structures = {}
    core_structures = core_tower["tower"]["town"]["structures"]
    ranks_structures = rank_patch["core:tower"]["town"].get("structures", {})
    bonus_files = {path.stem.lower() for path in (root / "assets/new-horizons/academy/native/ui/bonus").glob("*.png")}

    for name, core_structure in core_structures.items():
        ranked = ranks_structures.get(name, {})
        effective = {**core_structure, **ranked}
        if name == "mageGuild5":
            animation = "NH_ACADEMY_GUILD5_TOP"
        elif name == "special4":
            animation = "NH_ACADEMY_SPECIAL4_LOWER"
        else:
            resource = Path(effective["animation"]).stem.upper()
            animation = f"NH_ACADEMY_{resource}"

        structure = {"animation": animation}
        bonus_stem = Path(effective["campaignBonus"]).stem.lower()
        if bonus_stem not in bonus_files:
            raise ValueError(f"Missing supplied campaign bonus illustration for {name}: {bonus_stem}")
        structure["campaignBonus"] = f"NH_academy/ui/bonus/{bonus_stem}.png"

        if name in SEMANTIC_STRUCTURE_ASSETS:
            semantic = SEMANTIC_STRUCTURE_ASSETS[name]
            structure["animation"] = semantic["resource"]
            structure["x"] = semantic["x"]
            structure["y"] = semantic["y"]
            structure["campaignBonus"] = f"NH_academy/ui/bonus/{semantic['bonus']}"
        elif name == "mageGuild5":
            structure["x"] = 592
            structure["y"] = 14

        image_path = animation_image_path(root, animation)
        structure["area"] = f"NH_academy/town/masks/{name}-area.png"
        structure["border"] = f"NH_academy/town/masks/{name}-border.png"
        structure["_generatedImagePath"] = image_path

        patch_structures[name] = structure

    patch_structures["academyRoof"] = {
        "animation": "NH_ACADEMY_TOWN_ROOF",
        "x": 665,
        "y": 255,
        "z": 4,
    }

    map_templates = {
        template: {"animation": definition["resource"]}
        for definition in MAP_BODY_REGISTRATIONS.values()
        for template in definition["templates"]
    }

    # The entrance/blocking geometry and remaining template data are not
    # touched; only each template's visual animation is changed.
    return {
        "core:tower": {
            "name": "Academy",
            "nativeTerrain#override": ["sand"],
            "creatureBackground": {
                "120px": "NH_academy/ui/tpcastow.png",
                "130px": "NH_academy/ui/crbkgtow.png",
            },
            "puzzleMap": {"prefix": "NH_academy/ui/puzzle/puztow"},
            "town": {
                "mapObject": {"templates": map_templates},
                "structures": patch_structures,
                "icons": {
                    "village": {
                        "normal": {
                            "large": "NH_academy_village_large_normal.png",
                            "small": "NH_academy_village_small_normal.png",
                        },
                        "built": {
                            "large": "NH_academy_village_large_built.png",
                            "small": "NH_academy_village_small_built.png",
                        },
                    },
                    "fort": {
                        "normal": {
                            "large": "NH_academy_fort_large_normal.png",
                            "small": "NH_academy_fort_small_normal.png",
                        },
                        "built": {
                            "large": "NH_academy_fort_large_built.png",
                            "small": "NH_academy_fort_small_built.png",
                        },
                    },
                },
                "townBackground": "NH_academy/town/landscape.png",
                "guildWindow": ["NH_academy/ui/tpmagetw.png"],
                "buildingsIcons": "NH_tower_buildings",
            },
        }
    }


def animation_image_path(root: Path, resource: str) -> str:
    descriptor_path = root / IMAGE_ROOT / f"{resource}.json"
    descriptor = json.loads(descriptor_path.read_text(encoding="utf-8"))
    images = descriptor.get("images", [])
    if not images or not images[0].get("file"):
        raise ValueError(f"Animation {resource} has no registered image file")
    image_path = PurePosixPath(images[0]["file"])
    if image_path.is_absolute() or any(part in ("", ".", "..") for part in image_path.parts):
        raise ValueError(f"Unsafe image path in {descriptor_path}: {image_path}")
    if not resolve_case_insensitive(root / IMAGE_ROOT, image_path):
        raise ValueError(f"Animation {resource} references missing image {image_path}")
    return image_path.as_posix()


def resolve_case_insensitive(base: Path, relative: PurePosixPath) -> Path | None:
    if relative.is_absolute() or any(part in ("", ".", "..") for part in relative.parts):
        return None
    current = base
    for part in relative.parts:
        if (current / part).exists():
            current = current / part
            continue
        if not current.is_dir():
            return None
        match = next((entry for entry in current.iterdir() if entry.name.casefold() == part.casefold()), None)
        if match is None:
            return None
        current = match
    return current if current.is_file() else None


def validate_runtime_routes(root: Path, archive: zipfile.ZipFile, names: dict[str, zipfile.ZipInfo], icon_revision: dict):
    patch = json.loads((root / "Mods/new-horizons/Content/config/factions/academyArt.json").read_text(encoding="utf-8"))
    faction = patch["core:tower"]
    town = faction["town"]
    structures = town["structures"]

    for structure_name, structure in structures.items():
        if structure_name == "academyRoof":
            animation_image_path(root, structure["animation"])
            continue
        for field in ("area", "border", "campaignBonus"):
            image_path = PurePosixPath(structure[field])
            if not resolve_case_insensitive(root / IMAGE_ROOT, image_path):
                raise ValueError(f"{structure_name}.{field} references missing image {image_path}")
        animation_image_path(root, structure["animation"])

    for template, definition in town["mapObject"]["templates"].items():
        animation_image_path(root, definition["animation"])

    for group_name, group in town["icons"].items():
        for size in ("large", "small"):
            normal = group["normal"][size]
            slot = f"{group_name}.{size}.normal"
            expected = ICON_REVISION_SLOTS.get(slot)
            if expected is None or normal != expected["runtime"]:
                raise ValueError(f"Academy normal icon route differs from the reviewed v2 slot: {slot} -> {normal}")
            normal_path = resolve_case_insensitive(root / IMAGE_ROOT, PurePosixPath(normal))
            if not normal_path:
                raise ValueError(f"Missing normal town icon {normal}")
            reviewed_bytes = icon_revision["exports_by_runtime"][normal]
            if normal_path.read_bytes() != reviewed_bytes:
                raise ValueError(f"Installed Academy icon differs from its reviewed v2 export: {normal}")
            built_fallback = group["built"][size]
            fallback = resolve_case_insensitive(root / IMAGE_ROOT, PurePosixPath(built_fallback))
            if not fallback:
                raise ValueError(f"Missing generated-only built icon fallback {built_fallback}")
            expected_built = normal.replace("_normal.png", "_built.png")
            if built_fallback != expected_built:
                raise ValueError(f"Academy built fallback route differs from its normal icon: {built_fallback}")
            if fallback.read_bytes() != reviewed_bytes:
                raise ValueError(f"Built icon fallback must exactly match the reviewed safe normal export: {built_fallback}")

    siege_assets = {
        name for name in names
        if name.startswith("native/siege/") and name.endswith(".png") and name not in EXCLUDED_NATIVE
    }
    for native in siege_assets:
        resource = Path(native).stem.upper()
        image = root / IMAGE_ROOT / f"{resource}.png"
        if not image.is_file():
            raise ValueError(f"Siege ImagePath override is missing direct PNG resource {resource}.png")
        if (root / IMAGE_ROOT / f"{resource}.json").exists():
            raise ValueError(f"Siege ImagePath {resource} must not rely on an animation JSON alias")

    excluded_gate_stems = {Path(path).stem.upper() for path in EXCLUDED_NATIVE if path.startswith("native/siege/")}
    for gate in excluded_gate_stems:
        if (root / IMAGE_ROOT / f"{gate}.png").exists():
            raise ValueError(f"Original gate image must remain external: {gate}.png")
    if faction.get("name") != "Academy" or faction.get("nativeTerrain#override") != ["sand"]:
        raise ValueError("Academy name and user-confirmed sand terrain override must be present")


def write_structure_masks(root: Path, structures: dict, check_only: bool):
    from PIL import ImageChops, ImageFilter

    for name, structure in structures.items():
        if name == "academyRoof":
            continue
        image_path = structure.pop("_generatedImagePath")
        source = Image.open(root / IMAGE_ROOT / Path(*PurePosixPath(image_path).parts)).convert("RGBA")
        alpha = source.getchannel("A")
        area = Image.new("RGBA", source.size, (255, 255, 255, 0))
        area.putalpha(alpha)
        dilated = alpha.filter(ImageFilter.MaxFilter(5))
        border_alpha = ImageChops.subtract(dilated, alpha)
        border = Image.new("RGBA", source.size, (255, 220, 64, 0))
        border.putalpha(border_alpha)
        for mask_type, image in (("area", area), ("border", border)):
            from io import BytesIO
            buffer = BytesIO()
            image.save(buffer, format="PNG", optimize=False)
            safe_write(
                root,
                IMAGE_ROOT / "NH_academy/town/masks" / f"{name}-{mask_type}.png",
                buffer.getvalue(),
                check_only,
            )


def map_body_png(archive: zipfile.ZipFile, names: dict[str, zipfile.ZipInfo], assets: list[dict], master_name: str) -> bytes:
    registration = next((entry for entry in assets if entry.get("master") == master_name), None)
    if registration is None:
        raise ValueError(f"No registration record for generated map master {master_name}")

    master = Image.open(archive.open(names[master_name])).convert("RGBA")
    expected_master = registration.get("masterSize")
    if expected_master and list(master.size) != expected_master:
        raise ValueError(f"Unexpected master size for {master_name}: {master.size}, expected {expected_master}")
    master_box = registration["masterSolidBox"]
    source_box = registration["sourceSolidBox"]
    canvas_size = (registration["width"], registration["height"])

    crop = master.crop((
        master_box["left"],
        master_box["top"],
        master_box["left"] + master_box["width"],
        master_box["top"] + master_box["height"],
    ))
    fitted = crop.resize((source_box["width"], source_box["height"]), Image.Resampling.LANCZOS)
    canvas = Image.new("RGBA", canvas_size, (0, 0, 0, 0))
    canvas.alpha_composite(fitted, (source_box["left"], source_box["top"]))
    if list(canvas.size) != [192, 192]:
        raise ValueError(f"Map body export must remain a 192x192 native canvas: {master_name}")
    from io import BytesIO
    buffer = BytesIO()
    canvas.save(buffer, format="PNG", optimize=False)
    return buffer.getvalue()


def split_guild_wall(archive: zipfile.ZipFile, names: dict[str, zipfile.ZipInfo]) -> tuple[bytes, bytes]:
    from io import BytesIO
    composite = Image.open(archive.open(names["native/town/corrections/guild-and-wall.png"])).convert("RGBA")
    if composite.size != (179, 220):
        raise ValueError(f"Guild-and-wall native image must be 179x220, got {composite.size}")
    split_y = 175  # lowerWallRedrawY 189 - authored placement y=14
    outputs = []
    for box in ((0, 0, composite.width, split_y), (0, split_y, composite.width, composite.height)):
        part = composite.crop(box)
        buffer = BytesIO()
        part.save(buffer, format="PNG", optimize=False)
        outputs.append(buffer.getvalue())
    return outputs[0], outputs[1]


def expected_hall_alias() -> bytes:
    source_frame = {frame: frame for frame in range(44)}
    for left, right in ((33, 34), (40, 41)):
        source_frame[left], source_frame[right] = source_frame[right], source_frame[left]
    return compact_json({
        "images": [
            {
                "group": 0,
                "frame": frame,
                "file": f"NH_academy/ui/hall/frame-{source_frame[frame]:03d}.png",
            }
            for frame in range(44)
        ]
    })


def make_runtime_assets(root: Path, archive: zipfile.ZipFile, names: dict[str, zipfile.ZipInfo], check_only: bool, icon_revision: dict):
    native_paths = sorted(path for path in names if path.startswith("native/") and path.endswith(".png"))
    if not EXCLUDED_NATIVE <= set(native_paths):
        missing = sorted(EXCLUDED_NATIVE - set(native_paths))
        raise ValueError(f"Expected provenance exceptions are absent from package: {missing}")

    # Preserve all approved native exports as provenance and expose them under
    # one mod-private resource directory; original assets are not copied.
    for native in native_paths:
        if native in EXCLUDED_NATIVE:
            continue
        package_bytes = archive.read(names[native])
        safe_write(root, SOURCE_ROOT / native, package_bytes, check_only)
        if native in ICON_NORMALS:
            # The original normal export remains provenance-only. Runtime art
            # comes from the separately reviewed and pinned v2 revision below.
            continue
        else:
            runtime_path = Path("NH_academy") / PurePosixPath(native).relative_to("native")
        safe_write(root, IMAGE_ROOT / runtime_path, package_bytes, check_only)

    # Install only manifest-pinned v2 exports. Native package normals are the
    # sole accepted prior state; built paths remain byte-identical safe
    # fallbacks while the client composes the marker from external DEF frames.
    for slot, expected in ICON_REVISION_SLOTS.items():
        runtime_name = expected["runtime"]
        legacy_bytes = archive.read(names[expected["source"]])
        install_curated_icon(
            root,
            runtime_name,
            icon_revision["exports_by_runtime"][runtime_name],
            legacy_bytes,
            check_only,
        )

    asset_records = archive_json(archive, names, "integration/academy-assets.json")
    town_layout = archive_json(archive, names, "integration/town-layout.json")

    # Three map-object bodies are rebuilt from the supplied generated masters.
    # Their mixed `native/adventure` package exports are intentionally excluded
    # because those restore original-game translucent shadow pixels.
    for master, definition in MAP_BODY_REGISTRATIONS.items():
        png = map_body_png(archive, names, asset_records, master)
        safe_write(root, IMAGE_ROOT / definition["png"], png, check_only)
        descriptor = aliased_animation(definition["resource"], definition["png"])
        safe_write(root, IMAGE_ROOT / f"{definition['resource']}.json", descriptor, check_only)

    top, lower = split_guild_wall(archive, names)
    split_images = (
        ("NH_academy/town/corrections/guild5-top.png", top),
        ("NH_academy/town/corrections/special4-lower.png", lower),
    )
    for path, data in split_images:
        safe_write(root, IMAGE_ROOT / path, data, check_only)
    safe_write(
        root,
        IMAGE_ROOT / "NH_ACADEMY_GUILD5_TOP.json",
        aliased_animation("NH_ACADEMY_GUILD5_TOP", split_images[0][0]),
        check_only,
    )
    safe_write(
        root,
        IMAGE_ROOT / "NH_ACADEMY_SPECIAL4_LOWER.json",
        aliased_animation("NH_ACADEMY_SPECIAL4_LOWER", split_images[1][0]),
        check_only,
    )

    # Native building art is one static authored frame per resource. Repeat
    # it through the original group-0 frame count, so no frame can fall back
    # to the original snowy animation.
    building_frame_counts = {
        Path(item.get("name", "")).stem.upper(): item.get("originalFrameCount", 1)
        for item in town_layout.get("assets", [])
        if item.get("name") and item.get("native", "").startswith("native/town/buildings/")
    }
    for native in native_paths:
        if not native.startswith("native/town/buildings/"):
            continue
        stem = Path(native).stem.upper()
        resource = f"NH_ACADEMY_{stem}"
        runtime_path = f"NH_academy/town/buildings/{Path(native).name}"
        descriptor = aliased_animation(resource, runtime_path, building_frame_counts.get(stem, 1))
        safe_write(root, IMAGE_ROOT / f"{resource}.json", descriptor, check_only)

    roof_path = "NH_academy/town/corrections/house-roof.png"
    safe_write(root, IMAGE_ROOT / "NH_ACADEMY_TOWN_ROOF.json", aliased_animation("NH_ACADEMY_TOWN_ROOF", roof_path), check_only)

    # Siege resources keep their original SGTW names except the four original
    # wood-gate images, which remain external and are never overridden. Siege
    # code loads these through ImagePath, not AnimationPath, so write direct
    # same-name PNG resource overrides rather than DEF/animation aliases.
    for native in native_paths:
        if not native.startswith("native/siege/") or native in EXCLUDED_NATIVE:
            continue
        resource = Path(native).stem.upper()
        package_bytes = archive.read(names[native])
        safe_write(root, IMAGE_ROOT / f"{resource}.png", package_bytes, check_only)

    safe_write(root, IMAGE_ROOT / "NH_tower_buildings.json", expected_hall_alias(), check_only, replace_vanilla_hall_alias=True)

    rank_patch = json.loads((root / "Mods/new-horizons/Content/config/factions/towerCreatureRanks.json").read_text(encoding="utf-8"))
    core_tower = load_jsonc(root / "config/factions/tower.json")
    faction_patch = academy_patch(root, core_tower, rank_patch)
    write_structure_masks(root, faction_patch["core:tower"]["town"]["structures"], check_only)
    safe_write(root, "Mods/new-horizons/Content/config/factions/academyArt.json", compact_json(faction_patch), check_only)


def import_provenance(root: Path, archive: zipfile.ZipFile, names: dict[str, zipfile.ZipInfo], check_only: bool):
    for item in PROVENANCE_FILES:
        if item not in names:
            raise ValueError(f"Archive is missing provenance file {item}")
        payload = archive.read(names[item])
        if item.startswith("integration/"):
            destination = SOURCE_ROOT / "integration" / Path(item).name
        else:
            destination = SOURCE_ROOT / "handoff" / item
        safe_write(root, destination, payload, check_only)

    masters = sorted(
        path for path in names
        if path.startswith("masters/") and (path.endswith(".png") or path.endswith(".prompt.txt"))
    )
    if not masters:
        raise ValueError("No generated masters were found")
    for master in masters:
        safe_write(root, SOURCE_ROOT / master, archive.read(names[master]), check_only)


def build_patch_outputs(root: Path, check_only: bool):
    generator_path = root / "tools/update-new-horizons-module.py"
    source = generator_path.read_text(encoding="utf-8")
    if "config/factions/academyArt.json" not in source:
        raise RuntimeError("Module generator has not registered academyArt.json")


def render_all_built_preview(root: Path, archive: zipfile.ZipFile, names: dict[str, zipfile.ZipInfo]):
    """Render effective production structure positions into ignored build/."""
    town_layout = archive_json(archive, names, "integration/town-layout.json")
    output = Image.open(root / IMAGE_ROOT / "NH_academy/town/landscape.png").convert("RGBA")
    if output.size != (800, 374):
        raise ValueError(f"Supplied native town landscape is {output.size}, expected (800, 374)")

    core_tower = load_jsonc(root / "config/factions/tower.json")
    rank_patch = json.loads((root / "Mods/new-horizons/Content/config/factions/towerCreatureRanks.json").read_text(encoding="utf-8"))
    art_patch = json.loads((root / "Mods/new-horizons/Content/config/factions/academyArt.json").read_text(encoding="utf-8"))
    core_structures = core_tower["tower"]["town"]["structures"]
    rank_structures = rank_patch["core:tower"]["town"].get("structures", {})
    art_structures = art_patch["core:tower"]["town"].get("structures", {})

    layers = []
    for item in town_layout["layout"]:
        structure_id = item["id"]
        structure = {
            **core_structures.get(structure_id, {}),
            **rank_structures.get(structure_id, {}),
            **art_structures.get(structure_id, {}),
        }
        resource = structure.get("animation")
        if not resource:
            raise ValueError(f"No effective animation for town layer {structure_id}")
        path = animation_image_path(root, resource)
        x = structure.get("x", item["x"])
        y = structure.get("y", item["y"])
        z = structure.get("z", item.get("z", 0))
        layer = Image.open(root / IMAGE_ROOT / path).convert("RGBA")
        layers.append((z, len(layers), structure_id, x, y, layer))

    roof = art_structures["academyRoof"]
    roof_path = animation_image_path(root, roof["animation"])
    roof_image = Image.open(root / IMAGE_ROOT / roof_path).convert("RGBA")
    layers.append((roof.get("z", 0), len(layers), "academyRoof", roof["x"], roof["y"], roof_image))

    for z, order, name, x, y, layer in sorted(layers):
        output.alpha_composite(layer, (x, y))

    preview_dir = root / "build/new-horizons-academy"
    preview_dir.mkdir(parents=True, exist_ok=True)
    preview_path = preview_dir / "tower-all-built-integrated.png"
    output.save(preview_path, format="PNG", optimize=False)

    authored = Image.open(archive.open(names["previews/town-final.png"])).convert("RGBA")
    if authored.size == output.size:
        from PIL import ImageChops
        difference = ImageChops.difference(output, authored)
        channel_sum = sum(sum(pixel) for pixel in difference.getdata())
        changed_pixels = sum(1 for pixel in difference.getdata() if any(pixel))
        comparison = {
            "generatedPreview": "build/new-horizons-academy/tower-all-built-integrated.png",
            "authorPreview": "archive-only previews/town-final.png (not imported)",
            "dimensions": list(output.size),
            "meanAbsoluteChannelDifference": channel_sum / (output.width * output.height * 4),
            "changedPixels": changed_pixels,
            "pixelCount": output.width * output.height,
            "runtimeAcceptance": False,
        }
        (preview_dir / "tower-preview-comparison.json").write_text(
            json.dumps(comparison, indent="\t") + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--check", action="store_true", help="verify generated/imported outputs without writing")
    parser.add_argument("--root", type=Path, default=ROOT, help=argparse.SUPPRESS)
    args = parser.parse_args()
    root = args.root.resolve()
    try:
        icon_revision = load_icon_revision(root, APPROVED_ICON_REVISION_MANIFEST_SHA256)
        with zipfile.ZipFile(args.archive) as archive:
            names = validate_archive(archive)
            import_provenance(root, archive, names, args.check)
            make_runtime_assets(root, archive, names, args.check, icon_revision)
            build_patch_outputs(root, args.check)
            validate_runtime_routes(root, archive, names, icon_revision)
            if not args.check:
                render_all_built_preview(root, archive, names)
    except (OSError, zipfile.BadZipFile, ValueError, RuntimeError, KeyError, TypeError) as error:
        parser.exit(1, f"Academy asset import failed: {error}\n")
    print("PASS: Academy source provenance and generated-only runtime assets are registered")
    return 0


if __name__ == "__main__":
    sys.exit(main())
