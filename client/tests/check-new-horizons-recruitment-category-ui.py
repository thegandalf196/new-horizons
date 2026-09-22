#!/usr/bin/env python3
"""Static guard for New Horizons Core/Elite/Champion recruitment presentation.

The category view is optional and comes from the saved game callback.  This
keeps the ordinary seven-tier UI byte-for-byte in legacy contexts while making
the active New Horizons classification visible in town and recruitment views.
"""

from pathlib import Path


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
require(QUICK, "categoryGroupRects", "quick recruitment records visible group bounds")
require(QUICK, "categoryHeaders[index]", "quick recruitment renders category headers")
require(QUICK, "selected->recruitmentLevel", "quick recruitment preserves the original dwelling row after visual grouping")
require(QUICK_CARD_HEADER, "const int recruitmentLevel", "quick cards own their authoritative dwelling row")
require(QUICK, "uncategorizedLevels.empty()", "partial custom category contexts retain legacy order")
require(QUICK_HEADER, "void showAll(Canvas & to) override", "quick recruitment draws group boundaries")
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
require(CASTLE, "rankedFortMinimumCardHeight", "ranked card minimum derives from actual tiny-font metrics")
require(CASTLE, "rankedFortStatBottomPadding", "ranked stat rows reserve bottom padding")
require(CASTLE, "rankedFortStatRowHeight", "ranked stat rows derive from the card height")
require(CASTLE, "rankedFortStatRowCount(compactStatGrid) * rankedFortStatRowHeight(cardHeight, compactStatGrid)", "ranked code asserts all stat rows fit")
require(CASTLE, "compactStatGrid", "multi-row authored rosters use compact stat geometry")
require(CASTLE, "NH_FORT_COMPACT_STAT_COLUMNS", "compact cards use a two-column stat grid")
require(CASTLE, "rankedStatRect", "compact cards place all eight stat rows in the grid")
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
