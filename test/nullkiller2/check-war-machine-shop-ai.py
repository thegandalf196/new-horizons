#!/usr/bin/env python3
"""Focused source guard for Nullkiller2's town war-machine purchase pass."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
GATEWAY = (ROOT / "AI/Nullkiller2/AIGateway.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
	start = source.index(signature)
	opening = source.index("{", start)
	depth = 0
	for index in range(opening, len(source)):
		if source[index] == "{":
			depth += 1
		elif source[index] == "}":
			depth -= 1
			if depth == 0:
				return source[opening + 1:index]
	raise AssertionError(f"Unclosed function body: {signature}")


def require(condition: bool, message: str) -> None:
	if not condition:
		raise AssertionError(message)


turn = function_body(GATEWAY, "void AIGateway::makeTurn()")
require("requestedWarMachineArtifacts.clear();" in turn and "reservedWarMachineSlots.clear();" in turn,
	"per-turn request and slot reservations must reset before planning")
require(turn.index("nullkiller->makeTurn();") < turn.index("purchaseUsefulWarMachines();"),
	"machine purchases must use the post-planning resource and visitor state")
require(turn.index("purchaseUsefulWarMachines();") < turn.index("endTurn();"),
	"machine purchase requests must remain inside the AI player's turn")

town_interaction = function_body(GATEWAY, "void AIGateway::performObjectInteraction(")
require("purchaseUsefulWarMachines(visitedTown, heroPtr.get(), availableResources[EGameResID::GOLD]);" in town_interaction,
	"a visiting hero must shop before it can depart during this turn")
require("availableResources[EGameResID::GOLD] -= GameConstants::SPELLBOOK_GOLD_COST;" in town_interaction
	and "availableResources -= price;" in town_interaction,
	"town interaction purchases must reserve gold before war-machine affordability is evaluated")

movement = function_body(GATEWAY, "bool AIGateway::moveHeroToTile(")
require("startingTown->tempOwner == playerID" in movement
	and "startingTown->getVisitingHero() == heroPtr.get()" in movement
	and "purchaseUsefulWarMachines(startingTown, heroPtr.get());" in movement,
	"a hero already in an owned town must get a purchase opportunity before leaving")

ordinary = function_body(GATEWAY, "bool isOrdinaryWarMachine(ArtifactID artifact)")
for artifact in ("BALLISTA", "AMMO_CART", "FIRST_AID_TENT"):
	require(f"ArtifactID::{artifact}" in ordinary, f"ordinary inventory omits {artifact}")
require("ArtifactID::CATAPULT" in ordinary,
	"Catapult must be explicitly excluded from ordinary shop inventory")

usefulness = function_body(GATEWAY, "bool isUsefulWarMachineForHero(")
require("BonusType::SHOOTER" in usefulness,
	"Ammo Cart value must depend on a ranged stack in the hero's army")
require("BonusType::UNDEAD" in usefulness,
	"First Aid Tent value must exclude an army consisting only of Undead")

free_slot = function_body(GATEWAY, "std::optional<ArtifactPosition> findFreeWarMachineSlot(")
require("ArtifactPosition::MACH4" in free_slot and "reservedSlots.contains(slot)" in free_slot,
	"purchases must avoid Catapult's slot and slots reserved by this request batch")
require("hero.getArt(slot) == nullptr" in free_slot and "hero.isPositionFree(slot)" in free_slot,
	"purchases must not replace a machine in an occupied or unavailable slot")

purchases = function_body(GATEWAY, "void AIGateway::purchaseUsefulWarMachines(")
for required in (
	"cc->getTownsInfo()",
	"town->tempOwner != playerID",
	"town->getVisitingHero()",
	"hero->tempOwner != playerID",
	"town->getWarMachineShopOffers()",
	"(onlyTown && town != onlyTown)",
	"(onlyHero && hero != onlyHero)",
	"seenOffers.insert(offer.artifact)",
	"hero->hasArt(offer.artifact, false, false)",
	"getPotentialArtifactScore(artifact)",
	"offer.price",
	"purchase.price > warMachinePurchaseBudgetRemaining",
	"warMachinePurchaseBudgetRemaining -= purchase.price",
	"cc->buyArtifact(purchase.hero, purchase.artifact)",
	"requestedWarMachineArtifacts[purchase.hero->id].insert(purchase.artifact)",
	"reservedWarMachineSlots[purchase.hero->id].insert(*slot)",
):
	require(required in purchases, f"purchase pass is missing required guard/action: {required}")

print("Nullkiller2 war-machine shop source guard passed")
