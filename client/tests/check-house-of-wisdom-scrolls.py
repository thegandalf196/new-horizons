#!/usr/bin/env python3
"""Source guard for House of Wisdom native-parchment scroll presentation."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GUI_HEADER = (ROOT / "client/windows/GUIClasses.h").read_text(encoding="utf-8")
GUI = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
TOWN = (ROOT / "lib/mapObjects/CGTownInstance.cpp").read_text(encoding="utf-8")


def require(source: str, value: str, label: str) -> None:
    if value not in source:
        raise AssertionError(f"missing {label}: {value}")


def check() -> None:
    require(GUI_HEADER, "class CSpellScrollPresentation final : public CIntObject",
            "shared spell-scroll presentation")
    require(GUI, 'AnimationPath::builtin("TPMAGES.DEF"), 0', "native blank parchment frame 0")
    require(GUI, "constexpr int nativeScrollWidth = 83", "native scroll width")
    require(GUI, "constexpr int nativeScrollHeight = 61", "native scroll height")
    require(GUI, "spellFrame->pos.w == nativeScrollWidth", "complete native scroll preservation")
    require(GUI, "emblemMaximumWidth = 54", "safe emblem width")
    require(GUI, "emblemMaximumHeight = 45", "safe emblem height")
    for event in ("clickPressed(cursorPosition)", "showPopupWindow(cursorPosition)", "hover(on)"):
        require(GUI, f"interactionTarget->{event}", f"full-scroll interaction forwarding: {event}")

    house_item = GUI.split("void CUniversityWindow::CItem::update()", 1)[1].split(
        "CUniversityWindow::CUniversityWindow", 1
    )[0]
    require(house_item, "const int bottomBarHeight = bottomBar->pos.h", "loaded strip height")
    require(house_item, "topBar->pos.h / 2", "vertically centered title")
    require(house_item, "bottomBar->pos.h / 2", "vertically centered price")
    require(house_item, "constexpr int spellScrollHeight = 61", "full-size parchment layout")
    require(house_item, "Point(-19, parchmentY)", "83px parchment centered in native slot")
    require(house_item, "scrollPresentation->setInteractionTarget(scroll.get())",
            "scroll click/right-click/hover uses the existing spell component")
    require(house_item, "scrollPresentation->setInputEnabled(available)", "only offered scrolls are interactive")
    require(house_item, "scroll->image->visible = false", "no separate 44px icon over the parchment")

    eligibility = TOWN.split("bool isHouseOfWisdomCandidate(", 1)[1].split("\n}", 1)[0]
    checks = (
        "spellAllowedBySavedRoster(magicRules, spellId)",
        "spellAvailableForOrdinaryAcquisition(magicRules, spellId)",
        "callback.isAllowed(spellId)",
        "const auto * spell = spellId.toSpell()",
    )
    for check_text in checks:
        require(eligibility, check_text, "saved-roster, acquisition, map, and safe-ID offer filter")
    assert [eligibility.index(value) for value in checks] == sorted(eligibility.index(value) for value in checks)
    visible_stock = TOWN.split("std::vector<TradeItemBuy> CGTownInstance::availableItemsIds", 1)[1].split(
        "void CGTownInstance::", 1
    )[0]
    require(visible_stock, "for(const auto spell : getHouseOfWisdomScrolls())", "persisted stock filtering")
    require(visible_stock, "isHouseOfWisdomCandidate(cb->getMagicRules(), spell, *cb)",
            "stale stock is hidden without rerolling")

    print("PASS: full native parchment, shared interaction forwarding, loaded-strip layout, and stale-stock filtering")
    print("Source guard only; rendered layout and runtime purchase remain unverified")


if __name__ == "__main__":
    check()
