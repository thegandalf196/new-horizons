#!/usr/bin/env python3
"""Guard shared current Estates quotes; native tests establish rounding/payment."""
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
ordinary = (ROOT / "client/windows/CRecruitmentWindow.cpp").read_text()
quick = (ROOT / "client/windows/QuickRecruitmentWindow.cpp").read_text()
quick += (ROOT / "client/windows/QuickRecruitmentPricing.cpp").read_text()
cards = (ROOT / "client/windows/CreaturePurchaseCard.cpp").read_text()
town = (ROOT / "client/windows/CCastleInterface.cpp").read_text()
town += (ROOT / "client/windows/TownEstateUI.cpp").read_text()
bar = (ROOT / "client/adventureMap/CResDataBar.cpp").read_text()
market = (ROOT / "client/widgets/markets/CMarketResources.cpp").read_text()
assert "maxRecruitableAmount(" not in ordinary
assert "dwelling->getRecruitmentAmount(card->creature->getId()" in ordinary
assert "newHorizonsEstates::recruitmentBuyer(dst)" in ordinary
assert "canAfford(recruitmentCost * to)" not in ordinary
assert "totalCostValue->set(recruitmentCost);" in ordinary
assert "newHorizonsEstates::recruitmentBuyer(town->getUpperArmy())" in quick
assert "quoteBasket(card.get())" in quick
assert "RecruitmentQuoteCursor cursor(buyer)" in quick
assert "resources - others.cost" in quick
assert "&others.ledger" in quick
assert "recruitmentBasket::fits" in quick
assert "static_cast<int64_t>(currentAmount) + maxAmount" not in quick
for source in (quick, cards):
    assert "getFullRecruitCost()" not in source
assert "parent->recruitmentCost(creatureOnTheCard->getId(), amount, this)" in cards
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
# Artifact-only purchase routes share the authoritative weekly Hoarder quote.
guild = town.split("void CCastleBuildings::enterMagesGuild()", 1)[1].split("void CCastleBuildings::enterTownHall()", 1)[0]
assert "GameConstants::SPELLBOOK_GOLD_COST" in guild
assert "artifactPurchaseQuote(basePrice, hero).cost" in guild
assert "canAfford(price)" in guild
assert "prompt.replaceNumber(price[EGameResID::GOLD])" in guild
assert "allTexts[214]" not in guild and "< 500" not in guild
university = (ROOT / "client/windows/GUIClasses.cpp").read_text()
scroll = university.split("CUniversityWindow::CItem::CItem(CUniversityWindow * _parent, SpellID", 1)[1].split("void CUniversityWindow::CItem::update()", 1)[0]
scroll_update = university.split("void CUniversityWindow::CItem::update()", 1)[1].split("void CUniversityWindow::", 1)[0]
for source in (scroll, scroll_update):
    assert "newHorizonsEstates::artifactPurchaseQuote(" in source
    assert "newHorizonsHouseOfWisdom::price(scrollID), parent->hero).cost" in source
    assert "canAfford(cost)" in source
assert 'std::to_string(cost[EGameResID::GOLD])' in scroll_update
skill = university.split("CUniversityWindow::CItem::CItem(CUniversityWindow * _parent, SecondarySkill", 1)[1].split("CUniversityWindow::CItem::CItem(CUniversityWindow * _parent, SpellID", 1)[0]
assert "newHorizonsUniversity::tuition(" in skill
assert "artifactPurchaseQuote" not in skill
interface = (ROOT / "client/CPlayerInterface.cpp").read_text()
property_handler = interface.split("void CPlayerInterface::objectPropertyChanged(", 1)[1].split("void CPlayerInterface::initializeHeroTownList()", 1)[0]
receipt = property_handler.split("ObjProperty::NEW_HORIZONS_HOARDER_LAST_WEEK", 1)[1].split("ObjProperty::OWNER", 1)[0]
assert "hero->getOwner() == playerID" in receipt and "receivedResource();" in receipt
assert "setProperty" not in receipt
refresh = interface.split("void CPlayerInterface::receivedResource()", 1)[1].split("void CPlayerInterface::heroInGarrisonChange", 1)[0]
assert "mw->updateResources();" in refresh and "mw->updateSecondarySkills();" in refresh
print("PASS: shared Estates economic quotes, artifact-only Hoarder prices and committed-receipt refresh")
print("Source routing only; native payment and rendered presentation remain unverified")
