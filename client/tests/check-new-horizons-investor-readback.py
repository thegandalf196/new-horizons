#!/usr/bin/env python3
"""Source guard for the read-only Investor contribution in the resource popup."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UI = (ROOT / "client/adventureMap/CResDataBar.cpp").read_text(encoding="utf-8")
HERO = (ROOT / "lib/mapObjects/CGHeroInstance.h").read_text(encoding="utf-8")
MODULE = (ROOT / "Mods/new-horizons/mod.json").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def main() -> None:
    summary = UI.split("MetaString investorIncomeSummary(", 1)[1].split("\n}\n}", 1)[0]
    for condition in (
        "newHorizonsMagic::rulesActive(magicRules)",
        "playerState.getHeroes()",
        'hero->hasActivePerk("new-horizons:estates", "new-horizons:estates.investor")',
        'summary.appendTextID("new-horizons.economy.investor.header")',
        'heroContribution.appendTextID("new-horizons.economy.investor.hero")',
        'heroContribution.replaceTokenTextID("%HERO", hero->getNameTextID())',
        'heroContribution.replaceTokenNumber("%GOLD", hero->getNewHorizonsInvestorDailyGold())',
        'summary.appendTextID("new-horizons.economy.investor.help")',
    ):
        require(summary, condition, "owner/active/saved localized readback")

    row = summary.split("MetaString heroContribution;", 1)[1].split("summary.append(heroContribution);", 1)[0]
    if "getResourceAmount" in summary or "dailyIncome" in summary:
        raise AssertionError("Investor readback must use its saved weekly snapshot, not recompute income")
    if "if(" in row or "if (" in row:
        raise AssertionError("zero-valued and legacy-default Investor snapshots must still get a row")

    popup = UI.split("void CResDataBar::showPopupWindow(", 1)[1]
    require(popup, "investorIncomeSummary(*playerState, GAME->interface()->cb->getMagicRules())",
            "saved-rules presentation gate")
    require(popup, "CInfoWindow::genText(popupText, investorSummary.toString(&GAME->translator()))",
            "localized section in the existing native popup")
    require(popup, "CRClickPopup::createAndPush(popupText, comp)", "unchanged resource components")

    require(HERO, "int32_t newHorizonsInvestorDailyGold = 0;", "legacy-safe default snapshot")
    legacyRead = HERO.split("else if(!h.saving)\n\t\t\tnewHorizonsInvestorDailyGold = 0;", 1)
    if len(legacyRead) != 2:
        raise AssertionError("older saves must leave an explicit zero Investor snapshot")

    for key in (
        '"new-horizons.economy.investor.header"',
        '"new-horizons.economy.investor.hero"',
        '"new-horizons.economy.investor.help"',
    ):
        require(MODULE, key, "registered localized Investor copy")

    print("PASS: owned active Investors use saved weekly amounts, including zero/legacy defaults, in the existing popup")
    print("Source wiring only; native popup fit and rendered acceptance remain unverified")


if __name__ == "__main__":
    main()
