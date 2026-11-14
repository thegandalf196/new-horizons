#!/usr/bin/env python3
"""Focused hero/creature readback and ordinary army-slot wiring checks."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


def check_hero_windows() -> None:
    readout = source("client/windows/HeroWindowCommandReadout.cpp")
    compact = readout.split("void CHeroWindow::refreshLegacyCommandReadout()", 1)[1].split(
        "void CHeroWindow::refreshCommandReadout()", 1
    )[0]
    require(compact, "usesLeadershipEfficiency(curHero->getCapabilityRules())", "compact current-profile eligibility")
    require(compact, "efficiency || command.has_value()", "compact availability preserves both profiles")
    for control in ("legacyCommandArea", "legacyLeadershipLabel", "legacyLeadershipImage"):
        require(compact, f"{control}->setEnabled(available)", "compact help, value and icon availability")
    require(compact, "leadershipHeroReadout(*curHero).baseLeadership", "compact authoritative numeric Leadership")
    require(compact, "commandPresentation::commandHelp(*curHero)", "compact shared Leadership help")
    require(compact, "std::to_string(command->effective())", "historical Command value remains available")

    growth = source("client/windows/HeroGrowthWindow.cpp")
    section = growth.split("std::optional<std::string> leadershipSection", 1)[1].split(
        "void HeroGrowthWindow::selectPerkSkill", 1
    )[0]
    require(section, "usesLeadershipEfficiency(hero.getCapabilityRules())", "growth current-profile eligibility")
    require(section, "if(!efficiency && !hero.getArmyCommand())", "growth unavailable-profile rejection")
    require(section, "commandPresentation::commandHelp(hero)", "growth shared Leadership details")
    require(section, "if(!efficiency)\n\t\tfor(int slot", "retired slot details stay historical-only")
    require(growth, "const auto leadership = leadershipSection(hero)", "growth derives text and availability together")
    require(growth, "navigation.refresh({growth.has_value(), leadership.has_value()", "growth Leadership tab availability")
    require(growth, 'leadership.value_or("No saved Leadership profile for this hero.")', "growth displays resolved Leadership details")


def check_battle_defense() -> None:
    panel = source("client/battle/StackInfoBasicPanel.cpp")
    stats = panel.split("void StackInfoBasicPanel::initializeData(const CStack * stack)", 1)[1]
    require(stats, "currentDefense = creatureDefensePresentation::query(*stack, stack->isShooter())", "sidebar shared current Defense")
    require(stats, "creatureDefensePresentation::query(*LIBRARY->creatures()->getByIndex", "sidebar shared base Defense")
    require(stats, "std::to_string(currentDefense.physical)", "sidebar displays physical Defense")
    require(stats, "currentDefense.physical != currentDefense.general", "conditional Defense explanation")
    require(stats, "Defense against magical creature attacks:", "sidebar preserves general Defense distinction")
    require(stats, "Physical-only Defense bonuses do not apply against magical creature attacks.", "sidebar explains physical-only bonuses")


def main() -> None:
    check_hero_windows()
    check_battle_defense()
    helper = source("client/widgets/NewHorizonsLeadershipReadback.h")
    for field in (
        "leadership", "requirement", "maximumCount", "existingCount",
        "incomingCount", "legalIncomingCount", "resultingCount",
        "addedLeadership", "resultingDemand", "excess",
    ):
        require(helper, f"int64_t {field}", f"readback field {field}")
    for invariant in (
        "std::max<int64_t>(0, value)",
        "saturatedAdd(result.existingCount, result.incomingCount)",
        "saturatedMultiply(result.incomingCount, result.requirement)",
        "saturatedMultiply(result.resultingCount, result.requirement)",
        "result.resultingDemand - result.leadership",
        "result.maximumCount - std::min(result.maximumCount, result.existingCount)",
    ):
        require(helper, invariant, "normalized/saturating per-slot proposal math")

    ui = source("client/windows/GUIClasses.cpp")
    header = source("client/windows/GUIClasses.h")
    recruitment_ui = source("client/windows/CRecruitmentWindow.cpp")
    # The simple purchase dialog intentionally omits the retired destination
    # row. Its selected creature still uses normal authoritative slot checks.
    recruitment = recruitment_ui.split("CRecruitmentWindow::CCreatureCard::CCreatureCard", 1)[1].split(
        "CSplitWindow::CSplitWindow", 1
    )[0]
    recruitment_header = header.split("class CRecruitmentWindow", 1)[1].split("class CObjectListWindow", 1)[0]
    for removed in ("leadershipHelp", "leadershipLimit", "updateLeadershipProposalHelp"):
        assert removed not in recruitment and removed not in recruitment_header, removed
    select = recruitment_ui.split("void CRecruitmentWindow::select(std::shared_ptr<CCreatureCard> card)", 1)[1].split(
        "void CRecruitmentWindow::close", 1
    )[0]
    require(select, "UIHelper::recruitmentPosition(dst, card->creature->getId(), maxAmount)",
            "ordinary recruitment position admission")
    require(select, "if(!slot.validSlot() && !card->creature->warMachine.hasValue())",
            "full-army admission remains guarded")

    split = ui.split("void CSplitWindow::updateLeadershipReadback()", 1)[1].split("void CSplitWindow::apply()", 1)[0]
    split_header = source("client/windows/CSplitWindow.h")
    require(split_header, "initialLeftAmount", "split original left count")
    require(split_header, "initialRightAmount", "split original right count")
    require(split_header, "leftArmy", "split left receiving hero context")
    require(split_header, "rightArmy", "split right receiving hero context")
    require(ui, "leftLeadershipHelp = std::make_shared<LRClickableAreaWText>(Rect(leftSideCenter - 29, ownerMarkerY, 58, 64))",
            "split owner-marker help surface")
    require(ui, "leftLeadershipHelp->disable()", "empty split help disabled")
    require(split, "help->disable()", "invalid split proposal disables help")
    require(split, "help->enable()", "available split proposal enables help")
    require(split, "UIHelper::armySlotPreview(army, slot, splitCreature->getId())", "split shared army-slot help")
    require(split, "UIHelper::armySlotProblem(army, slot, splitCreature->getId())", "split shared placement admission")
    require(split, "ok->block(illegalSide(leftArmy, leftSlot, leftAmount)", "split confirmation uses placement admission")
    set_amount = ui.split("void CSplitWindow::setAmount(int value, bool left)", 1)[1].split(
        "void CSplitWindow::updateLeadershipReadback", 1
    )[0]
    require(set_amount, "updateLeadershipReadback()", "split input/slider refresh")
    constructor = ui.split("CSplitWindow::CSplitWindow", 1)[1].split("void CSplitWindow::setAmountText", 1)[0]
    require(constructor, "updateLeadershipReadback();", "split initial post-construction readback")

    garrison = source("client/widgets/CGarrisonInt.cpp")
    hover = garrison.split("void CGarrisonSlot::hover (bool on)", 1)[1].split(
        "const CArmedInstance * CGarrisonSlot::getObj", 1
    )[0]
    require(hover, "UIHelper::armySlotProblem(getObj(), ID, source->creature->getId())", "transfer shared placement readback")

    # Current Leadership is a damage-efficiency attribute, not a placement cap.
    # Its shared help retains the authoritative base and Authority values.
    presentation = source("client/widgets/LeadershipEfficiencyPresentation.cpp")
    texts = json.loads(source("config/newHorizonsCombatTexts.json"))
    for token in ("%BASE%", "%AUTHORITY%", "%RANK_EFFECTIVE%"):
        require(texts["new-horizons.leadership.heroHelp"], token, f"localized hero Leadership token {token}")
        require(presentation, f'boost::replace_all(text, "{token}"', f"shared hero Leadership formatting {token}")
    print("Leadership readback source guard passed")


if __name__ == "__main__":
    main()
