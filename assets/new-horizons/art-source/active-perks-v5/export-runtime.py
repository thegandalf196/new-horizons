#!/usr/bin/env python3
"""Export active-perk v5 provisional paintings into VCMI button states."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from PIL import Image, ImageEnhance


ASSETS = (
    ("malediction", "NH_perk_malediction"),
    ("arcane-acquisition", "NH_perk_arcane_acquisition"),
    ("spell-buffer", "NH_perk_spell_buffer_v2"),
    ("stormcaller", "NH_perk_stormcaller_v2"),
    ("intelligence", "NH_perk_intelligence"),
    ("illusionist", "NH_perk_illusionist"),
    ("spellbinder", "NH_perk_spellbinder"),
    ("pathfinding", "NH_perk_pathfinding"),
    ("navigation", "NH_perk_navigation"),
    ("scouting", "NH_perk_scouting"),
    ("volunteer-network", "NH_perk_volunteer_network"),
    ("elite-draft", "NH_perk_elite_draft"),
    ("champions-call", "NH_perk_champions_call"),
    ("master-recruiter", "NH_perk_master_recruiter"),
    ("swift-gate", "NH_perk_swift_gate"),
    ("wide-gate", "NH_perk_wide_gate"),
    ("hellfire-arrival", "NH_perk_hellfire_arrival"),
    ("reinforced-gate", "NH_perk_reinforced_gate"),
    ("mobile-gate", "NH_perk_mobile_gate"),
    ("infernal-beacon", "NH_perk_infernal_beacon"),
    ("reserve-discipline", "NH_perk_reserve_discipline"),
    ("endless-legion", "NH_perk_endless_legion"),
)


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main() -> None:
    here = Path(__file__).resolve().parent
    root = here.parents[3]
    live = root / "Mods/new-horizons/Images"
    outputs: dict[str, str] = {}

    for slug, stem in ASSETS:
        normal = Image.open(here / "exports" / slug / f"{slug}-44.png").convert("RGBA")
        states = {
            "normal": normal,
            "pressed": ImageEnhance.Brightness(normal).enhance(0.82),
            "disabled": ImageEnhance.Brightness(ImageEnhance.Color(normal).enhance(0.15)).enhance(0.65),
            "highlighted": ImageEnhance.Brightness(normal).enhance(1.13),
        }
        frames = []
        for frame, (state, image) in enumerate(states.items()):
            path = live / f"{stem}_{state}.png"
            if path.exists():
                with Image.open(path) as existing:
                    if existing.convert("RGBA").tobytes() != image.tobytes():
                        raise FileExistsError(f"refusing to replace different runtime art: {path}")
            else:
                image.save(path, format="PNG")
            outputs[path.name] = sha256(path)
            frames.append({"group": 0, "frame": frame, "file": path.name})

        descriptor = live / f"{stem}.json"
        descriptor_text = json.dumps({"images": frames}, indent=2) + "\n"
        if descriptor.exists() and descriptor.read_text(encoding="utf-8") != descriptor_text:
            raise FileExistsError(f"refusing to replace different descriptor: {descriptor}")
        if not descriptor.exists():
            descriptor.write_text(descriptor_text, encoding="utf-8")
        outputs[descriptor.name] = sha256(descriptor)

    manifest = {
        "method": "homm3-art high-resolution master, LANCZOS reduction, brightness/color-only runtime states",
        "status": "provisional; not final user-approved artwork",
        "assets": [stem for _, stem in ASSETS],
        "files": outputs,
    }
    (here / "runtime-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
