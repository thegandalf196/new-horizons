#!/usr/bin/env python3
"""Guard shared current Estates quotes; native tests establish rounding/payment."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ordinary = (ROOT / "client/windows/GUIClasses.cpp").read_text()
quick = (ROOT / "client/windows/QuickRecruitmentWindow.cpp").read_text()
cards = (ROOT / "client/windows/CreaturePurchaseCard.cpp").read_text()
town = (ROOT / "client/windows/CCastleInterface.cpp").read_text()
bar = (ROOT / "client/adventureMap/CResDataBar.cpp").read_text()
market = (ROOT / "client/widgets/markets/CMarketResources.cpp").read_text()
assert "maxRecruitableAmount(" not in ordinary
assert "dwelling->getRecruitmentAmount(card->creature->getId()" in ordinary
assert "newHorizonsEstates::recruitmentBuyer(dst)" in ordinary
assert "canAfford(recruitmentCost * to)" not in ordinary
assert "totalCostValue->set(recruitmentCost);" in ordinary
assert "newHorizonsEstates::recruitmentBuyer(town->getUpperArmy())" in quick
assert "allAvailableResources + ownQuote" in quick
assert "static_cast<int64_t>(currentAmount) + maxAmount" not in quick
for source in (quick, cards):
    assert "getFullRecruitCost()" not in source
assert "parent->recruitmentCost(creatureOnTheCard->getId(), amount)" in cards
assert "town->getBuildingCost(building->bid)" in town
build = town.split("CBuildWindow::CBuildWindow(", 1)[1].split("std::string CBuildWindow::", 1)[0]
assert "building->resources[" not in build
assert "town->getRecruitmentAmount(creatureId" in town
for source in (ordinary, quick, cards):
    assert "conscription" not in source, "client must not implement the discount"
for retired in ("economy.investor.", "estates.investor", "economy.resourceBroker.", "economy.magnate."):
    assert retired not in town + bar + market
assert "new-horizons.economy.stewardship.help" in town
assert "new-horizons.economy.steward.help" not in town
print("PASS: shared buyer-specific aggregate recruitment and resident building quotes; retired economy UI removed")
print("Source routing only; native payment and rendered presentation remain unverified")
