#!/usr/bin/env python3
"""Source guard for the New Horizons level-up presentation contract."""

from pathlib import Path
import hashlib
import json
import re
from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
LEVEL = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
LEVEL_HEADER = (ROOT / "client/windows/GUIClasses.h").read_text(encoding="utf-8")
HERO = (ROOT / "client/windows/CHeroWindow.cpp").read_text(encoding="utf-8")
COMPONENT = (ROOT / "client/widgets/CComponent.cpp").read_text(encoding="utf-8")
COMPONENT_HOLDER = (ROOT / "client/widgets/CComponentHolder.cpp").read_text(encoding="utf-8")
ASSETS = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
PERK_ICONS = (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(encoding="utf-8")


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise AssertionError(f"missing {label}: {needle}")


def verify_paged_columns() -> None:
    """Exercise the same two-per-category paging contract used by the UI."""
    for skill_count, perk_count in ((2, 2), (3, 1), (1, 3), (5, 4)):
        pages = max((skill_count + 1) // 2, (perk_count + 1) // 2)
        seen_skills = set()
        seen_perks = set()
        for page in range(pages):
            skills = list(range(page * 2, min(skill_count, page * 2 + 2)))
            perks = list(range(page * 2, min(perk_count, page * 2 + 2)))
            assert len(skills) <= 2 and len(perks) <= 2
            seen_skills.update(skills)
            seen_perks.update(perks)
        assert seen_skills == set(range(skill_count))
        assert seen_perks == set(range(perk_count))


def main() -> None:
    verify_paged_columns()
    # The layout routine moves the fallback widgets, so they must exist first.
    if HERO.index("configureNewHorizonsLayout();") < HERO.index("learnedPerksSummary = std::make_shared"):
        raise AssertionError("New Horizons hero layout runs before fallback widgets are constructed")
    require(LEVEL, '"newHorizonsLevelUpBackground.png"', "taller level-up background")
    require(LEVEL, 'AnimationPath::builtin("PSKIL42")', "canonical primary skill icon animation")
    require(LEVEL, 'Rect(48, 308, 135, 148)', "reserved skill column")
    require(LEVEL, 'Rect(199, 308, 135, 148)', "reserved perk column")
    require(LEVEL, 'comp->setHorizontalLayout(135, 70)', "bounded horizontal choice cards")
    require(COMPONENT, 'width - image->pos.w - 12', "wrapped caption beside native icon")
    require(COMPONENT, 'pos.h = height', "fixed component height before box layout")
    require(LEVEL, 'Point(23, 480)', "paging below choice cards")
    require(LEVEL, 'Point(65, 480)', "next-page control clear of confirmation")
    require(LEVEL, 'Point(296, 480)', "confirmation below choice cards")
    require(LEVEL, 'if(!skillComps.empty())', "no empty skill component box")
    require(LEVEL, 'if(!perkComps.empty())', "no empty perk component box")
    # CComponentBox vertically centers its total row height; prove the worst
    # supported page fits before that math runs, rather than allowing a
    # negative centering offset and overflowing the confirmation/footer.
    row_height, row_gap, column_top, column_height = 70, 8, 308, 148
    for count in (1, 2):
        content_height = count * row_height + (count - 1) * row_gap
        assert content_height <= column_height
        first_top = column_top + (column_height - content_height) // 2
        last_bottom = first_top + content_height
        assert first_top > 290 + 8
        assert last_bottom <= 456 < 480
    assert 48 + 135 < 199 and 199 + 135 < 384 - 12
    assert 480 + 32 <= 528 - 12
    background = ASSETS.split("AssetGenerator::CanvasPtr AssetGenerator::createNewHorizonsLevelUpBackground()", 1)[1].split(
        "AssetGenerator::CanvasPtr AssetGenerator::createStackExperienceDialogBackground", 1)[0]
    require(background, 'createDialogBackground(size)', "clean lower choice texture")
    require(background, 'Rect(0, topFrame, sideFrame, height)', "only side frame repeats")
    assert 'Rect(0, topFrame, source->width(), height)' not in background, "decorated middle repeats behind offers"
    require(LEVEL, "category.size() < 2", "two-choice category cap")
    require(LEVEL, "choicePageCount", "per-category paging")
    require(LEVEL, "skillChoiceBox->setShortcuts", "global skill choice shortcuts")
    require(LEVEL, "perkChoiceBox->setShortcuts", "global perk choice shortcuts")
    require(LEVEL, "shortcutIndex++", "row-major 1-4 choice shortcuts")
    require(LEVEL, "skillChoiceBox->clearSelection", "cross-column selection clearing")
    require(LEVEL, "perkChoiceBox->clearSelection", "cross-column selection clearing")
    require(LEVEL, "skillChoiceBox->selectFirst", "default skill selection")
    require(LEVEL, 'choiceHeaders.push_back', "skill/perk column headers")
    require(LEVEL, "displayedChoiceOrder", "category-aware choice ordering")
    require(LEVEL, '"core:necromancy"', "classic Necromancy perk icon alias")
    require(LEVEL, 'newHorizonsPerkIcon(perk.selection.perkId)', "shared perk icon lookup")
    require(PERK_ICONS, '"new-horizons:necromancy.boneCollector"', "preserved Bone Collector icon key")
    require(PERK_ICONS, '"NH_perk_neutral"', "unknown-perk fallback icon")
    require(LEVEL_HEADER, "displayedChoiceOrder", "persisted displayed choice mapping")
    require(HERO, "perkState.selected", "saved perk selections on hero screen")
    require(HERO, '"Learned"', "learned perk presentation")
    require(HERO, '"NH_perk_bone_collector"', "Bone Collector hero icon")
    require(HERO, '"NH_perk_neutral"', "neutral hero perk icon")
    require(HERO, "area->text.clear()", "stale empty-row text clearing")
    require(HERO, "area->disable()", "stale empty-row hit-area clearing")
    require(HERO, '"NH_capability_leadership_32"', "painted legacy Leadership icon")
    require(HERO, '"NH_capability_siege_32"', "painted legacy Siege icon")
    require(HERO, 'newHorizonsPerkIcon(hasPerk ?', "shared learned-perk icon lookup")
    require(HERO, 'capabilityIcons.push_back', "new-layout capability art")
    require(HERO, 'Rect(342, 404, 65, 24)', "bounded legacy perk summary")
    require(HERO, 'Rect(314, 404, 24, 24)', "legacy strip clear of secondary skills")
    require(HERO, 'Rect(506, 404, 24, 24)', "legacy strip clear of artifacts")
    require(HERO, 'move(learnedPerksSummary, Point(342, 404))', "restored legacy perk placement")
    require(HERO, 'move(legacyBoneCollectorImage, Point(314, 404))', "restored legacy strip placement")
    require(HERO, 'legacyLeadershipLabel->disable()', "classic hero capability icon gating")
    require(HERO, 'cellLabels.at(labelIndex)->setText("")', "non-overlapping perk marker")
    require(HERO, "area->hoverText", "learned perk description tooltip")
    require(HERO, "if(learnedSkill && hasPerk)", "only learned perk slots show icons")
    require(HERO, "setEnabled(learnedSkill && hasPerk)", "only learned perk slots show labels")
    require(HERO, "if(!learnedSkill || !hasPerk)", "empty perk slot skips hit area activation")
    require(HERO, 'cellLabels.at(labelIndex + label)->setText("")', "clear stale perk captions")
    assert '"Unbound"' not in HERO and '"slot"' not in HERO
    assert "No learned perk in this slot." not in HERO
    require(COMPONENT, 'SecondarySkill::NECROMANCY', "classic Necromancy component icon")
    require(COMPONENT, "setCustomIcon", "perk-specific component icon override")
    require(COMPONENT_HOLDER, 'SecondarySkill::NECROMANCY', "classic Necromancy hero icon")
    require(ASSETS, 'createNewHorizonsLevelUpBackground', "generated tall background registration")
    for stem, size in {
        "NH_orders_gauntlet": (48, 36),
        "NH_perk_bone_collector": (44, 44),
        "NH_perk_neutral": (44, 44),
    }.items():
        animation = ROOT / "Mods/new-horizons/Images" / f"{stem}.json"
        if not animation.is_file():
            raise AssertionError(f"missing {stem} animation asset")
        for state in ("normal", "pressed", "disabled", "highlighted"):
            with Image.open(ROOT / "Mods/new-horizons/Images" / f"{stem}_{state}.png") as image:
                if image.size != size:
                    raise AssertionError(f"wrong {stem} {state} dimensions: {image.size}")
    mapping = dict(re.findall(r'\{"(new-horizons:[^"]+)", "([^"]+)"\}', PERK_ICONS))
    definitions = json.loads((ROOT / "config/newHorizonsPerks.json").read_text())
    active = {perk["id"] for skill in definitions["skills"].values()
              for perk in skill["perks"] if perk["effect"]["status"] == "active"}
    assert active <= mapping.keys(), f"Active perks lack art: {active - mapping.keys()}"
    assert len({mapping[perk] for perk in active}) == len(active), "Active perks share an icon"
    images = ROOT / "Mods/new-horizons/Images"
    hashes = set()
    for perk in sorted(active):
        key = mapping[perk]
        descriptor = json.loads((images / f"{key}.json").read_text())
        assert [frame["frame"] for frame in descriptor["images"]] == list(range(4))
        for frame in descriptor["images"]:
            with Image.open(images / frame["file"]) as image:
                assert image.size == (44, 44), (perk, image.size)
        digest = hashlib.sha256((images / descriptor["images"][0]["file"]).read_bytes()).hexdigest()
        assert digest not in hashes, f"Duplicate active perk painting: {perk}"
        hashes.add(digest)
    for capability in ("leadership", "siege"):
        descriptor = json.loads((images / f"NH_capability_{capability}_32.json").read_text())
        assert descriptor["images"] == [{"group": 0, "frame": 0, "file": f"NH_capability_{capability}_32.png"}]
        for suffix, size in (("_normal", 44), ("_32", 32)):
            with Image.open(images / f"NH_capability_{capability}{suffix}.png") as image:
                assert image.size == (size, size)
    print(f"New Horizons level-up layout and {len(active)} distinct active perk icons passed")


if __name__ == "__main__":
    main()
