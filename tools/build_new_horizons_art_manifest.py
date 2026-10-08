#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Pin selected runtime art bytes, not authoring folders or original archives."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil

SELECTED_SNAPSHOT = "1c720ae7826b8819b6e776d9af2652c79e8f72eb311806189e83ec07eeb3526e"
IMAGE_ROOT = "Mods/new-horizons/Images/"
CONTENT_ROOT = "Mods/new-horizons/Content/"
EXTERNAL_CASTLE_HALL_ICON = CONTENT_ROOT + "sprites/HALLCSTL/mage-guild-5.png"
CASTLE_HALL_DESCRIPTOR = CONTENT_ROOT + "sprites/HALLCSTL.json"
CASTLE_HALL_ALIAS = {
    "images": [{"group": 0, "frame": 4, "defFile": "HALLCSTL.def",
                "defGroup": 0, "defFrame": 3}],
}
IMAGE_FAMILIES = {
    "NH_academy", "NH_magic_assets", "Wisp", "WispProjectile",
    "WispUpgrade", "WispUpgradeProjectile", "cabir-complete-handoff",
    "cabir-handoff", "cabir-handoff-map-native-v1", "magi-vcmi-complete",
}
OLD_DESCRIPTORS = {
    "NH_Cabir.json", "NH_CabirMaster.json", "NH_CabirMap.json",
    "NH_CabirMasterMap.json", "AVWgrem0.json", "AVWgrex0.json",
}
OLD_CABIR_IMAGES = {
    "NH_cabir_icon_large.png", "NH_cabir_icon_small.png",
    "NH_cabirMaster_icon_large.png", "NH_cabirMaster_icon_small.png",
    "NH_CabirEncounterLeft.png", "NH_CabirEncounterRight.png",
    "NH_CabirMasterEncounterLeft.png", "NH_CabirMasterEncounterRight.png",
}


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def safe_path(value):
    path = PurePosixPath(value)
    if not value or path.is_absolute() or any(p in {".", ".."} for p in value.split("/")) or "\\" in value or ":" in value:
        raise ValueError("Non-portable resource/source path")
    return path


def descriptor_references(value):
    base = value.get("basepath", "")
    for sequence in value.get("sequences", []):
        for frame in sequence.get("frames", []):
            if isinstance(frame, str):
                yield base + frame
    for image in value.get("images", []):
        if isinstance(image, dict) and isinstance(image.get("file"), str):
            yield base + image["file"]


def family(source):
    lower = source.lower()
    if "cabir" in lower:
        return "cabir-and-master"
    if "magi-vcmi-complete" in lower:
        return "magi-and-archmagi"
    if "wisp" in lower:
        return "wisps-and-projectiles"
    if "/nh_magic_assets/casting/" in lower:
        return "casting-glows"
    if "/nh_magic_assets/guild/" in lower or source == "config/newHorizonsMagicAssets.json":
        return "guild-room-composites-and-bindings"
    if "academy" in lower:
        return "academy-town-and-portrait-ui"
    if source.startswith(CONTENT_ROOT):
        return "mage-guild-town-art-and-ui"
    return "builtin-skills-perks-orders-and-ui"


def resource(source):
    if source.startswith(IMAGE_ROOT):
        return "SPRITES/" + source[len(IMAGE_ROOT):]
    if source.startswith(CONTENT_ROOT + "sprites/"):
        return "SPRITES/" + source[len(CONTENT_ROOT + "sprites/"):]
    if source.startswith(CONTENT_ROOT + "data/"):
        return "DATA/" + source[len(CONTENT_ROOT + "data/"):]
    if source == "config/newHorizonsMagicAssets.json":
        return "CONFIG/newHorizonsMagicAssets.json"
    raise ValueError("Unexpected runtime art mount")


def selected(source):
    path = safe_path(source)
    # Native decoded pixels equal the unchanged original HALLCSTL frame 3.
    # Keep presentation via an external frame alias, never repack the pixels.
    if source == EXTERNAL_CASTLE_HALL_ICON:
        return False
    if source.startswith(IMAGE_ROOT):
        relative = source[len(IMAGE_ROOT):]
        parts = PurePosixPath(relative).parts
        return (len(parts) == 1 and path.name not in OLD_CABIR_IMAGES) or parts[0] in IMAGE_FAMILIES
    if source.startswith(CONTENT_ROOT + "sprites/"):
        return path.name not in OLD_DESCRIPTORS and not path.name.startswith("NH_ArchMageGrey")
    return source.startswith(CONTENT_ROOT + "data/") or source == "config/newHorizonsMagicAssets.json"


def runtime_payload(source, original):
    if source == CASTLE_HALL_DESCRIPTOR:
        return (json.dumps(CASTLE_HALL_ALIAS, indent=2) + "\n").encode("utf-8")
    return original


def origin(source, group):
    if source == CASTLE_HALL_DESCRIPTOR:
        return "custom-descriptor-referencing-unchanged-external-original-frame"
    if group == "cabir-and-master":
        return "selected-original-Lizardman-derived-modified-creature-art-and-descriptors"
    if group == "builtin-skills-perks-orders-and-ui":
        return "selected-mixed-original-based-UI-and-independently-authored-icons"
    if group in {"guild-room-composites-and-bindings", "academy-town-and-portrait-ui",
                 "mage-guild-town-art-and-ui", "magi-and-archmagi"}:
        return "selected-supplied-or-modified-composite"
    return "selected-authored-runtime-export"


def build_manifest(snapshot, stage, output, inventory):
    snapshot = snapshot.resolve(strict=True)
    evidence = json.loads(inventory.read_text())
    if evidence.get("selectedSnapshotSha256") != SELECTED_SNAPSHOT or not evidence.get("reviewMastersDraftsExcluded"):
        raise ValueError("Inventory does not identify the selected, draft-free runtime")
    stage = stage.absolute()
    if stage.exists() or stage.is_symlink():
        raise ValueError("Staging destination must be fresh")
    resolved_stage = stage.resolve()
    resolved_output = output.resolve()
    if resolved_output.is_relative_to(snapshot) or resolved_output.is_relative_to(resolved_stage) or resolved_output == inventory.resolve():
        raise ValueError("Manifest output must not overwrite an input or enter private staging")
    if resolved_stage.is_relative_to(Path(__file__).resolve().parents[1]):
        raise ValueError("Private staging must remain outside the repository")
    if any(parent.is_symlink() for parent in [stage, *stage.parents]):
        raise ValueError("Staging must not traverse symbolic links")
    if resolved_stage.is_relative_to(snapshot) or snapshot.is_relative_to(resolved_stage):
        raise ValueError("Staging must not overlap immutable input")
    metadata = json.loads((snapshot / "SNAPSHOT.json").read_text())
    if metadata.get("snapshot_sha256") != SELECTED_SNAPSHOT:
        raise ValueError("Not the explicitly selected runtime snapshot")
    sources = sorted(source for source in metadata["files"] if selected(source))
    if not sources:
        raise ValueError("Empty art selection")
    entries = []
    resources = set()
    # Preflight every input before creating any staging output.
    for source in sources:
        path = snapshot / safe_path(source)
        if path.is_symlink() or not path.is_file() or sha256(path) != metadata["files"][source]:
            raise ValueError("Selected input missing, linked or changed: " + source)
        name = resource(source)
        if name.upper() in resources:
            raise ValueError("Ambiguous case-insensitive resource: " + name)
        resources.add(name.upper())
        group = family(source)
        payload = runtime_payload(source, path.read_bytes())
        entries.append({
            "resource": name, "source": source, "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(), "family": group,
            "origin": origin(source, group),
            "selection": "pinned-current-runtime-" + SELECTED_SNAPSHOT,
            "approval": "user-selected-runtime-art-bundle-2026-10-08; not blanket-original-archive-authorization",
        })
    for entry in entries:
        if not entry["source"].endswith(".json") or entry["resource"].startswith("CONFIG/"):
            continue
        value = json.loads(runtime_payload(entry["source"], (snapshot / entry["source"]).read_bytes()))
        for reference in descriptor_references(value):
            if reference.lower().endswith((".png", ".bmp", ".pcx")) and ("SPRITES/" + reference).upper() not in resources and ("DATA/" + reference).upper() not in resources:
                raise ValueError("Selected descriptor dependency absent: " + reference)
    tower = json.loads((snapshot / (CONTENT_ROOT + "config/creatures/tower.json")).read_text())
    conflux = json.loads((snapshot / (CONTENT_ROOT + "config/creatures/conflux.json")).read_text())
    bindings = {key: tower[key]["graphics"] for key in ["core:gremlin", "core:masterGremlin", "core:mage", "core:archMage"]}
    bindings.update({key: conflux[key]["graphics"] for key in ["wisp", "wispUpgrade"]})
    if bindings != evidence["creatureGraphicsBindings"]:
        raise ValueError("Current creature bindings disagree with reviewed inventory")
    for graphics in bindings.values():
        for key in ["iconLarge", "iconSmall"]:
            if ("SPRITES/" + graphics[key]).upper() not in resources:
                raise ValueError("Current creature portrait binding absent: " + graphics[key])
    groups = {group: sum(e["family"] == group for e in entries) for group in sorted({e["family"] for e in entries})}
    if sum(e["family"] == "casting-glows" for e in entries) != 864:
        raise ValueError("Casting family must retain exactly 864 frames")
    if sum("/NH_magic_assets/guild/" in e["source"] for e in entries) != 9:
        raise ValueError("All nine selected guild composites required")
    private_roots = ["cabir-complete-handoff", "cabir-handoff", "cabir-handoff-map-native-v1", "magi-vcmi-complete"]
    if sum(any(e["source"].startswith(IMAGE_ROOT + root + "/") for root in private_roots) for e in entries) != 552:
        raise ValueError("Selected private creature export must retain exactly 552 files")
    for field in ["cabir-complete-handoff", "cabir-handoff", "cabir-handoff-map-native-v1", "magi-vcmi-complete", "Wisp", "WispUpgrade"]:
        if not any((IMAGE_ROOT + field + "/") in e["source"] for e in entries):
            raise ValueError("Required selected family absent: " + field)
    manifest = {
        "format": 1, "entries": entries, "requiredFamilies": sorted(groups),
        "requiredFamilyCounts": groups,
    }
    stage.mkdir(parents=True)
    for entry in entries:
        target = stage / entry["source"]
        target.parent.mkdir(parents=True, exist_ok=True)
        if entry["source"] == CASTLE_HALL_DESCRIPTOR:
            target.write_bytes(runtime_payload(entry["source"], (snapshot / entry["source"]).read_bytes()))
        else:
            shutil.copyfile(snapshot / entry["source"], target)
        if sha256(target) != entry["sha256"]:
            raise ValueError("Staging copy changed bytes")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--snapshot-root", required=True, type=Path)
    parser.add_argument("--stage-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--inventory", required=True, type=Path)
    args = parser.parse_args()
    result = build_manifest(args.snapshot_root, args.stage_root, args.output, args.inventory)
    print(json.dumps({"entries": len(result["entries"]), "requiredFamilies": result["requiredFamilies"]}, sort_keys=True))


if __name__ == "__main__":
    main()
