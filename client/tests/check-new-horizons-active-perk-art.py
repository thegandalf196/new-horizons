#!/usr/bin/env python3
"""Focused source/runtime guard for the active-perk v2 paintings."""

from __future__ import annotations

import hashlib
import json
import argparse
from pathlib import Path
import re
import sys

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools/tests"))
from nhart_test_resources import ArtPath
SOURCE_V2 = ROOT / "assets/new-horizons/art-source/active-perks-v2"
SOURCE_V3 = ROOT / "assets/new-horizons/art-source/active-perks-v3"
SOURCE_V5 = ROOT / "assets/new-horizons/art-source/active-perks-v5"
SOURCE_ENCIRCLEMENT = ROOT / "assets/new-horizons/art-source/encirclement-v1"
SOURCE_CLEAVE = ROOT / "assets/new-horizons/art-source/cleave-v1"
SOURCE_MASTER_GATE = ROOT / "assets/new-horizons/art-source/master-gate-v1"
SOURCE_COUNTERCHARGE = ROOT / "assets/new-horizons/art-source/countercharge-v1"
SOURCE_SHIELD_MASTER = ROOT / "assets/new-horizons/art-source/shield-master-v1"
SOURCE_IRON_DISCIPLINE = ROOT / "assets/new-horizons/art-source/iron-discipline-v1"
SOURCE_PAVISE = ROOT / "assets/new-horizons/art-source/pavise-v1"
SOURCE_SPELL_PENETRATION = ROOT / "assets/new-horizons/art-source/spell-penetration-v1"
SOURCE_EMPOWER_SPELL = ROOT / "assets/new-horizons/art-source/empower-spell-v1"
IMAGES = ArtPath()
ICONS = ROOT / "client/windows/NewHorizonsPerkIcons.h"
DEFINITIONS = ROOT / "config/newHorizonsPerks.json"

V2_EXPECTED = {
    "new-horizons:discipline.inspirationalLeader": (
        "NH_perk_inspirational_leader",
        "inspirational-leader",
    ),
    "new-horizons:sylvanLuck.wildChance": (
        "NH_perk_wild_chance",
        "wild-chance",
    ),
    "new-horizons:sylvanLuck.perfectMoment": (
        "NH_perk_perfect_moment",
        "perfect-moment",
    ),
}
V3_EXPECTED = {
    "new-horizons:offense.shockAssault": (
        "NH_perk_shock_assault",
        "shock-assault",
    ),
}
ENCIRCLEMENT_EXPECTED = {
    "new-horizons:offense.encirclement": (
        "NH_perk_encirclement",
        "encirclement",
    ),
}
CLEAVE_EXPECTED = {
    "new-horizons:offense.cleave": (
        "NH_perk_cleave",
        "cleave",
    ),
}
V5_EXPECTED = {
    "new-horizons:metamagic.arcaneAcquisition": (
        "NH_perk_arcane_acquisition",
        "arcane-acquisition",
    ),
    "new-horizons:metamagic.spellBuffer": (
        "NH_perk_spell_buffer_v2",
        "spell-buffer",
    ),
    "new-horizons:havocMagic.stormcaller": (
        "NH_perk_stormcaller_v2",
        "stormcaller",
    ),
    "new-horizons:wisdom.intelligence": (
        "NH_perk_intelligence",
        "intelligence",
    ),
    "new-horizons:sorceryMagic.illusionist": (
        "NH_perk_illusionist",
        "illusionist",
    ),
    "new-horizons:sorceryMagic.spellbinder": (
        "NH_perk_spellbinder",
        "spellbinder",
    ),
    "new-horizons:logistics.pathfinding": (
        "NH_perk_pathfinding",
        "pathfinding",
    ),
    "new-horizons:logistics.navigation": (
        "NH_perk_navigation",
        "navigation",
    ),
    "new-horizons:logistics.scouting": (
        "NH_perk_scouting",
        "scouting",
    ),
    "new-horizons:recruitment.volunteerNetwork": (
        "NH_perk_volunteer_network",
        "volunteer-network",
    ),
    "new-horizons:recruitment.eliteDraft": (
        "NH_perk_elite_draft",
        "elite-draft",
    ),
    "new-horizons:recruitment.championSCall": (
        "NH_perk_champions_call",
        "champions-call",
    ),
    "new-horizons:recruitment.masterRecruiter": (
        "NH_perk_master_recruiter",
        "master-recruiter",
    ),
    "new-horizons:demonicGating.swiftGate": (
        "NH_perk_swift_gate",
        "swift-gate",
    ),
    "new-horizons:demonicGating.wideGate": (
        "NH_perk_wide_gate",
        "wide-gate",
    ),
    "new-horizons:demonicGating.hellfireArrival": (
        "NH_perk_hellfire_arrival",
        "hellfire-arrival",
    ),
    "new-horizons:demonicGating.reinforcedGate": (
        "NH_perk_reinforced_gate",
        "reinforced-gate",
    ),
    "new-horizons:demonicGating.mobileGate": (
        "NH_perk_mobile_gate",
        "mobile-gate",
    ),
    "new-horizons:demonicGating.infernalBeacon": (
        "NH_perk_infernal_beacon",
        "infernal-beacon",
    ),
    "new-horizons:demonicGating.reserveDiscipline": (
        "NH_perk_reserve_discipline",
        "reserve-discipline",
    ),
    "new-horizons:demonicGating.endlessLegion": (
        "NH_perk_endless_legion",
        "endless-legion",
    ),
}
MASTER_GATE_EXPECTED = {
    "new-horizons:demonicGating.masterGate": (
        "NH_perk_master_gate",
        "master-gate",
    ),
}
COUNTERCHARGE_EXPECTED = {
    "new-horizons:armorer.countercharge": (
        "NH_perk_countercharge",
        "countercharge",
    ),
}
SHIELD_MASTER_EXPECTED = {
    "new-horizons:armorer.shieldMaster": (
        "NH_perk_shield_master",
        "shield-master",
    ),
}
IRON_DISCIPLINE_EXPECTED = {
    "new-horizons:armorer.ironDiscipline": (
        "NH_perk_iron_discipline",
        "iron-discipline",
    ),
}
PAVISE_EXPECTED = {
    "new-horizons:armorer.pavise": (
        "NH_perk_pavise",
        "pavise",
    ),
}
SPELL_PENETRATION_EXPECTED = {
    "new-horizons:spellcraft.spellPenetration": (
        "NH_perk_spell_penetration",
        "spell-penetration",
    ),
}
EMPOWER_SPELL_EXPECTED = {
    "new-horizons:spellcraft.empowerSpell": (
        "NH_perk_empower_spell",
        "empower-spell",
    ),
}
EXPECTED = (
    V2_EXPECTED
    | V3_EXPECTED
    | V5_EXPECTED
    | ENCIRCLEMENT_EXPECTED
    | CLEAVE_EXPECTED
    | MASTER_GATE_EXPECTED
    | COUNTERCHARGE_EXPECTED
    | SHIELD_MASTER_EXPECTED
    | IRON_DISCIPLINE_EXPECTED
    | PAVISE_EXPECTED
    | SPELL_PENETRATION_EXPECTED
    | EMPOWER_SPELL_EXPECTED
)


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_size(path: Path | ArtPath) -> tuple[int, int]:
    with (path.open_image() if isinstance(path, ArtPath) else Image.open(path)) as image:
        return image.size


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path,
                        help="Optional private art-source directory for master/export byte checks")
    args = parser.parse_args()
    mapping = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', ICONS.read_text(encoding="utf-8")))
    for source, expected in (
        (SOURCE_V2, V2_EXPECTED),
        (SOURCE_V3, V3_EXPECTED),
        (SOURCE_ENCIRCLEMENT, ENCIRCLEMENT_EXPECTED),
        (SOURCE_V5, V5_EXPECTED),
        (SOURCE_MASTER_GATE, MASTER_GATE_EXPECTED),
        (SOURCE_CLEAVE, CLEAVE_EXPECTED),
        (SOURCE_COUNTERCHARGE, COUNTERCHARGE_EXPECTED),
        (SOURCE_SHIELD_MASTER, SHIELD_MASTER_EXPECTED),
        (SOURCE_IRON_DISCIPLINE, IRON_DISCIPLINE_EXPECTED),
        (SOURCE_PAVISE, PAVISE_EXPECTED),
        (SOURCE_SPELL_PENETRATION, SPELL_PENETRATION_EXPECTED),
        (SOURCE_EMPOWER_SPELL, EMPOWER_SPELL_EXPECTED),
    ):
        generation = json.loads((source / "generation.json").read_text(encoding="utf-8"))
        by_id = {asset["id"]: asset for asset in generation["assets"]}
        assert set(by_id) == set(expected), (source, "generation coverage")
        assert generation["status"].startswith("provisional"), "provisional art must be labeled honestly"
        for perk_id, (key, slug) in expected.items():
            asset = by_id[perk_id]
            assert tuple(asset["dimensions"]) == (1254, 1254)
            assert re.fullmatch(r"[0-9a-f]{64}", asset["source_sha256"]), (perk_id, "master hash")
            prompt_name = asset.get("prompt_file", generation.get("prompt_file"))
            assert prompt_name, (perk_id, "missing prompt provenance")
            prompt = source / prompt_name
            assert prompt.is_file() and prompt.read_text(encoding="utf-8").strip(), prompt

            export = source / "exports" / slug
            export_manifest = json.loads(
                (export / f"{slug}-manifest.json").read_text(encoding="utf-8")
            )
            outputs = {item["file"]: item for item in export_manifest["outputs"]}
            assert export_manifest["source"]["sha256"] == asset["source_sha256"]
            assert export_manifest["source"]["dimensions"] == asset["dimensions"]
            for filename in (
                "master.png",
                f"{slug}-44.png",
                f"{slug}-32.png",
                f"{slug}-comparison.png",
            ):
                assert filename in outputs, (perk_id, "missing export provenance", filename)
                assert re.fullmatch(r"[0-9a-f]{64}", outputs[filename]["sha256"])
            assert outputs["master.png"]["sha256"] == asset["source_sha256"]
            assert outputs["master.png"]["dimensions"] == asset["dimensions"]
            assert outputs[f"{slug}-44.png"]["dimensions"] == [44, 44]
            assert outputs[f"{slug}-32.png"]["dimensions"] == [32, 32]
            if args.source_root is not None:
                private_source = args.source_root / source.relative_to(ROOT / "assets/new-horizons/art-source")
                private_master = private_source / asset["master"]
                assert digest(private_master) == asset["source_sha256"], (perk_id, "master hash")
                assert image_size(private_master) == tuple(asset["dimensions"])
                for item in export_manifest["outputs"]:
                    output = private_source / "exports" / slug / item["file"]
                    assert output.is_file(), output
                    assert digest(output) == item["sha256"], (output, "export hash")
                    assert list(image_size(output)) == item["dimensions"], (output, "export dimensions")

            assert mapping.get(perk_id) == key, (perk_id, mapping.get(perk_id), key)
            descriptor = json.loads((IMAGES / f"{key}.json").read_text(encoding="utf-8"))
            frames = descriptor["images"]
            assert [frame["frame"] for frame in frames] == [0, 1, 2, 3]
            assert [frame["file"] for frame in frames] == [
                f"{key}_normal.png",
                f"{key}_pressed.png",
                f"{key}_disabled.png",
                f"{key}_highlighted.png",
            ]
            for frame in frames:
                path = IMAGES / frame["file"]
                assert path.is_file(), path
                assert image_size(path) == (44, 44)

    encirclement_manifest = json.loads(
        (SOURCE_ENCIRCLEMENT / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert encirclement_manifest["status"].startswith("provisional")
    assert encirclement_manifest["assets"] == ["NH_perk_encirclement"]
    for filename, expected_hash in encirclement_manifest["files"].items():
        path = IMAGES / filename
        assert path.is_file(), path
        assert digest(path) == expected_hash, (filename, "runtime export hash")

    cleave_manifest = json.loads(
        (SOURCE_CLEAVE / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert cleave_manifest["status"].startswith("provisional")
    assert cleave_manifest["assets"] == ["NH_perk_cleave"]
    for manifest_path, expected_hash in cleave_manifest["files"].items():
        filename = Path(manifest_path).name
        if manifest_path.startswith("source/"):
            path = IMAGES / filename
        elif manifest_path.startswith("Mods/"):
            path = IMAGES / filename
        else:
            raise AssertionError(f"unexpected Cleave runtime manifest path: {manifest_path}")
        assert path.is_file(), path
        assert digest(path) == expected_hash, (manifest_path, "runtime export hash")

    countercharge_manifest = json.loads(
        (SOURCE_COUNTERCHARGE / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert countercharge_manifest["status"].startswith("provisional")
    assert countercharge_manifest["assets"] == ["NH_perk_countercharge"]
    live_state_hashes = set()
    for manifest_path, expected_hash in countercharge_manifest["files"].items():
        filename = Path(manifest_path).name
        if manifest_path.startswith("source/"):
            path = IMAGES / filename
        elif manifest_path.startswith("Mods/"):
            path = IMAGES / filename
        else:
            raise AssertionError(f"unexpected Countercharge runtime manifest path: {manifest_path}")
        assert path.is_file(), path
        assert digest(path) == expected_hash, (manifest_path, "runtime export hash")
        if manifest_path.startswith("Mods/") and filename.endswith(".png"):
            live_state_hashes.add(expected_hash)
    assert len(live_state_hashes) == 4, "Countercharge runtime states must have distinct hashes"

    shield_master_manifest = json.loads(
        (SOURCE_SHIELD_MASTER / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert shield_master_manifest["status"].startswith("provisional")
    assert shield_master_manifest["assets"] == ["NH_perk_shield_master"]
    live_state_hashes = set()
    for manifest_path, expected_hash in shield_master_manifest["files"].items():
        filename = Path(manifest_path).name
        if manifest_path.startswith("source/"):
            path = IMAGES / filename
        elif manifest_path.startswith("Mods/"):
            path = IMAGES / filename
        else:
            raise AssertionError(f"unexpected Shield Master runtime manifest path: {manifest_path}")
        assert path.is_file(), path
        assert digest(path) == expected_hash, (manifest_path, "runtime export hash")
        if manifest_path.startswith("Mods/") and filename.endswith(".png"):
            live_state_hashes.add(expected_hash)
    assert len(live_state_hashes) == 4, "Shield Master runtime states must have distinct hashes"

    iron_discipline_manifest = json.loads(
        (SOURCE_IRON_DISCIPLINE / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert iron_discipline_manifest["status"].startswith("provisional")
    assert iron_discipline_manifest["assets"] == ["NH_perk_iron_discipline"]
    live_state_hashes = set()
    for manifest_path, expected_hash in iron_discipline_manifest["files"].items():
        filename = Path(manifest_path).name
        if manifest_path.startswith("source/"):
            path = IMAGES / filename
        elif manifest_path.startswith("Mods/"):
            path = IMAGES / filename
        else:
            raise AssertionError(f"unexpected Iron Discipline runtime manifest path: {manifest_path}")
        assert path.is_file(), path
        assert digest(path) == expected_hash, (manifest_path, "runtime export hash")
        if manifest_path.startswith("Mods/") and filename.endswith(".png"):
            live_state_hashes.add(expected_hash)
    assert len(live_state_hashes) == 4, "Iron Discipline runtime states must have distinct hashes"

    pavise_manifest = json.loads(
        (SOURCE_PAVISE / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert pavise_manifest["status"].startswith("provisional")
    assert pavise_manifest["assets"] == ["NH_perk_pavise"]
    live_state_hashes = set()
    for manifest_path, expected_hash in pavise_manifest["files"].items():
        filename = Path(manifest_path).name
        if manifest_path.startswith("source/"):
            path = IMAGES / filename
        elif manifest_path.startswith("Mods/"):
            path = IMAGES / filename
        else:
            raise AssertionError(f"unexpected Pavise runtime manifest path: {manifest_path}")
        assert path.is_file(), path
        assert digest(path) == expected_hash, (manifest_path, "runtime export hash")
        if manifest_path.startswith("Mods/") and filename.endswith(".png"):
            live_state_hashes.add(expected_hash)
    assert len(live_state_hashes) == 4, "Pavise runtime states must have distinct hashes"

    spell_penetration_manifest = json.loads(
        (SOURCE_SPELL_PENETRATION / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert spell_penetration_manifest["status"].startswith("provisional")
    assert spell_penetration_manifest["assets"] == ["NH_perk_spell_penetration"]
    live_state_hashes = set()
    for manifest_path, expected_hash in spell_penetration_manifest["files"].items():
        filename = Path(manifest_path).name
        if manifest_path.startswith("source/"):
            path = IMAGES / filename
        elif manifest_path.startswith("Mods/"):
            path = IMAGES / filename
        else:
            raise AssertionError(f"unexpected Spell Penetration runtime manifest path: {manifest_path}")
        assert path.is_file(), path
        assert digest(path) == expected_hash, (manifest_path, "runtime export hash")
        if manifest_path.startswith("Mods/") and filename.endswith(".png"):
            live_state_hashes.add(expected_hash)
    assert len(live_state_hashes) == 4, "Spell Penetration runtime states must have distinct hashes"

    empower_spell_manifest = json.loads(
        (SOURCE_EMPOWER_SPELL / "runtime-manifest.json").read_text(encoding="utf-8")
    )
    assert empower_spell_manifest["status"].startswith("provisional")
    assert empower_spell_manifest["assets"] == ["NH_perk_empower_spell"]
    live_state_hashes = set()
    for manifest_path, expected_hash in empower_spell_manifest["files"].items():
        filename = Path(manifest_path).name
        if manifest_path.startswith("source/"):
            path = IMAGES / filename
        elif manifest_path.startswith("Mods/"):
            path = IMAGES / filename
        else:
            raise AssertionError(f"unexpected Empower Spell runtime manifest path: {manifest_path}")
        assert path.is_file(), path
        assert digest(path) == expected_hash, (manifest_path, "runtime export hash")
        if manifest_path.startswith("Mods/") and filename.endswith(".png"):
            live_state_hashes.add(expected_hash)
    assert len(live_state_hashes) == 4, "Empower Spell runtime states must have distinct hashes"

    definitions = json.loads(DEFINITIONS.read_text(encoding="utf-8"))
    active = {
        perk["id"]
        for skill in definitions["skills"].values()
        for perk in skill["perks"]
        if perk["effect"]["status"] == "active"
    }
    assert set(EXPECTED) <= active
    missing = sorted(active - set(mapping))
    assert not missing, f"active perks without named icon mappings: {missing}"
    normal_hashes = set()
    for perk_id in sorted(active):
        key = mapping[perk_id]
        descriptor = json.loads((IMAGES / f"{key}.json").read_text(encoding="utf-8"))
        normal = IMAGES / descriptor["images"][0]["file"]
        assert image_size(normal) == (44, 44)
        value = digest(normal)
        assert value not in normal_hashes, f"duplicate active normal art: {perk_id}"
        normal_hashes.add(value)

    print(f"PASS: active-perk provenance/export metadata, packaged runtime hashes/states, bindings, and {len(active)}-icon uniqueness"
          + ("; private source bytes verified" if args.source_root else ""))


if __name__ == "__main__":
    main()
