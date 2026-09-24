#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Offline first-slice art checks. Reads no purchaser assets; never launches a GUI.

Requires Pillow (also required by the artwork generator). Optional regeneration
runs a reviewed copy of that generator in temporary storage, never over product
outputs. This checks reproducibility/geometry, not artistic rights or game rendering.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

from PIL import Image

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


def audit(reproduce, hero_growth=False):
    source = ROOT / "assets/new-horizons/svg"
    images = ROOT / "Mods/new-horizons/Images"
    svgs = sorted(source.glob("*.svg"))
    provisional_skill_pngs = sorted(p for p in images.glob("*.png") if is_provisional_skill_png(p))
    metamagic_prism_pngs = sorted(
        p for p in images.glob("*.png") if METAMAGIC_PRISM_PNG.fullmatch(p.name)
    )
    approved_spell_border_pngs = sorted(
        p for p in images.glob("*.png") if APPROVED_SPELL_BORDER_PNG.fullmatch(p.name)
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
        p for p in images.glob("*.json") if APPROVED_SPELL_BORDER_JSON.fullmatch(p.name)
    )
    require(
        len(approved_spell_border_descriptors) == len(SCHOOLS),
        "Spell-border descriptor inventory is incomplete; use the school-art audit",
    )
    pngs = sorted(p for p in images.glob("*.png")
                  if not is_non_vector_png(p)
                  and not any(p.stem == name or p.name.startswith(name + "_") for name in CUSTOM_ANIMATIONS))
    animations = sorted(
        p for p in images.glob("*.json")
        if p.stem not in CUSTOM_ANIMATIONS
        and APPROVED_SPELL_BORDER_JSON.fullmatch(p.name) is None
    )
    require(bool(svgs) and bool(animations), "Missing artwork")
    require({p.stem for p in svgs} == {p.stem for p in pngs}, "SVG/PNG inventory mismatch")
    for path in svgs:
        tree = ET.parse(path).getroot()
        require(all(element.tag.split("}")[-1] in {"svg", "polygon", "polyline", "circle"}
                    for element in tree.iter()), f"Unexpected non-geometric SVG element: {path.name}")
        require(all(not key.lower().endswith("href") and "url(" not in value.lower()
                    for element in tree.iter() for key, value in element.attrib.items()),
                f"External/embedded SVG reference: {path.name}")
        with Image.open(images / (path.stem + ".png")) as image:
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
            with Image.open(images / name) as image:
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
            with Image.open(images / frame["file"]) as image:
                require(image.size == expected_size, f"Wrong custom state dimensions: {frame['file']}")
    for name in BUTTONS:
        require((images / f"NH_{name}_button.json").is_file(), f"Missing command control: {name}")
    require((images / "NH_hero_actions_entry.json").is_file(), "Missing entry animation")
    if hero_growth:
        require((images / "NH_hero_growth_entry.json").is_file(), "Missing growth entry animation")
        states = ("normal", "pressed", "disabled", "highlighted")
        require(len({digest(images / f"NH_hero_growth_entry_{state}.png") for state in states}) == 4,
                "Growth entry states are not distinct")
    with Image.open(images / "NH_hero_actions_back.png") as image:
        require(image.size == (640, 520), "Wrong chooser background size")
    for school in SCHOOLS:
        require((images / f"NH_{school}_bookmark.json").is_file(), f"Missing bookmark: {school}")
        with Image.open(images / f"NH_{school}_header.png") as image:
            require(image.size == (160, 96), f"Wrong school header size: {school}")
            require(image.getchannel("A").crop((0, 0, 160, 28)).getextrema() == (0, 0),
                    f"Nontransparent top28 header rows: {school}")
    for glyph in HERO_GLYPHS:
        for size in (32, 64):
            with Image.open(images / f"NH_hero_{glyph}_{size}.png") as image:
                require(image.size == (size, size), f"Wrong hero display glyph size: {glyph}/{size}")
    for school in SCHOOLS:
        for rank, rank_name in enumerate(("basic", "advanced", "expert"), start=1):
            for size_name, size in SKILL_SIZES.items():
                name = f"NH_{school}Magic_{rank_name}_{size_name}"
                require((source / (name + ".svg")).is_file(), f"Missing skill source: {name}")
                with Image.open(images / (name + ".png")) as image:
                    require(image.size == size, f"Wrong skill dimensions: {name}")
                circles = [e for e in ET.parse(source / (name + ".svg")).getroot().iter()
                           if e.tag.split("}")[-1] == "circle"]
                markers = circles[-3:]
                require(len(markers) == 3, f"Missing rank markers: {name}")
                require([e.attrib.get("fill") for e in markers] ==
                        ["#f2d875"] * rank + ["#302a25"] * (3 - rank),
                        f"Wrong Basic/Advanced/Expert markers: {name}")
    files = svgs + pngs + animations
    hashes = {str(path.relative_to(ROOT)): digest(path) for path in files}
    if reproduce:
        # The reviewed generator derives output roots from __file__, not cwd.
        with tempfile.TemporaryDirectory(prefix="nh-art-audit-") as temporary:
            sandbox = Path(temporary)
            copied = sandbox / "assets/new-horizons/generate_icons.py"
            copied.parent.mkdir(parents=True)
            shutil.copyfile(ROOT / "assets/new-horizons/generate_icons.py", copied)
            subprocess.run([sys.executable, str(copied)], cwd=sandbox, check=True,
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=60)
            for directory in ("assets/new-horizons/svg", "Mods/new-horizons/Images"):
                expected = {
                    p.name for p in (ROOT / directory).iterdir()
                    if p.is_file() and not (directory.endswith("Images") and is_non_vector_artifact(p))
                }
                actual = {
                    p.name for p in (sandbox / directory).iterdir()
                    if p.is_file() and not (directory.endswith("Images") and is_non_vector_artifact(p))
                }
                require(expected == actual, f"Regenerated inventory mismatch: {directory}")
            for name, value in hashes.items():
                require(digest(sandbox / name) == value, f"Regenerated bytes differ: {name}")
    require(all(digest(ROOT / name) == value for name, value in hashes.items()),
            "Source outputs changed during audit")
    return {"svg": len(svgs), "png": len(pngs), "provisional_skill_png": len(provisional_skill_pngs), "metamagic_prism_png": len(metamagic_prism_pngs), "approved_spell_border_png": len(approved_spell_border_pngs), "approved_spell_border_json": len(approved_spell_border_descriptors), "animation_json": len(animations),
            "reproduced": reproduce, "hero_growth_required": hero_growth, "hashes": hashes,
            "scope": "Offline geometry/dimensions/state/padding checks; not gameplay or rights clearance"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--reproduce", action="store_true")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--hero-growth", action="store_true", help="Require the future 24px growth entry")
    args = parser.parse_args()
    result = audit(args.reproduce, args.hero_growth)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + "\n")
    print(f"PASS: {result['svg']} SVG, {result['png']} PNG, {result['animation_json']} animations; "
          f"regeneration={result['reproduced']}")


if __name__ == "__main__":
    main()
