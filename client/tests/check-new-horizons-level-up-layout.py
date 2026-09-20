#!/usr/bin/env python3
"""Source guard for the New Horizons level-up presentation contract."""

from pathlib import Path
from PIL import Image


ROOT = Path(__file__).resolve().parents[2]
LEVEL = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
LEVEL_HEADER = (ROOT / "client/windows/GUIClasses.h").read_text(encoding="utf-8")
HERO = (ROOT / "client/windows/CHeroWindow.cpp").read_text(encoding="utf-8")
COMPONENT = (ROOT / "client/widgets/CComponent.cpp").read_text(encoding="utf-8")
COMPONENT_HOLDER = (ROOT / "client/widgets/CComponentHolder.cpp").read_text(encoding="utf-8")
ASSETS = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")


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
    require(LEVEL, 'Rect(48, 308, 135, 168)', "reserved skill column")
    require(LEVEL, 'Rect(199, 308, 135, 168)', "reserved perk column")
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
    require(LEVEL, '"new-horizons:necromancy.boneCollector"', "Bone Collector icon key")
    require(LEVEL, '"NH_perk_neutral"', "neutral missing-perk fallback icon")
    require(LEVEL_HEADER, "displayedChoiceOrder", "persisted displayed choice mapping")
    require(HERO, "perkState.selected", "saved perk selections on hero screen")
    require(HERO, '"Learned"', "learned perk presentation")
    require(HERO, '"NH_perk_bone_collector"', "Bone Collector hero icon")
    require(HERO, '"NH_perk_neutral"', "neutral hero perk icon")
    require(HERO, "area->text.clear()", "stale empty-row text clearing")
    require(HERO, "area->disable()", "stale empty-row hit-area clearing")
    require(HERO, '"NH_hero_leadership_32"', "legacy Leadership icon")
    require(HERO, '"NH_hero_siege_32"', "legacy Siege icon")
    require(HERO, 'Rect(342, 404, 65, 24)', "bounded legacy perk summary")
    require(HERO, 'Rect(314, 404, 24, 24)', "legacy strip clear of secondary skills")
    require(HERO, 'Rect(506, 404, 24, 24)', "legacy strip clear of artifacts")
    require(HERO, 'move(learnedPerksSummary, Point(342, 404))', "restored legacy perk placement")
    require(HERO, 'move(legacyBoneCollectorImage, Point(314, 404))', "restored legacy strip placement")
    require(HERO, 'legacyLeadershipLabel->disable()', "classic hero capability icon gating")
    require(HERO, 'cellLabels.at(labelIndex)->setText("")', "non-overlapping perk marker")
    require(HERO, "area->hoverText", "learned perk description tooltip")
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
    print("New Horizons level-up layout source checks passed")


if __name__ == "__main__":
    main()
