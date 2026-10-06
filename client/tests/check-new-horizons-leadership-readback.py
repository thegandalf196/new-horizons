#!/usr/bin/env python3
"""Focused wiring checks for requested per-slot Leadership readback."""

import json
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, needle: str, label: str) -> None:
    if needle not in text:
        raise AssertionError(f"missing {label}: {needle}")


def main() -> None:
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
    recruitment = ui.split("void CRecruitmentWindow::updateLeadershipProposalHelp", 1)[1].split(
        "void CRecruitmentWindow::close", 1
    )[0]
    require(header, "std::shared_ptr<LRClickableAreaWText> leadershipHelp", "existing recruitment-row help area")
    require(ui, "Rect(63 + layoutOffsetX, 207, 360, 14)", "native recruitment Leadership row hitbox")
    require(recruitment, "leadershipHelp->text.clear()", "stale recruitment help clearing")
    require(recruitment, "leadershipHelp->hoverText.clear()", "stale recruitment hover clearing")
    require(recruitment, "if(!selected || !hero)", "no-selection/no-hero gate")
    require(recruitment, "if(!capacity)", "no-capacity gate")
    require(recruitment, "leadershipHelp->disable()", "empty recruitment help is disabled")
    require(recruitment, "leadershipHelp->enable()", "available recruitment help is enabled")
    require(recruitment, "getNewHorizonsLeadershipProposalText(*capacity, existing, proposedCount)",
            "requested recruitment proposal readback")
    slider = ui.split("void CRecruitmentWindow::sliderMoved(int to)", 1)[1].split("CSplitWindow::CSplitWindow", 1)[0]
    require(slider, "updateLeadershipProposalHelp(to)", "slider refresh")
    select = ui.split("void CRecruitmentWindow::select(std::shared_ptr<CCreatureCard> card)", 1)[1].split(
        "void CRecruitmentWindow::updateLeadershipProposalHelp", 1
    )[0]
    require(select, "updateLeadershipProposalHelp(0)", "deselection clears proposal help")

    split = ui.split("void CSplitWindow::updateLeadershipReadback()", 1)[1].split("void CSplitWindow::apply()", 1)[0]
    require(header, "initialLeftAmount", "split original left count")
    require(header, "initialRightAmount", "split original right count")
    require(header, "leftArmy", "split left receiving hero context")
    require(header, "rightArmy", "split right receiving hero context")
    require(ui, "leftLeadershipHelp = std::make_shared<LRClickableAreaWText>(Rect(leftSideCenter - 29, ownerMarkerY, 58, 64))",
            "split owner-marker help surface")
    require(ui, "leftLeadershipHelp->disable()", "empty split help disabled")
    require(split, "help->disable()", "invalid split proposal disables help")
    require(split, "help->enable()", "available split proposal enables help")
    require(split, "const auto * hero = dynamic_cast<const CGHeroInstance *>(army)", "split receiving-hero gate")
    require(split, "if(!capacity)", "split no-capacity gate")
    require(split, "std::min(normalizedInitial, normalizedCurrent)", "existing split-stack baseline")
    require(split, "std::max<int64_t>(0, normalizedCurrent - normalizedInitial)",
            "incoming split proposal")
    require(split, "getNewHorizonsLeadershipProposalText(*capacity, existingCount, incomingCount)",
            "split proposal readback")
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
    require(hover, "incoming--", "source-last-stack proposal adjustment")
    require(hover, "creature == source->creature ? myStack->getCount() : 0", "merge vs empty/swap proposal baseline")
    require(hover, "getNewHorizonsLeadershipProposalSummary(*capacity, existing, incoming)",
            "transfer requested/legal/excess readback")

    ui_helper = source("client/UIHelper.cpp")
    for token in (
        '"%ADDED%"', '"%DEMAND%"', '"%CAPACITY%"', '"%MAX%"', '"%LEGAL%"',
        '"%EXCESS%"', '"%RESULTING%"',
    ):
        require(ui_helper, f"replaceTokenNumber({token}", f"complete localized token {token}")

    texts = json.loads(source("config/newHorizonsCombatTexts.json"))
    expected_text_tokens = {
        "new-horizons.combat.leadership.proposal": ("%ADDED%", "%DEMAND%", "%CAPACITY%", "%MAX%", "%LEGAL%"),
        "new-horizons.combat.leadership.proposalExcess": ("%EXCESS%",),
        "new-horizons.combat.leadership.transfer": ("%RESULTING%", "%MAX%", "%ADDED%", "%LEGAL%"),
        "new-horizons.combat.leadership.transferExcess": ("%EXCESS%",),
    }
    for key, tokens in expected_text_tokens.items():
        if key not in texts:
            raise AssertionError(f"missing localized Leadership text: {key}")
        for token in tokens:
            require(texts[key], token, f"localized placeholder {token} in {key}")
    print("UP261 Leadership readback source guard passed")


if __name__ == "__main__":
    main()
