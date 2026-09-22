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
require(CASTLE, "displayLevels", "fort screen keeps model dwelling levels independent from visual order")
require(CASTLE, "std::stable_sort", "fort screen groups active New Horizons ranks")
require(CASTLE, "hasCompleteCategoryContext", "legacy/custom towns retain stock fort order")
require(KINGDOM, "getCreatureCategory", "kingdom town overview reads saved category")
require(KINGDOM, "creatureCategoryBadge", "kingdom town overview marks rank groups")
require(KINGDOM, "townCreatureAtLevel", "kingdom rank badges also cover unbuilt template dwellings")
require(KINGDOM_HEADER, "availableCategory", "kingdom town overview owns availability rank markers")
require(KINGDOM_HEADER, "growthCategory", "kingdom town overview owns growth rank markers")
require(WIKI, "categoryName.empty()", "town wiki retains legacy tier fallback")
require(WIKI, '"T" + std::to_string(row.creature->getLevel())', "legacy town wiki tier remains available")

print("New Horizons recruitment category UI: PASS")
