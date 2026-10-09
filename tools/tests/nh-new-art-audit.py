#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline shipping-art checks against verified committed NHART bytes.

Requires Pillow. Optional SVG geometry/reproduction checks require an explicit
external authoring mirror, and regeneration writes only temporary storage. The
default never reads loose artwork or masters and never generates artwork. These
checks do not establish artistic rights or game-rendering acceptance.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from nhart_test_resources import ArtPath

ROOT = Path(__file__).resolve().parents[2]
SCHOOLS = ("light", "nature", "sorcery", "havoc", "shadow", "chaos")
BUTTONS = ("charge", "holdTheLine", "advance", "aggressive", "defensive", "spells", "cancel")
SKILL_SIZES = {"small": (32, 32), "medium": (44, 44), "large": (82, 93), "scenarioBonus": (58, 64)}
HERO_GLYPHS = ("attack", "defense", "power", "knowledge", "mana", "leadership", "movement",
               "morale", "luck", "siege", "mastery", "core", "elite", "champion", "growth")
CUSTOM_ANIMATIONS = {
    "NH_orders_gauntlet": (48, 36),
    "NH_perk_bone_collector": (44, 44),
    "NH_perk_neutral": (44, 44),
}
PROVISIONAL_SKILL_FAMILIES = (
    # These painted families are produced by art-source/export_skill_icons.py,
    # not by the geometric SVG generator audited below.  The dedicated
    # faction-skill audit owns the requested nine; the three earlier
    # provisional families remain classified here so they do not masquerade
    # as missing SVG counterparts.
    "battlecraft", "recruitment", "warcasting",
    "divineMandate", "sylvanLuck", "metamagic", "demonicGating",
    "shroudOfMalassa", "bloodrage", "bulwarkOfTheMire",
    "elementalRebirth",
)
PROVISIONAL_SKILL_PNG = re.compile(
    r"^NH_(?:" + "|".join(map(re.escape, PROVISIONAL_SKILL_FAMILIES)) +
    r")_(?:basic|advanced|expert)_(?:small|medium|large|scenarioBonus)\.png$"
)
METAMAGIC_PRISM_PNG = re.compile(
    r"^NH_metamagic_prism_(?:basic|advanced|expert)_"
    r"(?:small|medium|large|scenarioBonus)\.png$"
)
APPROVED_SPELL_BORDER_PNG = re.compile(
    r"^NH_(?:light|nature|sorcery|havoc|shadow|chaos)_spellBorder_"
    r"(?:basic|advanced|expert|none)\.png$"
)
APPROVED_SPELL_BORDER_JSON = re.compile(
    r"^NH_(?:light|nature|sorcery|havoc|shadow|chaos)_spellBorders\.json$"
)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def is_provisional_skill_png(path):
    return PROVISIONAL_SKILL_PNG.fullmatch(path.name) is not None


def is_non_vector_png(path):
    """Classify PNG-only art whose source is not the geometric SVG generator."""
    return (
        is_provisional_skill_png(path)
        or METAMAGIC_PRISM_PNG.fullmatch(path.name) is not None
        or APPROVED_SPELL_BORDER_PNG.fullmatch(path.name) is not None
    )


def is_non_vector_artifact(path):
    return is_non_vector_png(path) or APPROVED_SPELL_BORDER_JSON.fullmatch(path.name) is not None


def audit(reproduce, hero_growth=False, authoring_root=None):
    images = ArtPath()
    source = authoring_root / "assets/new-horizons/svg" if authoring_root else None
    svgs = sorted(source.glob("*.svg")) if source else []
    provisional_skill_pngs = sorted((p for p in images.glob("*.png") if is_provisional_skill_png(p)), key=lambda p: p.name)
    metamagic_prism_pngs = sorted(
        (p for p in images.glob("*.png") if METAMAGIC_PRISM_PNG.fullmatch(p.name)), key=lambda p: p.name
    )
    approved_spell_border_pngs = sorted(
        (p for p in images.glob("*.png") if APPROVED_SPELL_BORDER_PNG.fullmatch(p.name)), key=lambda p: p.name
    )
    require(
        len(provisional_skill_pngs) == len(PROVISIONAL_SKILL_FAMILIES) * 3 * len(SKILL_SIZES),
        "Provisional skill PNG inventory is incomplete; run the dedicated skill-icon audit",
    )
    expected_metamagic_prisms = {
        f"NH_metamagic_prism_{rank}_{size}.png"
        for rank in ("basic", "advanced", "expert")
        for size in SKILL_SIZES
    }
    require(
        {path.name for path in metamagic_prism_pngs} == expected_metamagic_prisms,
        "Metamagic prism PNG inventory is incomplete; use the dedicated skill-icon audit",
    )
    require(
        len(approved_spell_border_pngs) == len(SCHOOLS) * 4,
        "Spell-border PNG inventory is incomplete; use the school-art audit",
    )
    approved_spell_border_descriptors = sorted(
        (p for p in images.glob("*.json") if APPROVED_SPELL_BORDER_JSON.fullmatch(p.name)), key=lambda p: p.name
    )
    require(
        len(approved_spell_border_descriptors) == len(SCHOOLS),
        "Spell-border descriptor inventory is incomplete; use the school-art audit",
    )
    pngs = sorted((p for p in images.glob("*.png")
                  if not is_non_vector_png(p)
                  and not any(p.stem == name or p.name.startswith(name + "_") for name in CUSTOM_ANIMATIONS)), key=lambda p: p.name)
    animations = sorted(
        (p for p in images.glob("*.json")
        if p.stem in {*(f"NH_{name}_button" for name in BUTTONS),
                      *(f"NH_{school}_bookmark" for school in SCHOOLS),
                      "NH_hero_actions_entry", "NH_hero_growth_entry"}), key=lambda p: p.name
    )
    require(bool(animations), "Missing shipping artwork")
    if source:
        require(bool(svgs), "Missing externally staged SVG authoring sources")
        require(all((images / (p.stem + ".png")).is_file() for p in svgs),
                "Authored SVG lacks selected shipping PNG counterpart")
    for path in svgs:
        tree = ET.parse(path).getroot()
        require(all(element.tag.split("}")[-1] in {"svg", "polygon", "polyline", "circle"}
                    for element in tree.iter()), f"Unexpected non-geometric SVG element: {path.name}")
        require(all(not key.lower().endswith("href") and "url(" not in value.lower()
                    for element in tree.iter() for key, value in element.attrib.items()),
                f"External/embedded SVG reference: {path.name}")
        with (images / (path.stem + ".png")).open_image() as image:
            require(image.format == "PNG" and image.mode == "RGBA", f"Not RGBA PNG: {path.name}")
            require(image.getchannel("A").getbbox() is not None, f"Empty artwork: {path.name}")
            require(image.size == (int(tree.attrib["width"]), int(tree.attrib["height"])),
                    f"SVG/PNG size mismatch: {path.name}")
    for path in animations:
        frames = json.loads(path.read_text())["images"]
        bookmark = path.stem.endswith("_bookmark")
        require(len(frames) == (2 if bookmark else 4), f"Wrong state count: {path.name}")
        entry_sizes = {
            "NH_hero_actions_entry": (48, 36),
            "NH_hero_growth_entry": (24, 24),
            "NH_qload_24": (24, 24),
            "NH_qsave_24": (24, 24),
            "NH_qload_32": (32, 32),
            "NH_qsave_32": (32, 32),
            "NH_qload_64x32": (64, 32),
            "NH_qsave_64x32": (64, 32),
        }
        expected_size = (80, 60) if bookmark else entry_sizes.get(path.stem, (64, 64))
        states = ("selected", "unselected") if bookmark else ("normal", "pressed", "disabled", "highlighted")
        prefix = path.stem.removesuffix("_button")
        for index, frame in enumerate(frames):
            name = frame["file"]
            require(name == f"{prefix}_{states[index]}.png", f"Wrong semantic state order: {path.name}")
            require(Path(name).name == name and "/" not in name and "\\" not in name,
                    f"Non-local image reference: {name}")
            require(frame["frame"] == index and frame["group"] == 0, f"Non-contiguous frames: {path.name}")
            with (images / name).open_image() as image:
                require(image.size == expected_size, f"Wrong state dimensions: {name}")
    for stem, expected_size in CUSTOM_ANIMATIONS.items():
        path = images / (stem + ".json")
        require(path.is_file(), f"Missing generated custom animation: {stem}")
        frames = json.loads(path.read_text())["images"]
        require(len(frames) == 4, f"Wrong custom state count: {stem}")
        for index, state in enumerate(("normal", "pressed", "disabled", "highlighted")):
            frame = frames[index]
            require(frame == {"group": 0, "frame": index, "file": f"{stem}_{state}.png"},
                    f"Wrong custom state order: {stem}")
            with (images / frame["file"]).open_image() as image:
                require(image.size == expected_size, f"Wrong custom state dimensions: {frame['file']}")
    for name in BUTTONS:
        require((images / f"NH_{name}_button.json").is_file(), f"Missing command control: {name}")
    require((images / "NH_hero_actions_entry.json").is_file(), "Missing entry animation")
    if hero_growth:
        require((images / "NH_hero_growth_entry.json").is_file(), "Missing growth entry animation")
        states = ("normal", "pressed", "disabled", "highlighted")
        require(len({digest(images / f"NH_hero_growth_entry_{state}.png") for state in states}) == 4,
                "Growth entry states are not distinct")
    with (images / "NH_hero_actions_back.png").open_image() as image:
        require(image.size == (640, 520), "Wrong chooser background size")
    for school in SCHOOLS:
        require((images / f"NH_{school}_bookmark.json").is_file(), f"Missing bookmark: {school}")
        with (images / f"NH_{school}_header.png").open_image() as image:
            require(image.size == (160, 96), f"Wrong school header size: {school}")
            require(image.getchannel("A").crop((0, 0, 160, 28)).getextrema() == (0, 0),
                    f"Nontransparent top28 header rows: {school}")
    for glyph in HERO_GLYPHS:
        for size in (32, 64):
            with (images / f"NH_hero_{glyph}_{size}.png").open_image() as image:
                require(image.size == (size, size), f"Wrong hero display glyph size: {glyph}/{size}")
    for school in SCHOOLS:
        for rank, rank_name in enumerate(("basic", "advanced", "expert"), start=1):
            for size_name, size in SKILL_SIZES.items():
                name = f"NH_{school}Magic_{rank_name}_{size_name}"
                if source:
                    require((source / (name + ".svg")).is_file(), f"Missing skill source: {name}")
                with (images / (name + ".png")).open_image() as image:
                    require(image.size == size, f"Wrong skill dimensions: {name}")
                if source:
                    circles = [e for e in ET.parse(source / (name + ".svg")).getroot().iter()
                               if e.tag.split("}")[-1] == "circle"]
                    markers = circles[-3:]
                    require(len(markers) == 3, f"Missing rank markers: {name}")
                    require([e.attrib.get("fill") for e in markers] ==
                            ["#f2d875"] * rank + ["#302a25"] * (3 - rank),
                            f"Wrong Basic/Advanced/Expert markers: {name}")
    files = pngs + animations
    hashes = {path.name: digest(path) for path in files}
    if reproduce:
        require(authoring_root is not None, "--reproduce requires --authoring-root")
        with tempfile.TemporaryDirectory(prefix="nh-art-audit-") as temporary:
            sandbox = Path(temporary)
            subprocess.run([sys.executable, str(ROOT / "assets/new-horizons/generate_icons.py"),
                            "--output-root", str(sandbox)], check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            generated = sandbox / "Mods/new-horizons/Images"
            for path in svgs:
                require(digest(sandbox / "assets/new-horizons/svg" / path.name) == digest(path),
                        f"Regenerated source differs: {path.name}")
                require(digest(generated / (path.stem + ".png")) == digest(images / (path.stem + ".png")),
                        f"Regenerated selected bytes differ: {path.stem}")
    require(all(digest(images / name) == value for name, value in hashes.items()),
            "Selected outputs changed during audit")
    return {"svg": len(svgs), "png": len(pngs), "provisional_skill_png": len(provisional_skill_pngs), "metamagic_prism_png": len(metamagic_prism_pngs), "approved_spell_border_png": len(approved_spell_border_pngs), "approved_spell_border_json": len(approved_spell_border_descriptors), "animation_json": len(animations),
            "reproduced": reproduce, "hero_growth_required": hero_growth, "hashes": hashes,
            "scope": "Verified NHART dimensions/state/padding; external SVG geometry only when authoring-root supplied"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reproduce", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--authoring-root", type=Path, help="Explicit external source mirror for SVG/reproduction checks")
    parser.add_argument("--hero-growth", action="store_true", help="Require the future 24px growth entry")
    args = parser.parse_args()
    if args.authoring_root:
        args.authoring_root = args.authoring_root.resolve()
        if args.authoring_root == ROOT or ROOT in args.authoring_root.parents:
            parser.error("Authoring sources must be staged outside the checkout")
    if args.reproduce and not args.authoring_root:
        parser.error("--reproduce requires --authoring-root")
    result = audit(args.reproduce, args.hero_growth, args.authoring_root)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: {result['svg']} SVG, {result['png']} PNG, {result['animation_json']} animations; "
          f"regeneration={result['reproduced']}")


if __name__ == "__main__":
    main()
