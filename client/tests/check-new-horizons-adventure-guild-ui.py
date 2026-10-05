#!/usr/bin/env python3
"""Source guard for the authoritative Mage Guild Adventure Spell purchase UI."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "client/windows/CCastleInterface.h").read_text(encoding="utf-8")
UI = (ROOT / "client/windows/CCastleInterface.cpp").read_text(encoding="utf-8")
VISITORS = (ROOT / "client/ClientNetPackVisitors.h").read_text(encoding="utf-8")
PACKS = (ROOT / "client/NetPacksClient.cpp").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def main() -> None:
    require(HEADER, "class CMageGuildAdventureSpellWindow : public CWindowObject", "five-tier purchase panel")
    require(HEADER, "void updateSpells(ObjectInstanceID townId);", "authoritative UI refresh entry point")
    require(UI, "class MageGuildExteriorHotspot final : public CPicture", "picture-sized exterior hotspot")
    require(UI, ": CPicture(image, position.x, position.y)", "hitbox uses the loaded image's native bounds")
    require(UI, "addUsedEvents(HOVER)", "native picture hover interaction")
    require(UI, "setRedrawParent(true)", "hover outline redraws the Mage Guild surface")
    require(UI, "Rect::createAround(pos, 1), Colors::METALLIC_GOLD", "gold outline follows the loaded picture bounds")
    require(UI, "addLClickCallback([townId]()", "exterior picture click callback")
    require(UI, "createAndPushWindow<CMageGuildAdventureSpellWindow>(townId)", "hotspot opens the existing purchase panel")
    screen = UI.split("CMageGuildScreen::CMageGuildScreen", 1)[1].split(
        "void CMageGuildScreen::updateSpells", 1
    )[0]
    require(screen, "selectedGuildWindow, windowPosition, townId", "hotspot uses faction/tier picture and configured position")
    require(screen, "if(adventureSpellRulesActive)", "hotspot remains gated by saved Adventure Spell rules")
    require(screen, "else\n\t\twindow = std::make_shared<CPicture>(selectedGuildWindow, windowPosition.x, windowPosition.y);",
            "vanilla Mage Guild keeps its non-interactive picture")
    for obsolete in ("NH_spells_button", "adventureSpellsButton", "adventureSpellsLabel"):
        if obsolete in HEADER or obsolete in screen:
            raise AssertionError(f"separate Adventure Spell access control must be removed: {obsolete}")
    require(UI, "newHorizonsMagic::adventureSpellRulesActive", "saved-rules gate")
    require(UI, "guildLevel <= 5", "five fixed Guild tiers")
    require(UI, "newHorizonsMagic::adventureSpellForGuildLevel(magicRules, guildLevel)", "fixed spell from saved rules")
    require(UI, "newHorizonsMagic::adventureSpellUnlockCost(magicRules, spellId)", "exact saved unlock cost")
    require(UI, "town->hasNewHorizonsAdventureSpellUnlocked(guildLevel)", "locked/unlocked status from town state")
    require(UI, "resources.canAfford(cost)", "resource affordability check")
    require(UI, "unlockNewHorizonsAdventureSpell(currentTown, guildLevel)", "validated server callback")
    require(UI, 'AnimationPath::builtin("IBUY30.DEF")', "purchase control")
    require(UI, "visiting heroes learn this spell", "per-town learning status")
    require(UI, "MageGuildAdventureSpellHelpArea", "spell and purchase right-click help")
    require(VISITORS, "visitSetNewHorizonsAdventureSpellUnlock(SetNewHorizonsAdventureSpellUnlock & pack) override;",
            "client result-pack visitor declaration")
    body = PACKS.split(
        "void ApplyClientNetPackVisitor::visitSetNewHorizonsAdventureSpellUnlock", 1
    )[1].split("void ApplyClientNetPackVisitor::visitSetMovePoints", 1)[0]
    require(body, "findWindows<CMageGuildScreen>()", "refresh of an open Guild screen")
    require(body, "findWindows<CMageGuildAdventureSpellWindow>()", "refresh of an open purchase panel")
    require(body, "updateSpells(pack.townId)", "town-scoped refresh")

    panel = UI.split("CMageGuildAdventureSpellWindow::CMageGuildAdventureSpellWindow", 1)[1].split(
        "CMageGuildScreen::ScrollAllSpells", 1
    )[0]
    for forbidden in (
        "setNewHorizonsAdventureSpellUnlocked",
        "adventureSpellUnlockCost(magicRules, spellId) =",
        "town->newHorizonsAdventureSpell",
    ):
        if forbidden in panel:
            raise AssertionError(f"client UI must not mutate authoritative town unlock state: {forbidden}")
    for prototype_cost in ("2500", "5000", "10000", "15000", "25000"):
        if prototype_cost in panel:
            raise AssertionError(f"client UI must read the saved canonical price instead of hard-coding {prototype_cost}")

    row_left, row_top, row_width, row_height, row_gap = 18, 82, 604, 68, 5
    close_left, close_top, footer_top = 555, 462, 471
    for level in range(5):
        top = row_top + level * (row_height + row_gap)
        assert row_left >= 0 and row_left + row_width <= 640
        assert top + row_height <= close_top
    assert close_left >= 0 and close_left + 64 <= 640
    assert close_top < footer_top < 500

    print("PASS: fixed Adventure Spell tiers, saved costs, purchase callback, state refresh, and layout bounds")
    print("Source wiring only; rendered appearance and runtime purchase remain unverified")


if __name__ == "__main__":
    main()
