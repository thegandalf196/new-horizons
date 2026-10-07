#!/usr/bin/env python3
"""Source guard for New Horizons level-up Skill-rank subtitles.

This checks source wiring only. It is not a native UI, layout, or rendered
acceptance test.
"""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
LEVEL = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
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
    _, _, create_skill_box = braced_block(LEVEL, "void CLevelWindow::createSkillBox()")
    skill_start, skill_end, skill_branch = braced_block(
        create_skill_box, "if(originalIndex < skills.size())"
    )
    else_at = create_skill_box.index("else", skill_end)
    _, _, perk_branch = braced_block(create_skill_box, "else")
    assert skill_start < skill_end < else_at

    require(skill_branch, "const auto skill = skills[originalIndex];", "rank-choice Skill identity")
    require(skill_branch, "const int currentRank = hero->getSecSkillLevel(skill);", "actual current Skill rank")
    require(skill_branch, "const int offeredRank = currentRank + 1;", "next-rank offer")
    require(
        skill_branch,
        "ComponentType::SEC_SKILL, skill,\n\t\t\t\t\tofferedRank, CComponent::medium",
        "existing integer SEC_SKILL component with offered rank",
    )

    # The exact 0/1/2 index mapping: zero has no current rank, while positive
    # values and the offered rank use the existing Basic/Advanced/Expert table.
    require(skill_branch, "if(newHorizonsMagic::rulesActive(hero->getMagicRules()))", "saved New Horizons gate")
    require(skill_branch, "currentRank == 0", "unlearned current-rank branch")
    require(
        skill_branch,
        'translate("new-horizons.levelUp.skill.unlearned")',
        "localized unlearned label for rank zero",
    )
    require(
        skill_branch,
        'translate("core.skilllev", currentRank - 1)',
        "current-rank Basic/Advanced mapping",
    )
    require(
        skill_branch,
        'translate("core.skilllev", offeredRank - 1)',
        "offered-rank Basic/Advanced/Expert mapping",
    )
    require(
        skill_branch,
        'MetaString::createFromTextID("new-horizons.levelUp.skill.rankTransition")',
        "localized current-to-offered template",
    )
    current_replacement = skill_branch.index("transition.replaceRawString(currentRankText)")
    offered_replacement = skill_branch.index("transition.replaceRawString(offeredRankText)")
    assert current_replacement < offered_replacement, "rank transition must show current before offered"
    require(skill_branch, 'LIBRARY->skillh->getById(skill)->getNameTranslated()', "Skill name on second subtitle line")
    require(skill_branch, 'comp->customSubtitle = transition.toString(&GAME->translator()) + "\\n"', "rank subtitle override")

    assert "newHorizonsMagic::rulesActive" not in perk_branch, "rank transition gate must not alter perk cards"
    assert "rankTransition" not in perk_branch, "perk cards must keep their separate current presentation"
    require(perk_branch, "perk.requiredRank - 1", "existing perk prerequisite rank subtitle")
    require(perk_branch, "perk.name", "existing perk name subtitle")
    require(perk_branch, "newHorizonsPerkHelp::format", "existing perk help")
    require(perk_branch, "newHorizonsPerkIcon(perk.selection.perkId)", "existing perk icon")

    component_subtitle = COMPONENT.split("std::string CComponent::getSubtitle() const", 1)[1]
    require(component_subtitle, "if (!customSubtitle.empty())", "custom subtitle readback")
    require(component_subtitle, "case ComponentType::SEC_SKILL:", "generic SEC_SKILL fallback")
    require(component_subtitle, "data.value.value_or(1)-1", "generic offered-rank value preservation")
    require(component_subtitle, 'translate("core.skilllev", data.value.value_or(1)-1)', "original-mode rank subtitle")

    unlearned = TEXTS.get("new-horizons.levelUp.skill.unlearned")
    transition = TEXTS.get("new-horizons.levelUp.skill.rankTransition")
    assert unlearned and unlearned.strip(), "missing localized unlearned text"
    assert transition and transition.count("%s") == 2, "rank transition needs current and offered placeholders"

    expected = {
        0: (unlearned, "Basic"),
        1: ("Basic", "Advanced"),
        2: ("Advanced", "Expert"),
    }
    assert all(current and offered for current, offered in expected.values())


def main() -> None:
    verify_rank_transition_wiring()
    print("New Horizons level-up rank transition source wiring passed (not rendered acceptance)")


if __name__ == "__main__":
    main()
