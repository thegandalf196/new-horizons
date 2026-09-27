#!/usr/bin/env python3
"""Focused source/runtime guard for the active-perk v2 paintings."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import re

from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
SOURCE_V2 = ROOT / "assets/new-horizons/art-source/active-perks-v2"
SOURCE_V3 = ROOT / "assets/new-horizons/art-source/active-perks-v3"
SOURCE_V5 = ROOT / "assets/new-horizons/art-source/active-perks-v5"
SOURCE_ENCIRCLEMENT = ROOT / "assets/new-horizons/art-source/encirclement-v1"
SOURCE_MASTER_GATE = ROOT / "assets/new-horizons/art-source/master-gate-v1"
IMAGES = ROOT / "Mods/new-horizons/Images"
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
EXPECTED = V2_EXPECTED | V3_EXPECTED | V5_EXPECTED | ENCIRCLEMENT_EXPECTED | MASTER_GATE_EXPECTED


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def image_size(path: Path) -> tuple[int, int]:
    with Image.open(path) as image:
        return image.size


def main() -> None:
    mapping = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', ICONS.read_text(encoding="utf-8")))
    for source, expected in (
        (SOURCE_V2, V2_EXPECTED),
        (SOURCE_V3, V3_EXPECTED),
        (SOURCE_ENCIRCLEMENT, ENCIRCLEMENT_EXPECTED),
        (SOURCE_V5, V5_EXPECTED),
        (SOURCE_MASTER_GATE, MASTER_GATE_EXPECTED),
    ):
        generation = json.loads((source / "generation.json").read_text(encoding="utf-8"))
        by_id = {asset["id"]: asset for asset in generation["assets"]}
        assert set(by_id) == set(expected), (source, "generation coverage")
        assert generation["status"].startswith("provisional"), "provisional art must be labeled honestly"
        for perk_id, (key, slug) in expected.items():
            asset = by_id[perk_id]
            master = source / asset["master"]
            assert master.is_file(), master
            assert image_size(master) == tuple(asset["dimensions"]) == (1254, 1254)
            assert digest(master) == asset["source_sha256"], (perk_id, "master hash")
            prompt = source / asset["prompt_file"]
            assert prompt.is_file() and prompt.read_text(encoding="utf-8").strip(), prompt

            export = source / "exports" / slug
            for filename in (
                "master.png",
                f"{slug}-44.png",
                f"{slug}-32.png",
                f"{slug}-comparison.png",
                f"{slug}-manifest.json",
            ):
                assert (export / filename).is_file(), export / filename
            assert image_size(export / f"{slug}-44.png") == (44, 44)
            assert image_size(export / f"{slug}-32.png") == (32, 32)

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

    print(f"PASS: active-perk sources, previews, runtime states, bindings, and {len(active)}-icon uniqueness")


if __name__ == "__main__":
    main()
