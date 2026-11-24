#!/usr/bin/env python3
"""Source/layout guard for the read-only New Horizons perk browser."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
HERO = (ROOT / "client/windows/CHeroWindow.cpp").read_text(encoding="utf-8")
HERO += (ROOT / "client/windows/CHeroWindowLayout.cpp").read_text(encoding="utf-8")
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
    require(HERO, "newHorizonsPerkAvailability::evaluateTier(", "authored-tier status readback")
    require(HERO, "tierSlotText(slot, newHorizonsPerkHelp::skillName(curHero, skillId))", "empty-tier lock explanation")
    require(HERO, "area->enable();", "empty tier remains inspectable")
    require(BROWSER_HEADER, "Read-only catalogue", "read-only browser contract")
    require(BROWSER, 'std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h))', "leather dialog texture cut to the content")
    require(BROWSER, "pos = Rect(0, 0, WINDOW_WIDTH, columnBottom + FOOTER_HEIGHT);", "window height follows the tallest column")
    require(BROWSER, "perkState.hasSelection(skillId, perk.id)", "learned state comes from saved selections")
    require(BROWSER, 'perk.effect["status"].String()', "effect implementation status from the saved registry")
    require(BROWSER, 'return "Not implemented";', "planned effect disclosure")
    require(BROWSER, 'Implementation: " + effectStatus', "implementation status in right-click help")
    require(BROWSER, "std::array<std::vector<PerkRow>, 3> perksByRank", "three rank groups")
    for rank in ("Basic", "Advanced", "Expert"):
        require(BROWSER, f'"{rank}"', f"{rank} group label")
    require(BROWSER, 'AnimationPath::builtin(newHorizonsHybridCapturedPerkIcon(perk.id, perk.name, perkState.getRules()))', "existing perk animation lookup")
    require(BROWSER, "newHorizonsPerkHelp::skillDescription(&hero, *parentSkill, rank) : std::string()", "Skill's own rank effect above each column")
    require(BROWSER, "FONT_SMALL, ETextAlignment::TOPCENTER, Colors::WHITE, rankDescription(hero, skillId, rank))", "one rank effect per column")
    require(BROWSER, "CMessage::breakText(rankDescription(hero, skillId, rank), COLUMN_WIDTH - 16, FONT_SMALL).size()", "perk grid starts below the tallest rank text")
    require(BROWSER, "learned ? Colors::YELLOW : Colors::WHITE, perk.name", "learned perk name highlighted, others plain")
    require(BROWSER, "if(learned)\n\t\telements.push_back(std::make_shared<TransparentFilledRectangle>(row", "gold frame only around learned perks")
    # The window shows no per-perk state captions or card boxes any more; the
    # eligibility explanation stays in the click/right-click help.
    for removed in ("Future level-up offer", "Not learned", "Requires this Skill", "statusName(", "reasonCaption(",
                    "Read-only catalogue. Learned perks", "cardFill", "unlearnedBorder"):
        if removed in BROWSER:
            raise AssertionError(f"removed perk browser status/card element returned: {removed}")
    require(BROWSER, "newHorizonsPerkAvailability::evaluatePerk(", "saved-rule eligibility classification")
    require(BROWSER, "newHorizonsPerkAvailability::explanation(evaluation, newHorizonsPerkHelp::skillName(&hero, skillId))", "complete lock explanation in help")
    require(BROWSER, 'newHorizonsPerkHelp::format(&hero, skillId, perk.name', "shared perk explanation")
    require(BROWSER, "addUsedEvents(LCLICK | SHOW_POPUP)", "persistent and right-click help hit area")
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

    # The 640-wide window's height follows its content: title, the tallest
    # rank text (clipped at its full height), the tallest perk column and the
    # footer with the original 64x30 checkmark 8 px inside the corner.
    window_width, column_left, column_stride, column_width = 640, 12, 208, 200
    panel_top, description_height, footer_height = 42, 160, 46
    row_height, icon = 50, 44
    for name, value in (("WINDOW_WIDTH", window_width), ("COLUMN_LEFT", column_left), ("COLUMN_STRIDE", column_stride),
                        ("COLUMN_WIDTH", column_width), ("COLUMN_TOP", panel_top), ("DESCRIPTION_HEIGHT", description_height),
                        ("FOOTER_HEIGHT", footer_height), ("PERK_ROW_HEIGHT", row_height), ("PERK_ICON", icon)):
        require(BROWSER, f"constexpr int {name} = {value};", f"{name} geometry")
    require(BROWSER, "const int columnBottom = perksTop + static_cast<int>(rows) * PERK_ROW_HEIGHT + 4;", "columns end below the last perk row")
    require(BROWSER, "Point(pos.w - 72, pos.h - 38)", "checkmark anchored to the fitted window corner")
    assert column_left + 2 * column_stride + column_width <= window_width
    assert icon <= row_height - 4
    assert 8 + 30 + 8 == footer_height and (window_width - 72) + 64 <= window_width
    # Longest rank text and four perk rows still fit a 600 px logical screen.
    tallest = panel_top + 26 + description_height + 14 + 4 * row_height + 4 + footer_height
    assert tallest <= 600, tallest

    # Browser code only reads saved selections and definitions; no choice
    # submitter or learning/acquisition API may appear in this new window.
    for forbidden in ("setSecSkillLevel", "selectPerk", "learnPerk", "requestNewSecondarySkill"):
        if forbidden in BROWSER:
            raise AssertionError(f"read-only browser must not acquire skills/perks: {forbidden}")

    print("New Horizons read-only perk browser binding and layout source guard PASS")


if __name__ == "__main__":
    main()
