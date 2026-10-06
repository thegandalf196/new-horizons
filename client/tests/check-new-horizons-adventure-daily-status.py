#!/usr/bin/env python3
"""Source guard for the Adventure Spell daily-use status in the spellbook."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "client/windows/CSpellWindow.h").read_text(encoding="utf-8")
UI = (ROOT / "client/windows/CSpellWindow.cpp").read_text(encoding="utf-8")
MODULE = (ROOT / "Mods/new-horizons/mod.json").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def method(source: str, signature: str, next_signature: str) -> str:
    return source.split(signature, 1)[1].split(next_signature, 1)[0]


def main() -> None:
    require(HEADER, "std::shared_ptr<CLabel> adventureSpellDailyStatusLabel;",
            "native spellbook status label")
    require(HEADER, "std::shared_ptr<LRClickableAreaWText> adventureSpellDailyStatusHelp;",
            "status hover/right-click help area")
    require(HEADER, "void updateAdventureSpellDailyStatus();", "refresh helper")
    require(HEADER, "bool isAdventureSpellUsedToday(SpellID spell) const;", "slot availability helper")

    constructor = method(UI, "CSpellWindow::CSpellWindow(", "CSpellWindow::~CSpellWindow()")
    require(constructor, "const int adventureStatusX = 300 + offR / 2;", "native small/large horizontal placement")
    require(constructor, "const int adventureStatusY = 394 + offB;", "native small/large vertical placement")
    require(constructor, "FONT_SMALL, ETextAlignment::CENTER, Colors::YELLOW", "centered native-scale label")
    require(constructor, "Rect(adventureStatusX - 120, adventureStatusY - 7, 240, 14)",
            "non-overlapping daily status help target")
    require(constructor, "adventureSpellDailyStatusHelp->removeUsedEvents(LCLICK);",
            "read-only help target does not consume left-clicks")

    refresh = method(UI, "void CSpellWindow::updateAdventureSpellDailyStatus()", "void CSpellWindow::turnPageLeft()")
    for gate in (
        "!battleSpellsOnly",
        "!onSpellSelect",
        "newHorizonsMagic::adventureSpellRulesActive(magicRules)",
        "myHero->hasNewHorizonsAdventureSpellCastToday()",
        '"new-horizons.adventure.spellbook.dailyAvailable"',
        '"new-horizons.adventure.spellbook.dailyUsed"',
        '"new-horizons.adventure.spellbook.dailyHelp"',
        "adventureSpellDailyStatusLabel->setText(statusText)",
        "adventureSpellDailyStatusHelp->hoverText = statusText",
    ):
        require(refresh, gate, "daily status gating/localized refresh")
    for key in (
        '"new-horizons.adventure.spellbook.dailyAvailable"',
        '"new-horizons.adventure.spellbook.dailyUsed"',
        '"new-horizons.adventure.spellbook.dailyHelp"',
    ):
        require(MODULE, key, "registered localized daily status text")

    used_today = method(UI, "bool CSpellWindow::isAdventureSpellUsedToday(SpellID spell) const", "void CSpellWindow::updateAdventureSpellDailyStatus()")
    for gate in (
        "!battleSpellsOnly && !onSpellSelect",
        "newHorizonsMagic::adventureSpellRulesActive(magicRules)",
        "newHorizonsMagic::isAdventureSpell(magicRules, spell)",
        "myHero->hasNewHorizonsAdventureSpellCastToday()",
    ):
        require(used_today, gate, "used-today slot predicate")

    compute = method(UI, "void CSpellWindow::computeSpellsPerArea()", "void CSpellWindow::setSchoolImages(")
    require(compute, "updateAdventureSpellDailyStatus();", "refresh on book-mode/page recomputation")
    require(compute, "redraw();", "spellbook redraw after status refresh")
    if compute.index("updateAdventureSpellDailyStatus();") > compute.index("redraw();"):
        raise AssertionError("daily status must be refreshed before the book redraw")

    page = method(UI, "void CSpellWindow::setCurrentPage(int value)", "void CSpellWindow::turnPageLeft()")
    require(page, "updateAdventureSpellDailyStatus();", "refresh on page selection")

    spell_area = method(UI, "void CSpellWindow::SpellArea::setSpell(const CSpell * spell)",
                        "void CSpellWindow::SpellArea::showPopupWindow")
    require(spell_area, "owner->isAdventureSpellUsedToday(mySpell->id)",
            "used Adventure Spell slot uses unavailable colors")
    require(spell_area, "firstLineColor = Colors::WHITE;", "unavailable name color")
    require(spell_area, "secondLineColor = Colors::ORANGE;", "unavailable cost color")
    require(spell_area, "name->setText(mySpell->getNameTranslated());", "spell name remains visible")
    require(spell_area, "costText.replaceNumber(spellCost);", "listed Mana cost remains visible")

    click = method(UI, "void CSpellWindow::SpellArea::clickPressed", "void CSpellWindow::SpellArea::showPopupWindow")
    if "isAdventureSpellUsedToday" in click:
        raise AssertionError("daily UI state must not change cast/action semantics")

    print("PASS: daily status is localized, gated, refreshable and non-interactive; used Adventure Spell slots are visibly unavailable")
    print("Source wiring only; native-resolution rendering remains unverified")


if __name__ == "__main__":
    main()
