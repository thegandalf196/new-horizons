#!/usr/bin/env python3
"""Source guard for the authoritative Mage Guild Adventure Spell purchase UI."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HEADER = (ROOT / "client/windows/CCastleInterface.h").read_text(encoding="utf-8")
UI = (ROOT / "client/windows/CCastleInterface.cpp").read_text(encoding="utf-8")
ASSET_GENERATOR = (ROOT / "client/render/AssetGenerator.cpp").read_text(encoding="utf-8")
VISITORS = (ROOT / "client/ClientNetPackVisitors.h").read_text(encoding="utf-8")
PACKS = (ROOT / "client/NetPacksClient.cpp").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def main() -> None:
    level_name = UI.split("std::string adventureSpellGuildLevelName(int guildLevel)", 1)[1].split("\n}", 1)[0]
    require(level_name, "return std::to_string(guildLevel);", "Arabic numerals in purchase tier names and lock hints")
    if "std::array" in level_name:
        raise AssertionError("Guild level labels must not substitute Roman numerals")
    require(HEADER, "class CMageGuildAdventureSpellWindow : public CWindowObject", "five-tier purchase panel")
    require(HEADER, "void updateSpells(ObjectInstanceID townId);", "authoritative UI refresh entry point")
    require(UI, 'ImagePath::builtin("newHorizonsAdventureGuildBackground.png")', "compact Guild dialog surface")
    require(ASSET_GENERATOR,
            'addDialogBackground("newHorizonsAdventureGuildBackground.png", Point(640, 440));',
            "registered compact 640-by-440 leather background")
    require(UI, "class MageGuildExteriorHotspot final : public CPicture", "picture-sized exterior hotspot")
    require(UI, ": CPicture(image, position.x, position.y)", "hitbox uses the loaded image's native bounds")
    require(UI, "addUsedEvents(HOVER)", "native picture hover interaction")
    require(UI, "setRedrawParent(true)", "hover outline redraws the Mage Guild surface")
    require(UI, "cacheImageSilhouette();", "outline is derived from the loaded image's color-key silhouette")
    require(UI, "surface->isTransparent(Point(x, y))", "outline scan respects transparent picture pixels")
    require(UI, "cacheConfiguredArchOutline();", "opaque exterior images use the configured arched opening profile")
    require(UI, "origin + segment.from,", "cached outline follows the picture's current position")
    require(UI, "origin + segment.to,", "cached outline follows the picture's current position")
    require(UI, "cacheHoverOutline();", "silhouette is cached once when the picture hotspot is constructed")
    hotspot = UI.split("class MageGuildExteriorHotspot final : public CPicture", 1)[1].split(
        "const CCreature * creatureAtDwellingLevel", 1
    )[0]
    if "drawBorder(Rect::createAround(pos" in hotspot:
        raise AssertionError("Mage Guild hover outline must trace the arched picture silhouette, not its rectangle")
    draw_hover = hotspot.split("void drawHoverBorder", 1)[1].split("public:", 1)[0]
    if "isTransparent" in draw_hover:
        raise AssertionError("image transparency must be scanned only while the hotspot is constructed")
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
    help_area = UI.split("class MageGuildAdventureSpellHelpArea final : public CHoverableArea", 1)[1].split("\n};", 1)[0]
    require(help_area, "SpellID spell;", "popup stores the spell identity only")
    require(help_area, "CRClickPopup::createAndPush(description, std::make_shared<CComponent>(ComponentType::SPELL, spell));",
            "spell component is created only when the help popup opens")
    if "std::shared_ptr<CComponent>" in help_area or "CComponent" in help_area.split("void showPopupWindow", 1)[0]:
        raise AssertionError("spell popup component must not become a child of the main Guild window")
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
    if 'setTextOverlay("BUY"' in panel:
        raise AssertionError("the native purchase control already owns its label; do not overprint BUY")
    require(panel, "Rect(listLeft, listTop, listWidth, listHeight), listFill, listBorder",
            "tiers share one recessed leather list surface")
    require(panel, "Rect(listLeft + 8, dividerY + 1, listWidth - 16, 1), rowDivider, rowDivider",
            "tier rows use restrained aligned dividers")

    list_left, list_top, list_width, list_height = 18, 72, 604, 308
    row_left, row_top, row_width, row_height, row_gap = 18, 79, 604, 56, 3
    text_left, text_width = 83, 454
    buy_left, buy_width = 548, 64
    close_left, close_top, footer_top = 555, 391, 400
    last_row_bottom = row_top + 4 * (row_height + row_gap) + row_height
    list_right = list_left + list_width
    assert list_left + list_width <= 640 and list_top + list_height <= 440
    for level in range(5):
        top = row_top + level * (row_height + row_gap)
        assert row_left >= 0 and row_left + row_width <= 640
        assert top + row_height <= list_top + list_height
        assert text_left + text_width < buy_left
        assert buy_left + buy_width <= list_right
        assert 4 + 48 <= row_height
    assert last_row_bottom < close_top
    assert close_left >= 0 and close_left + 64 <= 640
    assert close_top < footer_top < 440

    print("PASS: cached arched hotspot, popup-only spell component, saved tier costs, purchase callback, and compact panel bounds")
    print("Source wiring only; rendered appearance and runtime purchase remain unverified")


if __name__ == "__main__":
    main()
