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
FOUR_LEVEL = (ROOT / "client/windows/CFourOfferLevelWindow.cpp").read_text(encoding="utf-8")
FOUR_CHOICES = (ROOT / "client/windows/CFourOfferLevelChoices.cpp").read_text(encoding="utf-8")
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
    require(perk_branch, "newHorizonsHybridCapturedPerkIcon(perk.selection.perkId, perk.name, hero->getPerkState().rules)", "perk icon")
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

    # A Basic parent upgraded externally to Expert can retain exactly its four
    # Basic perk choices. Eligibility equality alone must not freeze its caption.
    require(FOUR_LEVEL, "hero->getPerkSkillRank(offers.perkSkills[group])", "actual held parent rank readback")
    require(FOUR_LEVEL, "const auto oldRanks = currentRanks;", "retained presentation rank signature")
    require(FOUR_LEVEL, "&& currentRanks == oldRanks", "rank-only changes invalidate presentation")
    require(FOUR_CHOICES, "const int rank = currentRanks[slot];", "perk parent caption uses the compared rank")
    require(FOUR_CHOICES, "const int current = currentRanks[slot];", "Skill caption uses the compared rank")

    ranks = ("Basic", "Advanced", "Expert")
    assert [ranks[(current + 1) - 1] for current in range(3)] == list(ranks)


def verify_four_offer_presentation() -> None:
    require(FOUR_CHOICES, "const int x = slot % 2 == 0 ? 44 : 344;", "original offer columns")
    require(FOUR_CHOICES, "fourOfferLayout::OFFER_ROW_TOP[slot < 2 ? 0 : 1]", "Skills above perks")
    assert "CTextBox" not in FOUR_CHOICES, "full descriptions are right-click help, not a scrolling box"
    require(FOUR_CHOICES, 'currentName + " -> " + nextName', "font-safe rank transition")
    assert "→" not in FOUR_CHOICES
    assert "TransparentFilledRectangle" not in FOUR_CHOICES
    assert "canvas.drawColor" not in FOUR_CHOICES and "canvas.drawBorder" not in FOUR_CHOICES
    # The common component retains native icon-only selection feedback.
    require(COMPONENT, "Rect::createAround(image->pos, 1), Colors::BRIGHT_YELLOW", "icon selection border")
    assets = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
    _, _, background = braced_block(assets, "AssetGenerator::CanvasPtr AssetGenerator::createNewHorizonsLevelUpBackground()")
    assert "recessed(" not in background
    assert "drawBorder" not in background


def verify_single_perk_confirmation() -> None:
    actions = (ROOT / "client/windows/CFourOfferLevelActions.cpp").read_text(encoding="utf-8")
    _, _, confirm = braced_block(actions, "void CFourOfferLevelWindow::confirmSelection()")
    require(confirm, "if(waiting || resolved)", "pending request guard")
    require(confirm, "if(!selectedPerk)", "explicit perk selection")
    require(confirm, "submitReply(perkReply(*openGroup, *selectedPerk));", "one-checkmark final submission")
    assert "perkChoiceBlocksTierCompletion" not in confirm and "Confirm again" not in confirm
    _, _, submit = braced_block(actions, "void CFourOfferLevelWindow::submitReply(int reply)")
    assert submit.index("waiting = true;") < submit.index("submit(reply)")


def main() -> None:
    verify_rank_transition_wiring()
    verify_four_offer_presentation()
    verify_single_perk_confirmation()
    print("New Horizons offered-rank and parent-Skill source wiring passed (not rendered acceptance)")


if __name__ == "__main__":
    main()
