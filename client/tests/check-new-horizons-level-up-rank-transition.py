#!/usr/bin/env python3
"""Source guard for the approved offered-rank and parent-Skill subtitles.

This checks source wiring only. It is not a native UI, layout, or rendered
acceptance test.
"""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LEVEL = (ROOT / "client/windows/CLevelWindow.cpp").read_text(encoding="utf-8")
CHOICES = (ROOT / "client/windows/CLevelWindowChoices.cpp").read_text(encoding="utf-8")
CARDS = (ROOT / "client/windows/LevelUpChoiceCards.cpp").read_text(encoding="utf-8")
COMPONENT = (ROOT / "client/widgets/CComponent.cpp").read_text(encoding="utf-8")
TEXTS = json.loads((ROOT / "config/newHorizonsCombatTexts.json").read_text(encoding="utf-8"))


def require(source: str, needle: str, label: str) -> None:
    if needle not in source:
        raise AssertionError(f"missing {label}: {needle}")


def braced_block(source: str, marker: str) -> tuple[int, int, str]:
    """Return the start/end and text for the brace block following marker."""
    marker_at = source.index(marker)
    opening = source.index("{", marker_at)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return marker_at, index + 1, source[opening + 1:index]
    raise AssertionError(f"unterminated source block after {marker}")


def verify_rank_transition_wiring() -> None:
    _, _, skill_branch = braced_block(CHOICES, "CLevelWindow::skillComponent(")
    _, _, perk_branch = braced_block(CHOICES, "CLevelWindow::perkComponent(")
    _, _, skill_card = braced_block(CARDS, "class CLevelUpSkillCardComponent")
    require(skill_branch, "const auto skill = skills.at(index);", "rank-choice Skill identity")
    require(skill_branch, "const int offeredRank = hero->getSecSkillLevel(skill) + 1;", "actual next Skill rank")
    require(skill_card, "CSelectableComponent(ComponentType::SEC_SKILL, skill, offeredRank, CComponent::medium)",
            "native secondary Skill icon with offered rank")
    require(skill_branch, 'translate("core.skilllev", offeredRank - 1)', "rank text")
    require(skill_branch, 'newHorizonsPerkHelp::skillLabel(hero, skill)', "captured Skill name")
    require(skill_branch, 'component->customSubtitle = rankText + "\\n" + skillName;', "rank above Skill name")
    assert 'skill.unlearned' not in skill_branch and 'skill.rankTransition' not in skill_branch
    require(perk_branch, "perk.name", "perk card name")
    require(perk_branch, "newHorizonsPerkHelp::format", "captured mechanical perk help")
    require(perk_branch, "newHorizonsPerkIcon(perk.selection.perkId, perk.name, hero->getPerkState().rules)", "perk icon")
    _, _, heading = braced_block(CHOICES, "CLevelWindow::perkHeaderText()")
    require(heading, "newHorizonsPerkHelp::tierName(first.requiredRank)", "visible required-rank heading")
    require(heading, "newHorizonsPerkHelp::skillName(hero, first.selection.skillId)", "visible owning-Skill heading")
    require(LEVEL, "perkHeaderText()", "perk heading consumer")
    require(LEVEL, "Rect(44, 454, 552, 52)", "visible scrollable full mechanical description")
    require(CHOICES, "perkDescription->setText(perks[index].description)", "selected description consumer")
    require(LEVEL, "perks = skills.empty() ? availablePerks", "separate Skill and mandatory perk modes")
    require(LEVEL, "if(!selectionSubmitted && !choiceOrder.empty())", "mandatory choice cannot be dismissed")

    component_subtitle = COMPONENT.split("std::string CComponent::getSubtitle() const", 1)[1]
    require(component_subtitle, "if (!customSubtitle.empty())", "custom subtitle readback")
    require(component_subtitle, "case ComponentType::SEC_SKILL:", "generic SEC_SKILL fallback")
    require(component_subtitle, "data.value.value_or(1)-1", "generic offered-rank value preservation")
    require(component_subtitle, 'translate("core.skilllev", data.value.value_or(1)-1)', "original-mode rank subtitle")

    unlearned = TEXTS.get("new-horizons.levelUp.skill.unlearned")
    transition = TEXTS.get("new-horizons.levelUp.skill.rankTransition")
    assert unlearned and unlearned.strip(), "missing localized unlearned text"
    assert transition and transition.count("%s") == 2, "rank transition needs current and offered placeholders"

    ranks = ("Basic", "Advanced", "Expert")
    assert [ranks[(current + 1) - 1] for current in range(3)] == list(ranks)


def main() -> None:
    verify_rank_transition_wiring()
    print("New Horizons offered-rank and parent-Skill source wiring passed (not rendered acceptance)")


if __name__ == "__main__":
    main()
