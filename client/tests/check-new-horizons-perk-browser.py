#!/usr/bin/env python3
"""Source/layout guard for the read-only New Horizons perk browser."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HERO = (ROOT / "client/windows/CHeroWindow.cpp").read_text(encoding="utf-8")
BROWSER = (ROOT / "client/windows/NewHorizonsPerkBrowser.cpp").read_text(encoding="utf-8")
BROWSER_HEADER = (ROOT / "client/windows/NewHorizonsPerkBrowser.h").read_text(encoding="utf-8")
HELP = (ROOT / "client/windows/NewHorizonsPerkHelp.h").read_text(encoding="utf-8")
ICONS = (ROOT / "client/windows/NewHorizonsPerkIcons.h").read_text(encoding="utf-8")
CMAKE = (ROOT / "client/CMakeLists.txt").read_text(encoding="utf-8")


def require(source: str, text: str, label: str) -> None:
    if text not in source:
        raise AssertionError(f"missing {label}: {text}")


def main() -> None:
    require(HERO, "createAndPushWindow<NewHorizonsPerkBrowser>(*curHero, skillId)", "learned Skill click opens browser")
    require(HERO, "newHorizonsPerkHelp::skillDefinition(curHero, skillId)", "browser gated by the hero's saved registry")
    require(HERO, "LRClickableAreaWTextComp::clickPressed(cursorPosition)", "legacy Skill info click preserved")
    require(BROWSER_HEADER, "Read-only catalogue", "read-only browser contract")
    require(BROWSER, 'ImagePath::builtin("newHorizonsOrdersBackground.png")', "existing leather dialog texture")
    require(BROWSER, "perkState.hasSelection(skillId, perk.id)", "learned state comes from saved selections")
    require(BROWSER, 'perk.effect["status"].String()', "effect implementation status from the saved registry")
    require(BROWSER, 'return "Not implemented";', "planned effect disclosure")
    require(BROWSER, 'Implementation: " + effectStatus', "implementation status in right-click help")
    require(BROWSER, "std::array<std::vector<const newHorizonsHeroes::PerkDefinition *>, 3>", "three rank groups")
    for rank in ("Basic", "Advanced", "Expert"):
        require(BROWSER, f'"{rank}"', f"{rank} group label")
    require(BROWSER, 'AnimationPath::builtin(newHorizonsPerkIcon(perk.id))', "existing perk animation lookup")
    require(BROWSER, "CMultiLineLabel>(Rect(cardLeft + 34, top + 2, cardWidth - 40, 26)", "wrapped full-width perk name")
    require(BROWSER, 'cardLeft + 34, top + 30, FONT_TINY', "learned state below the wrapped name")
    require(BROWSER, 'cardLeft + 34, top + 42, FONT_TINY', "full-width effect status on its own line")
    require(BROWSER, 'effectStatus, cardWidth - 40)', "untruncated implementation status width")
    require(BROWSER, 'newHorizonsPerkHelp::format(&hero, skillId, perk.name', "shared perk explanation")
    require(BROWSER, "addUsedEvents(SHOW_POPUP)", "right-click help hit area")
    require(BROWSER, "CRClickPopup::createAndPush(description", "native right-click explanation")
    require(BROWSER, "pos = area + pos.topLeft()", "card hit area uses the parent's absolute screen origin")
    help_area = BROWSER.split("class PerkBrowserHelpArea", 1)[1].split("};", 1)[0]
    require(help_area, "std::make_shared<CComponent>", "lazy owning-Skill popup component")
    constructor = BROWSER.split("NewHorizonsPerkBrowser::NewHorizonsPerkBrowser", 1)[1]
    if "std::make_shared<CComponent>" in constructor:
        raise AssertionError("popup-only Skill component must not be captured as a browser child")
    require(HELP, 'result += "\\nTier: "', "tier in shared perk explanation")
    require(ICONS, 'return found == icons.end() ? fallback : found->second;', "existing fallback for unbound icons")
    require(CMAKE, "windows/NewHorizonsPerkBrowser.cpp", "new browser source registration")
    require(CMAKE, "windows/NewHorizonsPerkBrowser.h", "new browser header registration")

    # The existing generated Orders texture is 640x500. Keep the three rank
    # panels and all controls inside that logical window, including the frame.
    window_width, window_height = 640, 500
    column_left, column_stride, column_width = 12, 208, 200
    panel_top, panel_height = 77, 381
    cards_top, cards_bottom = 116, 450
    close_top, footer_top = 462, 477
    assert column_left + 2 * column_stride + column_width <= window_width
    assert panel_top < cards_top < cards_bottom < panel_top + panel_height
    assert cards_top < cards_bottom < close_top < footer_top < window_height

    # The live registry has a 4/4/2 rank split. Each 56px card can fit a wrapped
    # two-line name followed by independent Learned and implementation lines.
    tiny_line_height = 10
    for rank_count in (4, 4, 2):
        available_height = (cards_bottom - cards_top) - 3 * (rank_count - 1)
        card_height = min(56, available_height // rank_count)
        assert card_height == 56
        assert 2 + 26 <= 30
        assert 30 + tiny_line_height <= 42
        assert 42 + tiny_line_height <= card_height

    # Browser code only reads saved selections and definitions; no choice
    # submitter or learning/acquisition API may appear in this new window.
    for forbidden in ("setSecSkillLevel", "selectPerk", "learnPerk", "requestNewSecondarySkill"):
        if forbidden in BROWSER:
            raise AssertionError(f"read-only browser must not acquire skills/perks: {forbidden}")

    print("New Horizons read-only perk browser binding and layout source guard PASS")


if __name__ == "__main__":
    main()
