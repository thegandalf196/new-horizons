#!/usr/bin/env python3
"""Static guard for New Horizons Core/Elite/Champion recruitment presentation.

The category view is optional and comes from the saved game callback.  This
keeps the ordinary seven-tier UI byte-for-byte in legacy contexts while making
the active New Horizons classification visible in town and recruitment views.
"""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[2]
GUI = (ROOT / "client/windows/GUIClasses.cpp").read_text(encoding="utf-8")
GUI_HEADER = (ROOT / "client/windows/GUIClasses.h").read_text(encoding="utf-8")
QUICK_CARD = (ROOT / "client/windows/CreaturePurchaseCard.cpp").read_text(encoding="utf-8")
QUICK_CARD_HEADER = (ROOT / "client/windows/CreaturePurchaseCard.h").read_text(encoding="utf-8")
CASTLE = (ROOT / "client/windows/CCastleInterface.cpp").read_text(encoding="utf-8")
CASTLE_HEADER = (ROOT / "client/windows/CCastleInterface.h").read_text(encoding="utf-8")
KINGDOM = (ROOT / "client/windows/CKingdomInterface.cpp").read_text(encoding="utf-8")
KINGDOM_HEADER = (ROOT / "client/windows/CKingdomInterface.h").read_text(encoding="utf-8")
WIKI = (ROOT / "client/windows/wiki/WikiTownContent.cpp").read_text(encoding="utf-8")
HELPER = (ROOT / "client/windows/NewHorizonsCreatureCategoryUI.h").read_text(encoding="utf-8")
TOWER_RANKS = (ROOT / "Mods/new-horizons/Content/config/factions/towerCreatureRanks.json").read_text(encoding="utf-8")
FORT_TEXTS = (ROOT / "config/newHorizonsFortTexts.json").read_text(encoding="utf-8")


def require(source: str, fragment: str, message: str) -> None:
    if fragment not in source:
        raise AssertionError(message)


require(HELPER, "CreatureCategoryView", "category view helper")
require(HELPER, "categoryName.empty()", "optional category keeps legacy text")
require(GUI, "getCreatureCategory", "standard recruitment reads saved category")
require(GUI, "categoryLabel", "standard recruit cards show category")
require(GUI_HEADER, "std::shared_ptr<CLabel> categoryLabel", "standard card owns category label")
require(GUI, "newHorizonsCreatureCategoryUI::prefix", "standard recruit title is category-aware")
require(GUI_HEADER, "std::array<std::shared_ptr<CLabel>, 3> categoryHeaders", "standard recruitment has category group headers")
require(GUI, "categoryCards", "standard recruitment groups cards by category")
require(GUI, "categoryHeaders[index]", "standard recruitment renders category headers")
require(QUICK_CARD, "getCreatureCategory", "quick recruitment reads saved category")
require(QUICK_CARD, "categoryLabel", "quick recruitment card shows category")
require(QUICK_CARD_HEADER, "std::shared_ptr<CLabel> categoryLabel", "quick card owns category label")
QUICK = (ROOT / "client/windows/QuickRecruitmentWindow.cpp").read_text(encoding="utf-8")
QUICK_HEADER = (ROOT / "client/windows/QuickRecruitmentWindow.h").read_text(encoding="utf-8")
require(QUICK, "categoryLevels", "quick recruitment groups dwelling rows by category before constructing cards")
require(QUICK, "categoryHeaders[index]", "quick recruitment renders category headers")
require(QUICK, "selected->recruitmentLevel", "quick recruitment preserves the original dwelling row after visual grouping")
require(QUICK_CARD_HEADER, "const int recruitmentLevel", "quick cards own their authoritative dwelling row")
require(QUICK, "uncategorizedLevels.empty()", "partial custom category contexts retain legacy order")
if "categoryGroupRects" in QUICK or "categoryGroupRects" in QUICK_HEADER:
    raise AssertionError("quick recruitment must not retain pasted gold category boxes")
if "compactBackground" in QUICK_CARD or "compactBackground" in QUICK_CARD_HEADER:
    raise AssertionError("compact cards must share the continuous parent leather surface")
require(QUICK, "updateCompactTotalCost(purchaseCost)", "categorized footer uses the unchanged total-cost arithmetic")
require(QUICK, "totalCost->createItems(purchaseCost)", "legacy total cost behavior remains available")
require(QUICK_CARD, 'AnimationPath::builtin("TWCRPORT")', "quick portrait uses native58x64 large frame")
require(QUICK_CARD, "Rect(4, 16, 58, 64)", "native portrait geometry remains unscaled")
require(QUICK_CARD, "Point(4, 16), compactPortrait", "upgrade popup geometry matches the portrait")
compact_view = QUICK_CARD.split("void CreaturePurchaseCard::initCompactView()", 1)[1].split("void CreaturePurchaseCard::updateCompactStats()", 1)[0]
require(compact_view, "setRedrawParent(true)", "shared-surface compact cards repaint their opaque owner before changing text")
compact_footer = QUICK.split("void QuickRecruitmentWindow::updateCompactTotalCost", 1)[1].split("QuickRecruitmentWindow::QuickRecruitmentWindow", 1)[0]
require(compact_footer, "exactCost", "compact total row keeps full resource values in popup/hover help")
assert re.search(r"if\(resourceIds.empty\(\)\)\s*\{\s*redraw\(\);[^}]*return;", compact_footer), "empty cost selection must repaint removed widgets"
assert re.search(r"\}\s*redraw\(\);\s*\}", compact_footer), "rebuilt compact total row must repaint the window"
require(QUICK, "hasCompleteCategoryContext", "compact quick layout requires complete saved category rules")
require(QUICK, "displayVariantsAtLevel", "unbuilt roster previews use template creature identities")
require(QUICK, "variants.resize(1)", "unbuilt dwellings do not offer unavailable upgrades")
require(QUICK, "const int stock = built ? town->creatures[level].first : 0", "unbuilt previews cannot fabricate recruitment stock")
require(QUICK, "getGrowthInfo(recruitmentLevel).totalGrowth()", "weekly growth is not current stock")
require(QUICK_CARD, "updateCompactStats()", "switching an upgrade refreshes statistics")
require(QUICK_CARD, "initCompactCostInfo()", "switching rebuilds resource cost composition")
require(QUICK_CARD_HEADER, "compactStatIcons", "stat icons have owning references")
for icon in ("iconAttack", "iconDefense", "iconDamage", "iconHealth", "iconSpeed", "iconInitiative", "iconGrowth"):
    require(QUICK_CARD, f'"stackWindow/{icon}"', f"quick cards reuse creature stat {icon}")
require(QUICK_CARD, '"NH_creature_leadership_20"', "quick cards reuse Leadership crown")
require(QUICK_CARD, "getBaseInitiative()", "quick cards show authored Initiative")
require(QUICK_CARD, "capabilityCreatureLeadershipRequirement", "quick cards show authoritative Leadership cost")

# Check the production band constants, not a manually invented preview size.
# This establishes nominal bounds only, not font/asset pixel or rendered acceptance.
def quick_constant(name: str) -> int:
    match = re.search(rf"constexpr int {name} = (\d+);", QUICK)
    if not match:
        raise AssertionError(f"missing quick-layout constant {name}")
    return int(match.group(1))

card_width = quick_constant("NH_QUICK_CARD_MAX_WIDTH")
card_height = quick_constant("NH_QUICK_CARD_HEIGHT")
gap = quick_constant("NH_QUICK_CARD_GAP")
margin = quick_constant("NH_QUICK_SIDE_MARGIN")
heading_gap = quick_constant("NH_QUICK_HEADING_GAP")
top = quick_constant("NH_QUICK_CONTENT_TOP")
footer = quick_constant("NH_QUICK_FOOTER_HEIGHT")
heading = quick_constant("NH_QUICK_HEADER_HEIGHT")
border_width = quick_constant("NH_QUICK_BORDER_WIDTH")
border_height = quick_constant("NH_QUICK_BORDER_HEIGHT")
minimum_width = quick_constant("NH_QUICK_MIN_WIDTH")
def card_constant(name: str) -> int:
    match = re.search(rf"constexpr int {name} = (\d+);", QUICK_CARD)
    assert match is not None, f"missing card-layout constant {name}"
    return int(match.group(1))

stat_top = card_constant("statTop")
stat_height = card_constant("statRowHeight")
stat_left = card_constant("statLeft")
slider_position = re.search(r"slider = std::make_shared<CSlider>\(Point\(18, (\d+)\), cardWidth - 36", QUICK_CARD)
assert slider_position is not None
slider_top = int(slider_position.group(1))
require(QUICK_CARD, "Point(4, 88)", "upgrade switch occupies its independent left column")
require(QUICK_CARD, "CLabel>(24, 81", "remaining stock is beside the upgrade control")
require(QUICK_CARD, "CLabel>(62, 104", "selected count is below remaining stock")
require(QUICK_CARD, "availableAmount->setText(std::to_string(maxAmount - value))", "remaining count never truncates an Available prefix")
for roster in ((1, 0, 0), (1, 1, 1), (3, 3, 1), (4, 2, 1), (1, 5, 1), (2, 5, 1)):
    columns = max(roster)
    window_width = min(800 - border_width, max(minimum_width, columns * card_width + (columns - 1) * gap + margin * 2))
    actual_width = min(card_width, (window_width - margin * 2 - gap * (columns - 1)) // columns)
    window_height = top + 3 * (heading + heading_gap + card_height + gap) + footer
    assert columns * actual_width + (columns - 1) * gap <= window_width - margin * 2
    assert window_width + border_width <= 800
    assert window_height + border_height <= 600
    assert actual_width >= 146
    # Native portrait and left count/switch region stay separate from all8
    # statistic rows. Slider has native16px controls; costs have up to2 rows.
    assert 4 + 58 < stat_left
    assert 16 + 64 <= 81
    assert 4 + 16 <= 24  # switch right edge before both count labels
    assert 81 + 11 <= 104 - 11
    assert 88 + 16 <= slider_top
    assert stat_top + 8 * stat_height <= slider_top
    assert stat_left + 10 + 3 < actual_width - 6
    assert slider_top + 16 <= card_height - 2 * 10
    for resource_count in (7, 8):
        cost_columns = min(4, resource_count)
        cost_rows = (resource_count + cost_columns - 1) // cost_columns
        cost_top = card_height - cost_rows * 10
        assert cost_top >= slider_top + 16
        assert cost_top + cost_rows * 10 <= card_height
        assert (actual_width - 8) // cost_columns > 9 + 1
    # Footer resource row precedes the unchanged native32px action buttons.
    assert window_height - footer + 5 + 16 <= window_height - 39
    assert window_height - 39 + 32 <= window_height
    assert window_width // 2 + 124 + 64 <= window_width  # optional native Muster button
    footer_cell_width = min(70, (window_width - 2 * margin) // 8)
    assert footer_cell_width > 22  # resource icon plus a positive value-label span
require(QUICK, "viewport.x < 800 || viewport.y < 600", "small viewports retain the legacy layout instead of overlapping")
require(CASTLE, "getCreatureCategory", "town dwelling presentation reads saved category")
require(CASTLE, "newHorizonsCreatureCategoryUI::prefix", "town dwelling text is category-aware")
require(CASTLE, "categoryLabel", "fort recruitment area shows category")
require(CASTLE_HEADER, "std::shared_ptr<CLabel> categoryLabel", "fort recruitment area owns category label")
require(CASTLE, "hasCompleteCreatureCategoryContext", "legacy/custom towns retain stock fort order")
require(CASTLE, "if(count == 0)", "any nonempty complete categorized roster uses ranked layout")
if "count != GameConstants::CREATURES_PER_TOWN" in CASTLE:
    raise AssertionError("seven-row towns must not be excluded from ranked fort layout")
require(CASTLE, "categoryLevels", "fort screen groups active New Horizons ranks")
require(CASTLE, "categoryHeaders", "fort screen renders rank headings")
require(CASTLE, "ETextAlignment::CENTER, Colors::YELLOW, heading", "all town rank headings use yellow")
require(CASTLE, "Colors::YELLOW, categoryName, 152", "fallback fort rank labels also use yellow")
if "creatureCategoryColor" in CASTLE:
    raise AssertionError("fort ranks must not restore distinct category colors")
require(CASTLE, "categoryViews", "fort headings retain the saved category text IDs")
require(CASTLE, "newHorizonsCreatureCategoryUI::name(categoryViews[rank]", "fort headings use the active category translator")
for stock_key in ("core.castinfo.0", "core.castinfo.1", "core.castinfo.2", "core.castinfo.3", "core.castinfo.4", "core.castinfo.5"):
    require(CASTLE, f'"{stock_key}"', f"fort cards reuse the stock translation for {stock_key}")
for nh_key in (
    "new-horizons.fort.stat.initiative",
    "new-horizons.fort.stat.initiative.description",
    "new-horizons.fort.stat.leadershipCost",
    "new-horizons.fort.stat.leadershipCost.description",
):
    require(CASTLE, f'"{nh_key}"', f"fort cards translate New Horizons-only stat {nh_key}")
    require(FORT_TEXTS, f'"{nh_key}"', f"canonical translations define {nh_key}")
if 'const std::array<const char *, 3> headings' in CASTLE:
    raise AssertionError("ranked fort headings must not hard-code English labels")
if 'std::make_shared<LabeledValue>(sizes, "Attack", ""' in CASTLE:
    raise AssertionError("ranked fort stat names/descriptions must be translated")
require(CASTLE, "createResponsiveFortBackground", "ranked fort background follows the viewport")
require(CASTLE, "createResponsiveFortCardBackground", "ranked cards have bounded backgrounds")
require(CASTLE, "CanvasScalingPolicy::AUTO", "ranked fort canvases follow UI scaling")
require(CASTLE, "CanvasClipRectGuard", "ranked fort clips its child drawing to the window")
require(CASTLE, "castleInt->pos.dimensions()", "ranked fort remains inside the parent town window")
require(CASTLE, "RecruitArea::showAll", "ranked cards clip oversized creature portraits")
require(CASTLE, "compactTitleCenterY", "ranked card titles are top-safe")
require(CASTLE, "minimumStatWidth", "ranked stat columns reserve Leadership Cost and values")
require(CASTLE, 'tinyFont->getStringWidth("99999")', "ranked values reserve five digits")
require(CASTLE, "rankedStatTextValueGap = 12", "ranked labels retain a separate value gutter")
require(CASTLE, "rankedStatRightInset = 12", "ranked values stay inset from the card border")
require(CASTLE, "rankedFortMinimumCardHeight", "ranked card minimum derives from actual tiny-font metrics")
require(CASTLE, "rankedFortStatBottomPadding", "ranked stat rows reserve bottom padding")
require(CASTLE, "rankedFortStatRowHeight", "ranked stat rows derive from the card height")
require(CASTLE, "rankedFortStatRowCount(compactStatGrid) * rankedFortStatRowHeight(cardHeight, compactStatGrid)", "ranked code asserts all stat rows fit")
require(CASTLE, "compactStatGrid", "multi-row authored rosters use compact stat geometry")
require(CASTLE, "NH_FORT_COMPACT_STAT_COLUMNS", "compact cards use a two-column stat grid")
require(CASTLE, "rankedStatRect", "compact cards place all eight stat rows in the grid")
require(CASTLE, "rankedStatIcons", "ranked cards own familiar creature-stat icons")
require(CASTLE, "std::min(rankedStatIconWidth, rect.h - 1)", "ranked stat icons fit the actual responsive row")
require(CASTLE, "icon->scaleTo(Point(iconSize, iconSize))", "bitmap stat icons scale with compact rows")
for icon in ("iconAttack", "iconDefense", "iconDamage", "iconHealth", "iconSpeed", "iconInitiative", "iconGrowth"):
    require(CASTLE, f'"stackWindow/{icon}"', f"ranked cards reuse creature UI {icon}")
require(CASTLE, '"NH_creature_leadership_20"', "ranked cards show the crown Leadership Cost icon")
require(CASTLE, "if(!rankedLayout)", "ranked cards omit obsolete dwelling art and names")
require(CASTLE, "RankedFortCreatureViewport", "compact cards clip the portrait to its utility band")
require(CASTLE, "cardsBottom", "ranked code computes the final band bottom")
require(CASTLE, "assert(cardsBottom <= footerTop)", "ranked code keeps every band above the footer")
require(CASTLE, "std::string availableText = rankedLayout", "ranked available counts stay compact after updates")
require(CASTLE_HEADER, "void showAll(Canvas & to) override", "fort screen owns its clipping boundary")
require(CASTLE, "levels[rowBegin + column]", "fort cards preserve their model dwelling level")
require(CASTLE, "getBaseInitiative()", "fort cards show creature Initiative")
require(CASTLE, "capabilityCreatureLeadershipRequirement", "fort cards show authoritative Leadership Cost")
if "Attack Skill" in CASTLE:
    raise AssertionError("ranked fort must not add the obsolete bottom Attack Skill label")
require(TOWER_RANKS, '"modify@4": [ "genie", "masterGenie" ]', "Tower Genies use dwelling row four")
require(TOWER_RANKS, '"modify@5": [ "mage", "archMage" ]', "Tower Magi use dwelling row five")
if '"modify@3"' in TOWER_RANKS:
    raise AssertionError("Tower creature override must not replace the Golem row")
require(KINGDOM, "getCreatureCategory", "kingdom town overview reads saved category")
require(KINGDOM, "creatureCategoryBadge", "kingdom town overview marks rank groups")
require(KINGDOM, "townCreatureAtLevel", "kingdom rank badges also cover unbuilt template dwellings")
require(KINGDOM_HEADER, "availableCategory", "kingdom town overview owns availability rank markers")
require(KINGDOM_HEADER, "growthCategory", "kingdom town overview owns growth rank markers")
require(WIKI, "categoryName.empty()", "town wiki retains legacy tier fallback")
require(WIKI, '"T" + std::to_string(row.creature->getLevel())', "legacy town wiki tier remains available")

print("New Horizons recruitment category UI: PASS")
