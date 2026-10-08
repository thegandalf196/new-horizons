/*
 * GameStatePackVisitor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "GameStatePackVisitor.h"

#include "CGameState.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/NewHorizonsArmorer.h"
#include "../battle/NewHorizonsOffense.h"
#include "../battle/NewHorizonsWarcasting.h"
#include "../bonuses/BonusSelector.h"
#include "../spells/NewHorizonsMagic.h"
#include "../spells/NewHorizonsSpellAvailability.h"
#include "TavernHeroesPool.h"

#include "../CPlayerState.h"
#include "../CStack.h"
#include "../IGameSettings.h"

#include "../campaign/CampaignState.h"
#include "../entities/artifact/ArtifactUtils.h"
#include "../entities/artifact/CArtifact.h"
#include "../entities/artifact/CArtifactFittingSet.h"
#include "../entities/building/CBuilding.h"
#include "../entities/faction/CTown.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGMarket.h"
#include "../mapObjects/CGTownInstance.h"
#include "../mapObjects/Quest.h"
#include "../mapObjects/FlaggableMapObject.h"
#include "../mapObjects/MiscObjects.h"
#include "../mapObjects/TownBuildingInstance.h"
#include "../mapping/CMap.h"
#include "../networkPacks/StackLocation.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsMagic.h"

#include <algorithm>
#include <set>
#include <string>
#include <string_view>

namespace
{
constexpr std::string_view TOWN_DEFENDING_HERO_BONUS_STACKING_PREFIX = "townDefendingHero:";

std::string townDefendingHeroBonusStacking(const CBuilding & building, const Bonus & bonus, size_t index)
{
	const auto buildingType = building.getUniqueTypeID();
	std::string result(TOWN_DEFENDING_HERO_BONUS_STACKING_PREFIX);
	result += std::to_string(buildingType.getFaction().getNum());
	result += ':';
	result += std::to_string(buildingType.getBuilding().getNum());
	result += ':';
	if(bonus.stacking.empty())
	{
		result += "entry:";
		result += std::to_string(index);
	}
	else
	{
		result += "key:";
		result += bonus.stacking;
	}
	return result;
}

bool isReplacedTownBuilding(const CTown & townType, const std::set<BuildingID> & builtBuildings, BuildingID buildingID)
{
	return std::ranges::any_of(builtBuildings, [&](BuildingID upgradeID)
	{
		const auto upgrade = townType.buildings.find(upgradeID);
		return upgrade != townType.buildings.end()
			&& upgrade->second->getBase() == buildingID
			&& upgrade->second->upgradeReplacesBonuses;
	});
}

void grantTownDefendingHeroBonuses(CGTownInstance & town, CGHeroInstance & hero)
{
	const auto * townType = town.getTown();
	if(!townType)
		return;

	const auto builtBuildings = town.getBuildings();
	for(const BuildingID buildingID : builtBuildings)
	{
		const auto buildingIterator = townType->buildings.find(buildingID);
		if(buildingIterator == townType->buildings.end()
			|| isReplacedTownBuilding(*townType, builtBuildings, buildingID))
			continue;

		const CBuilding & building = *buildingIterator->second;
		for(size_t index = 0; index < building.defendingHeroBonuses.size(); ++index)
		{
			const auto & configuredBonus = building.defendingHeroBonuses[index];
			if(!configuredBonus)
				continue;

			auto bonus = std::make_shared<Bonus>(*configuredBonus);
			bonus->duration = BonusDuration::ONE_BATTLE;
			bonus->turnsRemain = 0;
			bonus->source = BonusSource::TOWN_STRUCTURE;
			bonus->sid = BonusSourceID(building.getUniqueTypeID());
			bonus->stacking = townDefendingHeroBonusStacking(building, *configuredBonus, index);
			if(bonus->description.empty())
				bonus->description.appendTextID(building.getNameTextID());
			hero.addNewBonus(bonus);
		}
	}
}

SideInBattle * findBattleSide(CGameState & gs, ObjectInstanceID heroID)
{
	for(auto & battle : gs.currentBattles)
	{
		if(!battle)
			continue;
		for(const auto sideID : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			auto & side = battle->getSide(sideID);
			if(side.heroID == heroID)
				return &side;
		}
	}
	return nullptr;
}

bool hasSameHeroOrderIssuance(const HeroOrderState & previous, const HeroOrderState & next)
{
	return previous.command == next.command
		&& previous.issuedRound == next.issuedRound
		&& previous.primaryTargetUnitId == next.primaryTargetUnitId
		&& previous.secondaryTargetUnitId == next.secondaryTargetUnitId
		&& previous.protectInterceptionLimit == next.protectInterceptionLimit
		&& previous.anchors == next.anchors
		&& previous.warcastingBonusPercent == next.warcastingBonusPercent
		&& previous.sacredCommandEfficiencyBonusPercent == next.sacredCommandEfficiencyBonusPercent
		&& previous.knightlySequenceEfficiencyBonusPercent == next.knightlySequenceEfficiencyBonusPercent
		&& previous.holdMagicalReductionBasisPoints == next.holdMagicalReductionBasisPoints;
}

void validateHeroOrderStateMutation(const IBattleInfo & battle, BattleSide side,
	const std::vector<HeroOrderState> & updated)
{
	const auto previous = battle.getHeroOrderStates(side);
	if(previous.size() != updated.size())
		throw std::runtime_error("Canonical Hero Order state update cannot add or remove issued Orders");

	std::set<uint32_t> referencedUnitIds;
	const auto addReference = [&referencedUnitIds](uint32_t unitId)
	{
		if(unitId != HeroOrderState::INVALID_UNIT_ID)
			referencedUnitIds.insert(unitId);
	};
	for(size_t index = 0; index < previous.size(); ++index)
	{
		const auto & before = previous[index];
		const auto & after = updated[index];
		if(!hasSameHeroOrderIssuance(before, after))
			throw std::runtime_error("Canonical Hero Order state update changed its issuance snapshot");
		const auto preservesProgress = [](const auto & nextProgress, const auto & previousProgress)
		{
			return std::includes(nextProgress.begin(), nextProgress.end(),
				previousProgress.begin(), previousProgress.end());
		};
		if(!preservesProgress(after.consumedUnitIds, before.consumedUnitIds)
			|| !preservesProgress(after.braceTriggeredUnitIds, before.braceTriggeredUnitIds)
			|| !preservesProgress(after.holdBrokenUnitIds, before.holdBrokenUnitIds)
			|| after.protectInterceptionsConsumed < before.protectInterceptionsConsumed
			|| (before.protectBroken && !after.protectBroken)
			|| after.flankTargets.size() != before.flankTargets.size())
			throw std::runtime_error("Canonical Hero Order state update reversed or discarded spent progress");
		for(size_t targetIndex = 0; targetIndex < before.flankTargets.size(); ++targetIndex)
		{
			const auto & oldTarget = before.flankTargets[targetIndex];
			const auto & newTarget = after.flankTargets[targetIndex];
			if(oldTarget.unitId != newTarget.unitId
				|| (newTarget.sideMask & oldTarget.sideMask) != oldTarget.sideMask)
				throw std::runtime_error("Canonical Flank update changed its target or discarded side progress");
		}

		addReference(after.primaryTargetUnitId);
		addReference(after.secondaryTargetUnitId);
		for(const auto unitId : after.consumedUnitIds)
			addReference(unitId);
		for(const auto unitId : after.braceTriggeredUnitIds)
			addReference(unitId);
		for(const auto unitId : after.holdBrokenUnitIds)
			addReference(unitId);
		for(const auto & anchor : after.anchors)
			addReference(anchor.unitId);
		for(const auto & target : after.flankTargets)
			addReference(target.unitId);
	}

	if(referencedUnitIds.empty())
		return;
	std::set<uint32_t> existingUnitIds;
	for(const auto * unit : battle.getUnitsIf([&referencedUnitIds](const battle::Unit * candidate)
		{
			return candidate && referencedUnitIds.contains(candidate->unitId());
		}))
		if(unit)
			existingUnitIds.insert(unit->unitId());
	if(existingUnitIds != referencedUnitIds)
		throw std::runtime_error("Canonical Hero Order state update references a missing battle unit");
}

void validateBattleSpellPointSnapshots(const BattleInfo & battle)
{
	for(const auto sideID : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & side = battle.getSide(sideID);
		if(side.initialNormalSpellPoints < 0 || side.initialBufferSpellPoints < 0 || side.temporaryBufferRemaining < 0)
			throw std::runtime_error("Invalid negative battle Spell Point snapshot");

		const int64_t initialAndTemporaryBuffer = static_cast<int64_t>(side.initialBufferSpellPoints)
			+ side.temporaryBufferRemaining;
		if(initialAndTemporaryBuffer > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Battle temporary Spell Point Buffer overflows its persistent snapshot");
	}
}

void applySpellPointMutation(CGameState & gs, CGHeroInstance & hero, const SetMana & pack)
{
	using Operation = SetMana::Operation;
	// Validate the original payload before translating its legacy representation.
	// Otherwise a nonzero typed amount on a legacy packet would be overwritten
	// before it could be rejected.
	pack.validateOperationPayload();
	auto operation = pack.operation;
	int64_t amount = pack.amount;
	int32_t bufferAmount = pack.bufferAmount;
	if(operation == Operation::LEGACY)
	{
		if(pack.mode != ChangeValueMode::ABSOLUTE && pack.mode != ChangeValueMode::RELATIVE)
			throw std::runtime_error("Invalid legacy Spell Point mutation mode");
		if(pack.mode == ChangeValueMode::ABSOLUTE)
		{
			operation = Operation::SET_NORMAL;
			amount = std::max<int32_t>(0, pack.val);
		}
		else if(pack.val < 0)
		{
			operation = Operation::SPEND;
			amount = -static_cast<int64_t>(pack.val);
		}
		else
		{
			operation = Operation::RESTORE_NORMAL;
			amount = pack.val;
		}
	}

	const int32_t bufferBefore = hero.getBufferSpellPoints();
	switch(operation)
	{
		case Operation::SET_NORMAL:
			hero.setNormalSpellPoints(static_cast<int32_t>(amount));
			break;
		case Operation::RESTORE_NORMAL:
			if(!hero.restoreNormalSpellPoints(static_cast<int32_t>(amount)))
				throw std::runtime_error("Invalid Normal Spell Point restoration");
			break;
		case Operation::SPEND:
			if(!hero.spendSpellPoints(amount))
				throw std::runtime_error("Insufficient Spell Points for authoritative spend");
			break;
		case Operation::GRANT_BUFFER:
			if(bufferAmount != 0 || !hero.grantBufferSpellPoints(static_cast<int32_t>(amount)))
				throw std::runtime_error("Invalid Buffer Spell Point grant");
			break;
		case Operation::RESTORE_SNAPSHOT:
			if(findBattleSide(gs, hero.id))
				throw std::runtime_error("Cannot restore Spell Point snapshot during battle without temporary Buffer provenance");
			hero.restoreSpellPointSnapshot(static_cast<int32_t>(amount), bufferAmount);
			break;
		case Operation::LEGACY:
			throw std::runtime_error("Unresolved legacy Spell Point mutation");
	}

	if(operation == Operation::SPEND)
	{
		if(auto * side = findBattleSide(gs, hero.id))
		{
			const int32_t spentFromBuffer = bufferBefore - hero.getBufferSpellPoints();
			side->temporaryBufferRemaining = std::max<int32_t>(0,
				side->temporaryBufferRemaining - std::min(side->temporaryBufferRemaining, spentFromBuffer));
		}
	}
}

void applyFormulaReserveClosureReward(BattleInfo & battle, BattleSide sideID)
{
	auto & side = battle.getSide(sideID);
	if(side.metamagicPendingCount == 0 || side.metamagicSequenceSpells.size() <= 1)
		return;

	auto * hero = battle.battleGetFightingHero(sideID);
	if(!hero || !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules())
		|| !newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_FORMULA_RESERVE))
		return;

	if(!hero->restoreNormalSpellPoints(newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS))
		throw std::runtime_error("Invalid Formula Reserve Normal Spell Point restoration");
	side.metamagicFormulaReserveUsed = true;
}

void applySpellBufferRoundExpiryReward(BattleInfo & battle, BattleSide sideID)
{
	auto & side = battle.getSide(sideID);
	// Both the initial Metamagic offer and Grand Metamagic's further action are
	// Spell Actions granted by Metamagic. Either qualifies when it expires unused
	// at the round boundary; the once-per-combat flag prevents a second reward.
	if(side.metamagicPendingCount == 0 || side.metamagicSequenceSpells.empty()
		|| side.metamagicSpellBufferUsed)
		return;

	auto * hero = battle.battleGetFightingHero(sideID);
	if(!hero || !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules())
		|| !newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_SPELL_BUFFER))
		return;

	if(!hero->grantBufferSpellPoints(newHorizonsMagic::METAMAGIC_SPELL_BUFFER_POINTS))
		throw std::runtime_error("Invalid Spell Buffer grant");
	// The combat reward is permanent even if the pool was already capped.
	side.metamagicSpellBufferUsed = true;
}

std::set<uint32_t> bloodrageDeathCandidates(BattleInfo & battle, const std::vector<BattleStackAttacked> & updates)
{
	std::set<uint32_t> result;
	for(const auto & update : updates)
	{
		const auto * unit = battle.getStack(update.stackAttacked, false);
		// BattleStackAttacked::KILLED is authored while preparing the transition;
		// the stack state may already reflect that transition before this packet is
		// applied, so it is the authoritative alive-to-dead marker here.
		if(update.killed() && !update.willRebirth() && unit
			&& !unit->acquireState()->summoned && !unit->isClone())
			result.insert(update.stackAttacked);
	}
	return result;
}

void recordBloodrageDeaths(BattleInfo & battle, const std::set<uint32_t> & candidates)
{
	for(const auto unitId : candidates)
		battle.recordBloodrageStackDeath(unitId);
}

void refreshBloodrageLivingUnits(BattleInfo & battle, const std::vector<BattleStackAttacked> & updates)
{
	for(const auto & update : updates)
	{
		const auto * unit = battle.getStack(update.stackAttacked, false);
		if(unit && unit->alive())
			battle.clearBloodrageStackDeath(update.stackAttacked);
	}
}

void removeExhaustedGuardianSpirit(BattleInfo & battle, const BattleStackAttacked & hit)
{
	if((hit.flags & BattleStackAttacked::GUARDIAN_SPIRIT_EXHAUSTED) == 0)
		return;

	auto * stack = battle.getStack(hit.stackAttacked, false);
	if(!stack)
		return;
	const auto markers = stack->getBonuses(Selector::type()(BonusType::GUARDIAN_SPIRIT));
	std::vector<Bonus> markersToRemove;
	if(markers)
		for(const auto & marker : *markers)
			if(marker)
				markersToRemove.push_back(*marker);

	if(markersToRemove.empty())
	{
		stack->guardianSpiritHitPoints = 0;
		stack->guardianSpiritRoundsRemaining = 0;
		return;
	}
	battle.removeUnitBonus(hit.stackAttacked, markersToRemove);
}

bool chainGateKillQualifies(BattleInfo & battle, uint32_t attackerId,
	const std::vector<BattleStackAttacked> & hits)
{
	const auto rewardSide = battle.gatedDemonicStackSide(attackerId);
	if(rewardSide != BattleSide::ATTACKER && rewardSide != BattleSide::DEFENDER)
		return false;
	const auto * attacker = battle.getStack(static_cast<int>(attackerId), false);
	if(!attacker)
		return false;
	const auto * hero = battle.battleGetFightingHero(rewardSide);
	if(!hero || !hero->hasActivePerk(
		"new-horizons:demonicGating", "new-horizons:demonicGating.chainGate")
		|| attacker->unitId() != attackerId)
		return false;

	return std::ranges::any_of(hits, [&battle, attackerId, rewardSide, attacker](const auto & hit)
	{
		if(hit.attackerID != attackerId || !hit.killed() || hit.cloneKilled() || hit.willRebirth())
			return false;
		const auto * victim = battle.getStack(hit.stackAttacked, false);
		// A hypnotized gated unit still belongs to the side whose gated-stack
		// record names it.  Compare the victim with that original side rather
		// than with the attacker's temporary controller.
		return victim && victim != attacker && victim->unitSide() != rewardSide;
	});
}
}

void GameStatePackVisitor::updateMoraleOnTroopMixingBonusChange(CBonusSystemNode * node, const Bonus & bonus)
{
	if(bonus.type != BonusType::ALIGNMENT_MIX && bonus.type != BonusType::NONEVIL_ALIGNMENT_MIX)
		return;

	if(auto * army = dynamic_cast<CArmedInstance *>(node))
		army->updateMoraleBonusFromArmy();

	//bonus given to a player is inherited by all armies that he owns
	if(auto * player = dynamic_cast<PlayerState *>(node))
		updateMoraleForPlayer(player->color);
}

void GameStatePackVisitor::updateMoraleForPlayer(const PlayerColor & player)
{
	auto * playerState = gs.getPlayerState(player, false);
	if(!playerState)
		return;

	for(auto * hero : playerState->getHeroes())
		hero->updateMoraleBonusFromArmy();
	for(auto * town : playerState->getTowns())
		town->updateMoraleBonusFromArmy();
}

void GameStatePackVisitor::updateMoraleOnArtifactChange(const ObjectInstanceID & artHolder)
{
	auto * army = dynamic_cast<CArmedInstance *>(gs.getObjInstance(artHolder));
	if(!army)
		return;

	army->updateMoraleBonusFromArmy();

	//troop-mixing artifact may be propagated to the player, in which case all armies that he owns are affected
	updateMoraleForPlayer(army->getOwner());
}

void GameStatePackVisitor::visitSetResources(SetResources & pack)
{
	assert(pack.player.isValidPlayer());
	if(pack.mode == ChangeValueMode::ABSOLUTE)
		gs.getPlayerState(pack.player)->resources = pack.res;
	else
		gs.getPlayerState(pack.player)->resources += pack.res;
	gs.getPlayerState(pack.player)->resources.amin(GameConstants::PLAYER_RESOURCES_CAP);

	//just ensure that player resources are not negative
	//server is responsible to check if player can afford deal
	//but events on server side are allowed to take more than player have
	gs.getPlayerState(pack.player)->resources.positive();
}

void GameStatePackVisitor::reconcileSpellPointCapacity()
{
	if(!needsSpellPointReconciliation()
		|| !newHorizonsMagic::spellPointRulesActive(gs.getMagicRules()))
		return;
	const auto reconcile = [](CGHeroInstance * hero)
	{
		if(hero && hero->areSpellPointsInitialized())
			hero->clampSpellPointsToCapacity();
	};
	if(spellPointBonusGraphChanged)
	{
		// Shared/propagated bonuses can affect more than the packet's direct target.
		// Each hero skips recalculation when its own bonus revision is unchanged.
		for(auto * hero : gs.getMap().getObjects<CGHeroInstance>())
			reconcile(hero);
		for(const auto type : gs.getMap().getHeroesInPool())
			reconcile(gs.getMap().tryGetFromHeroPool(type));
	}
	else
		for(const auto id : spellPointHeroes)
			reconcile(gs.getHero(id));
}

void GameStatePackVisitor::visitSetPrimarySkill(SetPrimarySkill & pack)
{
	CGHeroInstance * hero = gs.getHero(pack.id);
	assert(hero);
	const auto oldKnowledge = hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE);
	hero->setPrimarySkill(pack.which, pack.val, pack.mode);
	if(pack.which == PrimarySkill::KNOWLEDGE
		&& hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE) < oldKnowledge)
		spellPointHeroes.insert(pack.id);
}

void GameStatePackVisitor::visitSetHeroExperience(SetHeroExperience & pack)
{
	CGHeroInstance * hero = gs.getHero(pack.id);
	assert(hero);
	hero->setExperience(pack.val, pack.mode);
}

void GameStatePackVisitor::visitGiveStackExperience(GiveStackExperience & pack)
{
	auto * army = gs.getArmyInstance(pack.id);

	for (const auto & slot : pack.val)
		army->getStackPtr(slot.first)->giveAverageStackExperience(slot.second);

	army->nodeHasChanged();
}

void GameStatePackVisitor::visitSetSecSkill(SetSecSkill & pack)
{
	CGHeroInstance *hero = gs.getHero(pack.id);
	const auto previousRank = hero->getSecSkillLevel(pack.which);
	hero->setSecSkillLevel(pack.which, pack.val, pack.mode);
	if(hero->getSecSkillLevel(pack.which) < previousRank)
		spellPointHeroes.insert(pack.id);
}

void GameStatePackVisitor::visitSetCommanderProperty(SetCommanderProperty & pack)
{
	spellPointBonusGraphChanged = true;
	const auto & commander = gs.getHero(pack.heroid)->getCommander();
	assert (commander);

	switch (pack.which)
	{
		case SetCommanderProperty::BONUS:
			commander->accumulateBonus (std::make_shared<Bonus>(pack.accumulatedBonus));
			break;
		case SetCommanderProperty::SPECIAL_SKILL:
			commander->accumulateBonus (std::make_shared<Bonus>(pack.accumulatedBonus));
			commander->specialSkills.insert (pack.additionalInfo);
			break;
		case SetCommanderProperty::SECONDARY_SKILL:
			commander->secondarySkills[pack.additionalInfo] = static_cast<ui8>(pack.amount);
			break;
		case SetCommanderProperty::ALIVE:
			if (pack.amount)
				commander->setAlive(true);
			else
				commander->setAlive(false);
			break;
		case SetCommanderProperty::EXPERIENCE:
			commander->giveTotalStackExperience(pack.amount);
			commander->nodeHasChanged();
			break;
	}
}

void GameStatePackVisitor::visitAddQuest(AddQuest & pack)
{
	assert(vstd::contains(gs.players, pack.player));
	auto * vec = &gs.players.at(pack.player).quests;
	if (!vstd::contains(*vec, pack.quest))
		vec->push_back(pack.quest);
	else
		logNetwork->warn("Warning! Attempt to add duplicated quest");
}

void GameStatePackVisitor::visitInfoWindow(InfoWindow & pack)
{
	if(!pack.journalInfo || pack.text.empty())
		return;

	assert(vstd::contains(gs.players, pack.player));
	ScenarioEventJournalEntry entry;
	entry.day = gs.day;
	entry.message = pack.text;
	entry.location = pack.journalInfo->location;
	entry.components = pack.components;
	gs.players.at(pack.player).scenarioEventJournal.push_back(std::move(entry));
}

void GameStatePackVisitor::visitChangeFormation(ChangeFormation & pack)
{
	gs.getHero(pack.hid)->setFormation(pack.formation);
}

void GameStatePackVisitor::visitChangeTactics(ChangeTactics & pack)
{
	gs.getHero(pack.hid)->tacticFormationEnabled = pack.enabled;
}

void GameStatePackVisitor::visitChangeTownName(ChangeTownName & pack)
{
	gs.getTown(pack.tid)->setCustomName(gs.getMap(), pack.name);
}

void GameStatePackVisitor::visitHeroVisitCastle(HeroVisitCastle & pack)
{
	spellPointBonusGraphChanged = true;
	CGHeroInstance *h = gs.getHero(pack.hid);
	CGTownInstance *t = gs.getTown(pack.tid);

	assert(h);
	assert(t);

	if(pack.start())
		t->setVisitingHero(h);
	else
		t->setVisitingHero(nullptr);
}

void GameStatePackVisitor::visitChangeSpells(ChangeSpells & pack)
{
	CGHeroInstance *hero = gs.getHero(pack.hid);

	if(pack.learn)
		for(const auto & sid : pack.spells)
			hero->addSpellToSpellbook(sid);
	else
		for(const auto & sid : pack.spells)
			hero->removeSpellFromSpellbook(sid);
}

void GameStatePackVisitor::visitSetResearchedSpells(SetResearchedSpells & pack)
{
	CGTownInstance *town = gs.getTown(pack.tid);

	town->spells[pack.level] = pack.spells;
	town->spellResearchCounterDay++;
	if(pack.accepted)
	{
		town->spellResearchAcceptedCounter++;
		town->spellResearchPendingRerollsCounters[pack.level] = 0;
	}
	else
	{
		town->spellResearchPendingRerollsCounters[pack.level]++;
	}
}

void GameStatePackVisitor::visitSetMana(SetMana & pack)
{
	CGHeroInstance * hero = gs.getHero(pack.hid);

	assert(hero);
	applySpellPointMutation(gs, *hero, pack);
}

void GameStatePackVisitor::visitSetNewHorizonsAdventureSpellState(SetNewHorizonsAdventureSpellState & pack)
{
	if(auto * hero = gs.getHero(pack.hid))
		hero->setNewHorizonsAdventureSpellCastToday(pack.castToday);
}

void GameStatePackVisitor::visitSetNewHorizonsCastleGateState(SetNewHorizonsCastleGateState & pack)
{
	if(auto * hero = gs.getHero(pack.hid))
		hero->markNewHorizonsCastleGateUsed(pack.lastUseDay);
}

void GameStatePackVisitor::visitSetNewHorizonsForcedMarchState(SetNewHorizonsForcedMarchState & pack)
{
	if(!pack.hasValidState())
		throw std::runtime_error("Invalid New Horizons Forced March state packet");
	auto * hero = gs.getHero(pack.heroID);
	if(!hero)
		throw std::runtime_error("New Horizons Forced March state references a missing hero");
	hero->setNewHorizonsForcedMarchState(pack.lastUseDay, pack.penaltyDay);
}

void GameStatePackVisitor::visitSetNewHorizonsMusterState(SetNewHorizonsMusterState & pack)
{
	if(auto * hero = gs.getHero(pack.heroId))
		hero->markNewHorizonsMusterUsed(pack.lastUseWeek, pack.usesThisWeek);
	if(auto * target = dynamic_cast<CGDwelling *>(gs.getObjInstance(pack.targetId)))
		target->markNewHorizonsMusterUsed(pack.lastUseWeek);
}

void GameStatePackVisitor::visitSetNewHorizonsLearningMentorState(SetNewHorizonsLearningMentorState & pack)
{
	if(auto * hero = gs.getHero(pack.heroId))
		hero->markNewHorizonsLearningMentorUsed(pack.lastUseWeek);
}

void GameStatePackVisitor::visitSetNewHorizonsDiplomacyState(SetNewHorizonsDiplomacyState & pack)
{
	if(!pack.hasValidState())
		throw std::runtime_error("Invalid New Horizons Diplomacy state packet");
	auto * hero = gs.getHero(pack.heroId);
	if(!hero)
		throw std::runtime_error("New Horizons Diplomacy state references a missing hero");
	hero->setNewHorizonsDiplomacyState(pack.peacemakerLastWeek, pack.pacifiedCreatureId,
		pack.tributeLastWeek, pack.pactExpiryDay);
}

void GameStatePackVisitor::visitSetNewHorizonsDemonicReserve(SetNewHorizonsDemonicReserve & pack)
{
	if(auto * hero = gs.getHero(pack.heroId))
		hero->setDemonicReserve(std::move(pack.reserve));
}

void GameStatePackVisitor::visitSetPortalDwellingSource(SetPortalDwellingSource & pack)
{
	auto * town = dynamic_cast<CGTownInstance *>(gs.getObjInstance(pack.townId));
	const auto * source = dynamic_cast<const CGDwelling *>(gs.getObjInstance(pack.sourceDwellingId));
	if(!town || !source
		|| (source->ID != Obj::CREATURE_GENERATOR1 && source->ID != Obj::CREATURE_GENERATOR4)
		|| pack.lastSelectionWeek < 0)
		throw std::runtime_error("Invalid New Horizons Portal source state packet");

	town->portalSourceDwellingId = pack.sourceDwellingId;
	town->portalLastSelectionWeek = pack.lastSelectionWeek;
}

void GameStatePackVisitor::visitSetMovePoints(SetMovePoints & pack)
{
	CGHeroInstance *hero = gs.getHero(pack.hid);
	assert(hero);
	hero->setMovementPoints(pack.val);
}

void GameStatePackVisitor::visitFoWChange(FoWChange & pack)
{
	TeamState * team = gs.getPlayerTeam(pack.player);
	auto & fogOfWarMap = team->fogOfWarMap;
	for(const int3 & t : pack.tiles)
		fogOfWarMap[t] = pack.mode != ETileVisibility::HIDDEN;

	if (pack.mode == ETileVisibility::HIDDEN) //do not hide too much
	{
		FowTilesType tilesRevealed;
		for (auto & o : gs.getMap().getObjects())
		{
			if (o->asOwnable())
			{
				if(vstd::contains(team->players, o->getOwner())) //check owned observators
					gs.getTilesInRange(tilesRevealed, o->getSightCenter(), o->getSightRadius(), ETileVisibility::HIDDEN, o->tempOwner);
			}
		}
		for(const int3 & t : tilesRevealed) //probably not the most optimal solution ever
			fogOfWarMap[t] = 1;
	}
}

void GameStatePackVisitor::visitSetAvailableHero(SetAvailableHero & pack)
{
	spellPointBonusGraphChanged = true;
	gs.heroesPool->setHeroForPlayer(pack.player, pack.slotID, pack.hid, pack.army, pack.roleID, pack.replenishPoints);
}

void GameStatePackVisitor::visitGiveBonus(GiveBonus & pack)
{
	spellPointBonusGraphChanged = true;
	CBonusSystemNode *cbsn = nullptr;
	switch(pack.who)
	{
		case GiveBonus::ETarget::OBJECT:
			cbsn = dynamic_cast<CBonusSystemNode*>(gs.getObjInstance(pack.id.as<ObjectInstanceID>()));
			break;
		case GiveBonus::ETarget::HERO_COMMANDER:
			cbsn = gs.getHero(pack.id.as<ObjectInstanceID>())->getCommander();
			break;
		case GiveBonus::ETarget::PLAYER:
			cbsn = gs.getPlayerState(pack.id.as<PlayerColor>());
			break;
		case GiveBonus::ETarget::BATTLE:
			assert(Bonus::OneBattle(&pack.bonus));
			cbsn = dynamic_cast<CBonusSystemNode*>(gs.getBattle(pack.id.as<BattleID>()));
			break;
	}

	assert(cbsn);

	if(Bonus::OneWeek(&pack.bonus))
	{
		auto calendar = gs.getCalendar();
		pack.bonus.turnsRemain = calendar.getDaysInWeek() + 1 - calendar.getDayOfWeek(); // set correct number of days before adding bonus
	}

	auto b = std::make_shared<Bonus>(pack.bonus);
	cbsn->addNewBonus(b);

	updateMoraleOnTroopMixingBonusChange(cbsn, *b);
}

void GameStatePackVisitor::visitChangeObjPos(ChangeObjPos & pack)
{
	CGObjectInstance *obj = gs.getObjInstance(pack.objid);
	if(!obj)
	{
		logNetwork->error("Wrong ChangeObjPos: object %d doesn't exist!", pack.objid.getNum());
		return;
	}
	gs.getMap().moveObject(pack.objid, pack.nPos + obj->getVisitableOffset());
}

void GameStatePackVisitor::visitChangeObjectVisitors(ChangeObjectVisitors & pack)
{
	auto objectPtr = gs.getObjInstance(pack.object);

	switch (pack.mode)
	{
		case ChangeObjectVisitors::VISITOR_ADD_HERO:
			gs.getHero(pack.hero)->visitedObjects.insert(pack.object);
			[[fallthrough]];
		case ChangeObjectVisitors::VISITOR_ADD_PLAYER:
			gs.getPlayerTeam(gs.getHero(pack.hero)->tempOwner)->scoutedObjects.insert(pack.object);
			gs.getPlayerState(gs.getHero(pack.hero)->tempOwner)->visitedObjects.insert(pack.object);
			gs.getPlayerState(gs.getHero(pack.hero)->tempOwner)->visitedObjectsGlobal.insert({objectPtr->ID, objectPtr->subID});
			break;

		case ChangeObjectVisitors::VISITOR_CLEAR:
			// remove visit info from all heroes, including those that are not present on map
			for (auto heroID : gs.getMap().getHeroesOnMap())
				gs.getHero(heroID)->visitedObjects.erase(pack.object);

			for (auto heroID : gs.getMap().getHeroesInPool())
				gs.getMap().tryGetFromHeroPool(heroID)->visitedObjects.erase(pack.object);

			for(auto &elem : gs.players)
				elem.second.visitedObjects.erase(pack.object);

			for(auto &elem : gs.teams)
				elem.second.scoutedObjects.erase(pack.object);

			break;
		case ChangeObjectVisitors::VISITOR_SCOUTED:
			gs.getPlayerTeam(gs.getHero(pack.hero)->tempOwner)->scoutedObjects.insert(pack.object);
			break;
	}
}

void GameStatePackVisitor::visitChangeArtifactsCostume(ChangeArtifactsCostume & pack)
{
	auto & allCostumes = gs.getPlayerState(pack.player)->costumesArtifacts;
	if(const auto & costume = allCostumes.find(pack.costumeIdx); costume != allCostumes.end())
		costume->second = pack.costumeSet;
	else
		allCostumes.try_emplace(pack.costumeIdx, pack.costumeSet);
}

void GameStatePackVisitor::visitPlayerEndsGame(PlayerEndsGame & pack)
{
	PlayerState *p = gs.getPlayerState(pack.player);
	if(pack.victoryLossCheckResult.victory())
	{
		p->status = EPlayerStatus::WINNER;

		// TODO: Campaign-specific code might as well go somewhere else
		// keep all heroes from the winning player
		if(p->human && gs.getStartInfo()->campState)
		{
			std::vector<CGHeroInstance *> crossoverHeroes;
			for (auto hero : p->getHeroes())
				if (hero->tempOwner == pack.player)
					crossoverHeroes.push_back(hero);

			gs.getStartInfo()->campState->savePersistentVariables(gs.getMap());
			gs.getStartInfo()->campState->setCurrentMapAsConquered(crossoverHeroes);
		}
	}
	else
	{
		p->status = EPlayerStatus::LOSER;
	}

	// defeated player may be making turn right now
	gs.actingPlayers.erase(pack.player);
}

void GameStatePackVisitor::visitRemoveBonus(RemoveBonus & pack)
{
	spellPointBonusGraphChanged = true;
	CBonusSystemNode *node = nullptr;
	switch(pack.who)
	{
		case GiveBonus::ETarget::OBJECT:
			node = dynamic_cast<CBonusSystemNode*>(gs.getObjInstance(pack.whoID.as<ObjectInstanceID>()));
			break;
		case GiveBonus::ETarget::PLAYER:
			node = gs.getPlayerState(pack.whoID.as<PlayerColor>());
			break;
		case GiveBonus::ETarget::BATTLE:
			assert(Bonus::OneBattle(&pack.bonus));
			node = dynamic_cast<CBonusSystemNode*>(gs.getBattle(pack.whoID.as<BattleID>()));
			break;
	}

	BonusList &bonuses = node->getExportedBonusList();
	std::shared_ptr<Bonus> removedBonus;

	for(const auto & b : bonuses)
	{
		if(b->source == pack.source && b->sid == pack.id)
		{
			pack.bonus = *b; //backup bonus (to show to interfaces later)
			removedBonus = b;
			node->removeBonus(b);
			break;
		}
	}

	if(removedBonus)
		updateMoraleOnTroopMixingBonusChange(node, *removedBonus);
}

void GameStatePackVisitor::visitRemoveObject(RemoveObject & pack)
{
	spellPointBonusGraphChanged = true;
	CGObjectInstance *obj = gs.getObjInstance(pack.objectID);
	logGlobal->debug("removing object id=%d; address=%x; name=%s", pack.objectID, (intptr_t)obj, obj->getObjectNameTextID());

	if (pack.initiator.isValidPlayer())
		gs.getPlayerState(pack.initiator)->destroyedObjects.insert(pack.objectID);

	if(obj->getOwner().isValidPlayer())
		gs.getPlayerState(obj->getOwner())->removeOwnedObject(obj); //object removed via map event or hero got beaten

	if(obj->ID == Obj::HERO) //remove beaten hero
	{
		// Diagnostic: heroes engaged in active battles should only be removed
		// via BattleResultProcessor::battleFinalize, which clears heroID inside
		// visitBattleResultsApplied before sending RemoveObject. If a side
		// still references this hero, something else is removing the hero
		// mid-battle - cause of A19 (iOS #7503) which we haven't pinned down.
		// Surface the call stack via the thrown exception's .what() so the
		// next Google Play / TestFlight report points at the culprit.
		for (const auto & battle : gs.currentBattles)
			for (auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
				if (battle->getSide(side).heroID == pack.objectID)
					throw std::runtime_error("Hero " + std::to_string(pack.objectID.getNum())
						+ " is being removed while still engaged in battle "
						+ std::to_string(battle->battleID.getNum()));

		auto beatenHero = dynamic_cast<CGHeroInstance*>(obj);
		assert(beatenHero);

		vstd::erase_if(beatenHero->artifactsInBackpack, [](const ArtSlotInfo& asi)
		{
			return asi.getArt()->getTypeId() == ArtifactID::GRAIL;
		});

		if(beatenHero->getVisitedTown())
		{
			if(beatenHero->getVisitedTown()->getGarrisonHero() == beatenHero)
				beatenHero->getVisitedTown()->setGarrisonedHero(nullptr);
			else
				beatenHero->getVisitedTown()->setVisitingHero(nullptr);

			beatenHero->setVisitedTown(nullptr, false);
		}

		//If hero on Boat is removed, the Boat disappears
		if(beatenHero->inBoat())
		{
			auto boat = beatenHero->getBoat();
			beatenHero->setBoat(nullptr);
			gs.getMap().eraseObject(boat->id);
		}

		beatenHero->detachFromBonusSystem(gs);
		// The hero is about to leave the map and become pool-owned. BattleInfo
		// clears this borrowed link from its destructor by looking the army up on
		// the map; that lookup cannot find a hero once eraseObject has moved it
		// into the hero pool. BattleResultsApplied has already released the hero
		// from the active battle above, so it no longer needs battle-scoped rules.
		beatenHero->battle = nullptr;
		beatenHero->tempOwner = PlayerColor::NEUTRAL; //no one owns beaten hero
		auto beatenObject = gs.getMap().eraseObject(obj->id);

		//return hero to the pool, so he may reappear in tavern
		gs.heroesPool->addHeroToPool(beatenHero->getHeroTypeID());
		gs.getMap().addToHeroPool(std::dynamic_pointer_cast<CGHeroInstance>(beatenObject));
		return;
	}

	if(obj->ID == Obj::TOWN)
	{
		auto * town = dynamic_cast<CGTownInstance *>(obj);
		town->setVisitingHero(nullptr);

		if (town->getGarrisonHero())
		{
			gs.getMap().showObject(gs.getHero(town->getGarrisonHero()->id));
			town->setGarrisonedHero(nullptr);
		}
	}

	if (obj->asQuestSource())
	{
		// Drop this object's own quest-log entry. Border guards/gates are tracked by
		// keymaster colour, not by instance, so their shared entry is not matched here
		// and correctly survives while other borders (or none) of the colour remain.
		const QuestInfo removed(obj->id);
		for (auto &player : gs.players)
			vstd::erase_if(player.second.quests, [&removed](const QuestInfo & q){ return q == removed; });
	}

	int3 objPosition = obj->anchorPos();
	int3 objDimensions(obj->getWidth(), obj->getHeight(), 1);

	obj->detachFromBonusSystem(gs);
	gs.getMap().eraseObject(pack.objectID);
	gs.getMap().calculateGuardingGreaturePositions(objPosition - objDimensions - int3(1,1,0), objPosition + int3(1,1,0));
}

static int getDir(const int3 & src, const int3 & dst)
{
	int ret = -1;
	if(dst.x+1 == src.x && dst.y+1 == src.y) //tl
	{
		ret = 1;
	}
	else if(dst.x == src.x && dst.y+1 == src.y) //t
	{
		ret = 2;
	}
	else if(dst.x-1 == src.x && dst.y+1 == src.y) //tr
	{
		ret = 3;
	}
	else if(dst.x-1 == src.x && dst.y == src.y) //r
	{
		ret = 4;
	}
	else if(dst.x-1 == src.x && dst.y-1 == src.y) //br
	{
		ret = 5;
	}
	else if(dst.x == src.x && dst.y-1 == src.y) //b
	{
		ret = 6;
	}
	else if(dst.x+1 == src.x && dst.y-1 == src.y) //bl
	{
		ret = 7;
	}
	else if(dst.x+1 == src.x && dst.y == src.y) //l
	{
		ret = 8;
	}
	return ret;
}

void GameStatePackVisitor::visitTryMoveHero(TryMoveHero & pack)
{
	CGHeroInstance *h = gs.getHero(pack.id);
	if (!h)
	{
		logGlobal->error("Attempt ot move unavailable hero %d", pack.id.getNum());
		return;
	}

	const TerrainTile & fromTile = gs.getMap().getTile(h->convertToVisitablePos(pack.start));
	const TerrainTile & destTile = gs.getMap().getTile(h->convertToVisitablePos(pack.end));

	h->setMovementPoints(pack.movePoints);

	if((pack.result == TryMoveHero::SUCCESS || pack.result == TryMoveHero::BLOCKING_VISIT || pack.result == TryMoveHero::EMBARK || pack.result == TryMoveHero::DISEMBARK) && pack.start != pack.end)
	{
		auto dir = getDir(pack.start, pack.end);
		if(dir > 0  &&  dir <= 8)
			h->moveDir = dir;
		//else don`t change move direction - hero might have traversed the subterranean gate, direction should be kept
	}

	if(pack.result == TryMoveHero::EMBARK) //hero enters boat at destination tile
	{
		const TerrainTile &tt = gs.getMap().getTile(h->convertToVisitablePos(pack.end));
		ObjectInstanceID topObjectID = tt.visitableObjects.back();
		CGObjectInstance * topObject = gs.getObjInstance(topObjectID);
		assert(tt.visitableObjects.size() >= 1 && topObject->ID == Obj::BOAT); //the only visitable object at destination is Boat
		auto * boat = dynamic_cast<CGBoat *>(topObject);
		assert(boat);

		gs.getMap().hideObject(boat); //hero blockvis mask will be used, we don't need to duplicate it with boat
		h->setBoat(boat);
	}
	else if(pack.result == TryMoveHero::DISEMBARK) //hero leaves boat to destination tile
	{
		auto * b = h->getBoat();
		b->direction = h->moveDir;
		b->pos = pack.start;
		gs.getMap().showObject(b);
		h->setBoat(nullptr);
	}

	if(pack.start != pack.end && (pack.result == TryMoveHero::SUCCESS || pack.result == TryMoveHero::TELEPORTATION || pack.result == TryMoveHero::EMBARK || pack.result == TryMoveHero::DISEMBARK))
	{
		gs.getMap().hideObject(h);
		h->setAnchorPos(pack.end);
		if(auto * b = h->getBoat())
			b->setAnchorPos(pack.end);
		gs.getMap().showObject(h);
	}

	auto & fogOfWarMap = gs.getPlayerTeam(h->getOwner())->fogOfWarMap;
	for(const int3 & t : pack.fowRevealed)
		fogOfWarMap[t] = 1;

	if (fromTile.getTerrainID() != destTile.getTerrainID())
		h->nodeHasChanged(); // update bonuses with terrain limiter
}

void GameStatePackVisitor::visitNewStructures(NewStructures & pack)
{
	spellPointBonusGraphChanged = true;
	CGTownInstance *t = gs.getTown(pack.tid);

	for(const auto & id : pack.bid)
	{
		assert(t->getTown()->buildings.at(id) != nullptr);
		t->addBuilding(id);
	}
	t->updateAppearance();
	t->built = pack.built;
	t->recreateBuildingsBonuses();
	if(pack.nextAstrologyWeek)
		gs.nextAstrologyWeek = *pack.nextAstrologyWeek;
}

void GameStatePackVisitor::visitRazeStructures(RazeStructures & pack)
{
	spellPointBonusGraphChanged = true;
	CGTownInstance *t = gs.getTown(pack.tid);
	for(const auto & id : pack.bid)
	{
		t->removeBuilding(id);

		t->updateAppearance();
	}
	t->destroyed = pack.destroyed; //yeaha
	t->recreateBuildingsBonuses();
}

void GameStatePackVisitor::visitSetAvailableCreatures(SetAvailableCreatures & pack)
{
	auto * dw = dynamic_cast<CGDwelling *>(gs.getObjInstance(pack.tid));
	assert(dw);
	dw->creatures = pack.creatures;
}

void GameStatePackVisitor::visitSetHeroesInTown(SetHeroesInTown & pack)
{
	spellPointBonusGraphChanged = true;
	CGTownInstance *t = gs.getTown(pack.tid);

	CGHeroInstance * v = gs.getHero(pack.visiting);
	CGHeroInstance * g = gs.getHero(pack.garrison);

	bool newVisitorComesFromGarrison = v && v == t->getGarrisonHero();
	bool newGarrisonComesFromVisiting = g && g == t->getVisitingHero();

	if(newVisitorComesFromGarrison)
		t->setGarrisonedHero(nullptr);
	if(newGarrisonComesFromVisiting)
		t->setVisitingHero(nullptr);
	if(!newGarrisonComesFromVisiting || v)
		t->setVisitingHero(v);
	if(!newVisitorComesFromGarrison || g)
		t->setGarrisonedHero(g);

	if(v)
		gs.getMap().showObject(v);

	if(g)
		gs.getMap().hideObject(g);
}

void GameStatePackVisitor::visitHeroRecruited(HeroRecruited & pack)
{
	spellPointBonusGraphChanged = true;
	auto h = gs.heroesPool->takeHeroFromPool(pack.hid);
	CGTownInstance *t = gs.getTown(pack.tid);
	PlayerState *p = gs.getPlayerState(pack.player);

	if (pack.boatId.hasValue())
	{
		CGObjectInstance *obj = gs.getObjInstance(pack.boatId);
		auto * boat = dynamic_cast<CGBoat *>(obj);
		if (boat)
		{
			gs.getMap().hideObject(boat);
			h->setBoat(boat);
		}
	}

	h->setOwner(pack.player);
	h->pos = pack.tile;
	h->updateAppearance();

	// Heroes taken from the tavern pool may carry a stale instance name from an
	// earlier lifetime or from an older save that reconstructed uidCounter from
	// on-map objects only. Always assign a fresh map-unique name on recruitment.
	gs.getMap().generateUniqueInstanceName(h.get());

	gs.getMap().addNewObject(h);
	assert(h->id.hasValue());

	p->addOwnedObject(h.get());
	h->attachToBonusSystem(gs);

	if(t)
		t->setVisitingHero(h.get());
}

void GameStatePackVisitor::visitGiveHero(GiveHero & pack)
{
	spellPointBonusGraphChanged = true;
	CGHeroInstance *h = gs.getHero(pack.id);

	if (pack.boatId.hasValue())
	{
		CGObjectInstance *obj = gs.getObjInstance(pack.boatId);
		auto * boat = dynamic_cast<CGBoat *>(obj);
		if (boat)
		{
			gs.getMap().hideObject(boat);
			h->setBoat(boat);
		}
	}

	//bonus system
	h->detachFrom(gs.globalEffects);
	h->attachTo(*gs.getPlayerState(pack.player));

	auto oldVisitablePos = h->visitablePos();
	gs.getMap().hideObject(h);
	h->updateAppearance();

	h->setOwner(pack.player);
	h->setMovementPoints(h->movementPointsLimit());
	h->setAnchorPos(h->convertFromVisitablePos(oldVisitablePos));
	gs.getPlayerState(h->getOwner())->addOwnedObject(h);

	gs.getMap().showObject(h);
	h->setVisitedTown(nullptr, false);
}

void GameStatePackVisitor::visitNewObject(NewObject & pack)
{
	spellPointBonusGraphChanged = true;
	int3 objPosition = pack.newObject->anchorPos();
	int3 objDimensions(pack.newObject->getWidth(), pack.newObject->getHeight(), 1);

	gs.getMap().addNewObject(pack.newObject);
	gs.getMap().calculateGuardingGreaturePositions(objPosition - objDimensions - int3(1,1,0), objPosition + int3(1,1,0));

	// attach newly spawned wandering monster to global bonus system node
	auto newArmy = std::dynamic_pointer_cast<CArmedInstance>(pack.newObject);
	if (newArmy)
		newArmy->attachToBonusSystem(gs);

	logGlobal->debug("Added object id=%d; name=%s", pack.newObject->id, pack.newObject->getObjectNameTextID());
}

void GameStatePackVisitor::visitNewArtifact(NewArtifact & pack)
{
	auto art = gs.createArtifact(pack.artId, pack.spellId);
	PutArtifact pa(art->getId(), ArtifactLocation(pack.artHolder, pack.pos), false);
	pa.visit(*this);
}

void GameStatePackVisitor::visitChangeStackCount(ChangeStackCount & pack)
{
	auto * srcObj = gs.getArmyInstance(pack.army);
	if(!srcObj)
		throw std::runtime_error("ChangeStackCount: invalid army object " + std::to_string(pack.army.getNum()) + ", possible game state corruption.");

	if(pack.mode == ChangeValueMode::ABSOLUTE)
		srcObj->setStackCount(pack.slot, pack.count);
	else
		srcObj->changeStackCount(pack.slot, pack.count);
}

void GameStatePackVisitor::visitSetStackType(SetStackType & pack)
{
	auto * srcObj = gs.getArmyInstance(pack.army);
	if(!srcObj)
		throw std::runtime_error("SetStackType: invalid army object " + std::to_string(pack.army.getNum()) + ", possible game state corruption.");

	srcObj->setStackType(pack.slot, pack.type);
}

void GameStatePackVisitor::visitEraseStack(EraseStack & pack)
{
	auto * srcObj = gs.getArmyInstance(pack.army);
	if(!srcObj)
		throw std::runtime_error("EraseStack: invalid army object " + std::to_string(pack.army.getNum()) + ", possible game state corruption.");

	srcObj->eraseStack(pack.slot);
}

void GameStatePackVisitor::visitSwapStacks(SwapStacks & pack)
{
	auto * srcObj = gs.getArmyInstance(pack.srcArmy);
	if(!srcObj)
		throw std::runtime_error("SwapStacks: invalid army object " + std::to_string(pack.srcArmy.getNum()) + ", possible game state corruption.");

	auto * dstObj = gs.getArmyInstance(pack.dstArmy);
	if(!dstObj)
		throw std::runtime_error("SwapStacks: invalid army object " + std::to_string(pack.dstArmy.getNum()) + ", possible game state corruption.");

	auto s1 = srcObj->detachStack(pack.srcSlot);
	auto s2 = dstObj->detachStack(pack.dstSlot);

	srcObj->putStack(pack.srcSlot, std::move(s2));
	dstObj->putStack(pack.dstSlot, std::move(s1));
}

void GameStatePackVisitor::visitInsertNewStack(InsertNewStack & pack)
{
	if(auto * obj = gs.getArmyInstance(pack.army))
		obj->putStack(pack.slot, std::make_unique<CStackInstance>(&gs, pack.type, pack.count));
	else
		throw std::runtime_error("InsertNewStack: invalid army object " + std::to_string(pack.army.getNum()) + ", possible game state corruption.");
}

void GameStatePackVisitor::visitRebalanceStacks(RebalanceStacks & pack)
{
	auto * srcObj = gs.getArmyInstance(pack.srcArmy);
	if(!srcObj)
		throw std::runtime_error("RebalanceStacks: invalid army object " + std::to_string(pack.srcArmy.getNum()) + ", possible game state corruption.");

	auto * dstObj = gs.getArmyInstance(pack.dstArmy);
	if(!dstObj)
		throw std::runtime_error("RebalanceStacks: invalid army object " + std::to_string(pack.dstArmy.getNum()) + ", possible game state corruption.");

	StackLocation src(srcObj->id, pack.srcSlot);
	StackLocation dst(dstObj->id, pack.dstSlot);

	[[maybe_unused]] const CCreature * srcType = srcObj->getCreature(src.slot);
	const CCreature * dstType = dstObj->getCreature(dst.slot);
	TQuantity srcCount = srcObj->getStackCount(src.slot);

	if(srcCount == pack.count) //moving whole stack
	{
		if(dstType) //stack at dest -> merge
		{
			assert(dstType == srcType);
			const auto srcHero = dynamic_cast<CGHeroInstance*>(srcObj);
			const auto dstHero = dynamic_cast<CGHeroInstance*>(dstObj);
			auto srcStack = srcObj->getStackPtr(src.slot);
			auto dstStack = dstObj->getStackPtr(dst.slot);
			if(srcStack->getArt(ArtifactPosition::CREATURE_SLOT))
			{
				if(auto dstArt = dstStack->getArt(ArtifactPosition::CREATURE_SLOT))
				{
					bool artifactIsLost = true;

					if(srcHero)
					{
						auto dstSlot = ArtifactUtils::getArtBackpackPosition(srcHero, dstArt->getTypeId());
						if (dstSlot != ArtifactPosition::PRE_FIRST)
						{
							gs.getMap().moveArtifactInstance(*dstStack, ArtifactPosition::CREATURE_SLOT, *srcHero, dstSlot);
							artifactIsLost = false;
						}
					}

					if (artifactIsLost)
					{
						BulkEraseArtifacts ea;
						ea.artHolder = dstHero->id;
						ea.posPack.emplace_back(ArtifactPosition::CREATURE_SLOT);
						ea.creature = dst.slot;
						ea.visit(*this);
						logNetwork->warn("Cannot move artifact! No free slots");
					}
					gs.getMap().moveArtifactInstance(*srcStack, ArtifactPosition::CREATURE_SLOT, *dstStack, ArtifactPosition::CREATURE_SLOT);
					//TODO: choose from dialog
				}
				else //just move to the other slot before stack gets erased
				{
					gs.getMap().moveArtifactInstance(*srcStack, ArtifactPosition::CREATURE_SLOT, *dstStack, ArtifactPosition::CREATURE_SLOT);
				}
			}

			auto movedStack = srcObj->detachStack(src.slot);
			dstObj->joinStack(dst.slot, std::move(movedStack));
		}
		else
		{
			auto movedStack = srcObj->detachStack(src.slot);
			dstObj->putStack(dst.slot, std::move(movedStack));
		}
	}
	else
	{
		auto movedStack = srcObj->splitStack(src.slot, pack.count);
		if(dstType) //stack at dest -> rebalance
		{
			assert(dstType == srcType);
			dstObj->joinStack(dst.slot, std::move(movedStack));
		}
		else //move new stack to an empty slot
		{
			dstObj->putStack(dst.slot, std::move(movedStack));
		}
	}

	srcObj->nodeHasChanged();
	if (srcObj != dstObj)
		dstObj->nodeHasChanged();
}

void GameStatePackVisitor::visitBulkRebalanceStacks(BulkRebalanceStacks & pack)
{
	for(auto & move : pack.moves)
		move.visit(*this);
}

void GameStatePackVisitor::visitGrowUpArtifact(GrowUpArtifact & pack)
{
	spellPointBonusGraphChanged = true;
	auto artInst = gs.getArtInstance(pack.id);
	assert(artInst);
	artInst->growingUp();
}

void GameStatePackVisitor::visitPutArtifact(PutArtifact & pack)
{
	spellPointBonusGraphChanged = true;
	auto art = gs.getArtInstance(pack.id);
	assert(!art->getParentNodes().empty());
	auto hero = gs.getHero(pack.al.artHolder);
	assert(hero);
	assert(art);
	assert(art->canBePutAt(hero, pack.al.slot));
	assert(ArtifactUtils::checkIfSlotValid(*hero, pack.al.slot));
	gs.getMap().putArtifactInstance(*hero, art->getId(), pack.al.slot);
	updateMoraleOnArtifactChange(pack.al.artHolder);
}

void GameStatePackVisitor::visitBulkEraseArtifacts(BulkEraseArtifacts & pack)
{
	spellPointBonusGraphChanged = true;
	const auto artSet = gs.getArtSet(pack.artHolder);
	assert(artSet);

	std::sort(pack.posPack.begin(), pack.posPack.end(), [](const ArtifactPosition & slot0, const ArtifactPosition & slot1) -> bool
	{
		return slot0.num > slot1.num;
	});

	for(const auto & slot : pack.posPack)
	{
		const auto slotInfo = artSet->getSlot(slot);
		const ArtifactInstanceID artifactID = slotInfo->artifactID;
		const CArtifactInstance * artifact = gs.getArtInstance(artifactID);
		if(slotInfo->locked)
		{
			logGlobal->debug("Erasing locked artifact: %s", artifact->getType()->getNameTranslated());
			DisassembledArtifact dis;
			dis.al.artHolder = pack.artHolder;

			for(auto & slotInfoWorn : artSet->artifactsWorn)
			{
				auto art = slotInfoWorn.second.getArt();
				if(art->isCombined() && art->isPart(artifact))
				{
					dis.al.slot = artSet->getArtPos(art);
					break;
				}
			}
			assert((dis.al.slot != ArtifactPosition::PRE_FIRST) && "Failed to determine the assembly this locked artifact belongs to");
			logGlobal->debug("Found the corresponding assembly: %s", artSet->getArt(dis.al.slot)->getType()->getNameTranslated());
			dis.visit(*this);
		}
		else
		{
			logGlobal->debug("Erasing artifact %s", artifact->getType()->getNameTranslated());
		}
		gs.getMap().removeArtifactInstance(*artSet, slot);
	}
	updateMoraleOnArtifactChange(pack.artHolder);
}

void GameStatePackVisitor::visitBulkMoveArtifacts(BulkMoveArtifacts & pack)
{
	spellPointBonusGraphChanged = true;
	const auto bulkArtsRemove = [this](std::vector<MoveArtifactInfo> & artsPack, CArtifactSet & artSet)
	{
		std::vector<ArtifactPosition> packToRemove;
		for(const auto & slotsPair : artsPack)
			packToRemove.push_back(slotsPair.srcPos);
		std::sort(packToRemove.begin(), packToRemove.end(), [](const ArtifactPosition & slot0, const ArtifactPosition & slot1) -> bool
		{
			return slot0.num > slot1.num;
		});

		for(const auto & slot : packToRemove)
			gs.getMap().removeArtifactInstance(artSet, slot);
	};

	const auto bulkArtsPut = [this](std::vector<MoveArtifactInfo> & artsPack, CArtifactSet & initArtSet, CArtifactSet & dstArtSet)
	{
		for(const auto & slotsPair : artsPack)
		{
			auto * art = initArtSet.getArt(slotsPair.srcPos);
			assert(art);
			gs.getMap().putArtifactInstance(dstArtSet, art->getId(), slotsPair.dstPos);
		}
	};

	auto * leftSet = gs.getArtSet(ArtifactLocation(pack.srcArtHolder, pack.srcCreature));
	assert(leftSet);
	auto * rightSet = gs.getArtSet(ArtifactLocation(pack.dstArtHolder, pack.dstCreature));
	assert(rightSet);
	CArtifactFittingSet artInitialSetLeft(*leftSet);
	bulkArtsRemove(pack.artsPack0, *leftSet);
	if(!pack.artsPack1.empty())
	{
		CArtifactFittingSet artInitialSetRight(*rightSet);
		bulkArtsRemove(pack.artsPack1, *rightSet);
		bulkArtsPut(pack.artsPack1, artInitialSetRight, *leftSet);
	}
	bulkArtsPut(pack.artsPack0, artInitialSetLeft, *rightSet);

	updateMoraleOnArtifactChange(pack.srcArtHolder);
	updateMoraleOnArtifactChange(pack.dstArtHolder);
}

void GameStatePackVisitor::visitDischargeArtifact(DischargeArtifact & pack)
{
	spellPointBonusGraphChanged = true;
	auto artInst = gs.getArtInstance(pack.id);
	assert(artInst);
	artInst->discharge(pack.charges);
	if(artInst->getType()->getRemoveOnDepletion() && artInst->getCharges() == 0 && pack.artLoc.has_value())
	{
		BulkEraseArtifacts ePack;
		ePack.artHolder = pack.artLoc.value().artHolder;
		ePack.creature = pack.artLoc.value().creature;
		ePack.posPack.push_back(pack.artLoc.value().slot);
		ePack.visit(*this);
	}
	// Workaround to inform hero bonus node about changes. Obviously this has to be done somehow differently.
	if(pack.artLoc.has_value())
		gs.getHero(pack.artLoc.value().artHolder)->nodeHasChanged();
}

void GameStatePackVisitor::visitAssembledArtifact(AssembledArtifact & pack)
{
	spellPointBonusGraphChanged = true;
	auto artSet = gs.getArtSet(pack.al.artHolder);
	assert(artSet);
	const auto transformedArt = artSet->getArt(pack.al.slot);
	assert(transformedArt);
	const auto builtArt = pack.artId.toArtifact();
	assert(vstd::contains_if(ArtifactUtils::assemblyPossibilities(artSet, transformedArt->getTypeId()), [=](const CArtifact * art)->bool
	{
		return art->getId() == builtArt->getId();
	}));

	auto * combinedArt = gs.getMap().createArtifactComponent(pack.artId);

	// Find slots for all involved artifacts
	std::set<ArtifactPosition, std::greater<>> slotsInvolved = { pack.al.slot };
	CArtifactFittingSet fittingSet(*artSet);
	auto parts = builtArt->getConstituents();
	parts.erase(std::find(parts.begin(), parts.end(), transformedArt->getType()));
	for(const auto constituent : parts)
	{
		const auto slot = fittingSet.getArtPos(constituent->getId(), false, false);
		fittingSet.lockSlot(slot);
		assert(slot != ArtifactPosition::PRE_FIRST);
		slotsInvolved.insert(slot);
	}

	// Find a slot for combined artifact
	if(ArtifactUtils::isSlotEquipment(pack.al.slot) && ArtifactUtils::isSlotBackpack(*slotsInvolved.begin()))
	{
		pack.al.slot = ArtifactPosition::BACKPACK_START;
	}
	else if(ArtifactUtils::isSlotBackpack(pack.al.slot))
	{
		for(const auto & slot : slotsInvolved)
			if(ArtifactUtils::isSlotBackpack(slot))
				pack.al.slot = slot;
	}
	else
	{
		for(const auto & slot : slotsInvolved)
			if(!vstd::contains(builtArt->getPossibleSlots().at(artSet->bearerType()), pack.al.slot)
			   && vstd::contains(builtArt->getPossibleSlots().at(artSet->bearerType()), slot))
			{
				pack.al.slot = slot;
				break;
			}
	}

	// Delete parts from hero
	for(const auto & slot : slotsInvolved)
	{
		const auto constituentInstance = artSet->getArt(slot);
		gs.getMap().removeArtifactInstance(*artSet, slot);

		if(!combinedArt->getType()->isFused())
		{
			if(ArtifactUtils::isSlotEquipment(pack.al.slot) && slot != pack.al.slot)
				combinedArt->addPart(constituentInstance, slot);
			else
				combinedArt->addPart(constituentInstance, ArtifactPosition::PRE_FIRST);
		}
	}

	// Put new combined artifacts
	gs.getMap().putArtifactInstance(*artSet, combinedArt->getId(), pack.al.slot);
	updateMoraleOnArtifactChange(pack.al.artHolder);
}

void GameStatePackVisitor::visitDisassembledArtifact(DisassembledArtifact & pack)
{
	spellPointBonusGraphChanged = true;
	auto hero = gs.getHero(pack.al.artHolder);
	assert(hero);
	auto disassembledArtID = hero->getArtID(pack.al.slot);
	auto disassembledArt = gs.getArtInstance(disassembledArtID);
	assert(disassembledArt);

	const auto parts = disassembledArt->getPartsInfo();
	gs.getMap().removeArtifactInstance(*hero, pack.al.slot);
	for(auto & part : parts)
	{
		// ArtifactPosition::PRE_FIRST is value of main part slot -> it'll replace combined artifact in its pos
		auto slot = (ArtifactUtils::isSlotEquipment(part.slot) ? part.slot : pack.al.slot);
		disassembledArt->detachFromSource(*part.getArtifact());
		gs.getMap().putArtifactInstance(*hero, part.getArtifact()->getId(), slot);
	}
	gs.getMap().eraseArtifactInstance(disassembledArt->getId());
	updateMoraleOnArtifactChange(pack.al.artHolder);
}

void GameStatePackVisitor::visitHeroVisit(HeroVisit & pack)
{
}

void GameStatePackVisitor::visitSetAvailableArtifacts(SetAvailableArtifacts & pack)
{
	if(pack.id != ObjectInstanceID::NONE)
	{
		if(auto * bm = dynamic_cast<CGBlackMarket *>(gs.getObjInstance(pack.id)))
		{
			bm->artifacts = pack.arts;
		}
		else
		{
			logNetwork->error("Wrong black market id!");
		}
	}
	else
	{
		gs.getMap().townMerchantArtifacts = pack.arts;
	}
}

void GameStatePackVisitor::visitSetHouseOfWisdomScrolls(SetHouseOfWisdomScrolls & pack)
{
	if(auto * town = gs.getTown(pack.townId))
		town->setHouseOfWisdomScrolls(pack.scrolls);
	else
		logNetwork->error("Wrong House of Wisdom town id!");
}

void GameStatePackVisitor::visitSetNewHorizonsAdventureSpellUnlock(SetNewHorizonsAdventureSpellUnlock & pack)
{
	if(pack.guildLevel < 1 || pack.guildLevel > 5)
	{
		logNetwork->error("Invalid New Horizons Adventure Spell Guild tier %d", pack.guildLevel);
		return;
	}

	if(auto * town = gs.getTown(pack.townId))
		town->setNewHorizonsAdventureSpellUnlocked(pack.guildLevel);
	else
		logNetwork->error("Wrong New Horizons Adventure Spell town id!");
}

void GameStatePackVisitor::visitNewTurn(NewTurn & pack)
{
	static constexpr int32_t goldPerInvestorStep = 50;
	static constexpr int32_t maximumInvestorDailyGold = 250;
	for(const auto & [heroID, investorDailyGold] : pack.newHorizonsInvestorDailyGold)
	{
		if(investorDailyGold < 0 || investorDailyGold > maximumInvestorDailyGold
			|| investorDailyGold % goldPerInvestorStep != 0)
		{
			logNetwork->error("Invalid New Horizons Investor snapshot for hero %d", heroID.getNum());
			return;
		}
		if(!gs.getHero(heroID))
		{
			logNetwork->error("Wrong New Horizons Investor hero id %d", heroID.getNum());
			return;
		}
	}

	spellPointBonusGraphChanged = true;
	gs.day = pack.day;
	gs.nextAstrologyWeek = pack.nextAstrologyWeek;
	for(const auto & [heroID, investorDailyGold] : pack.newHorizonsInvestorDailyGold)
		gs.getHero(heroID)->setNewHorizonsInvestorDailyGold(investorDailyGold);
	if(newHorizonsMagic::adventureSpellRulesActive(gs.getMagicRules()))
	{
		for(auto * hero : gs.getMap().getObjects<CGHeroInstance>())
			hero->resetNewHorizonsAdventureSpellCastToday();
	}

	// Troop-mixing bonuses (e.g. Temple of Loyalty) may expire now, so army morale of their owners must be recomputed afterwards
	std::vector<CArmedInstance *> troopMixingArmies;
	for(auto * army : gs.getMap().getObjects<CArmedInstance>())
		if(army->hasBonusOfType(BonusType::ALIGNMENT_MIX) || army->hasBonusOfType(BonusType::NONEVIL_ALIGNMENT_MIX))
			troopMixingArmies.push_back(army);

	// Update bonuses before doing anything else so hero don't get more MP than needed
	gs.globalEffects.removeBonusesRecursive(Bonus::OneDay); //works for children -> all game objs
	gs.globalEffects.reduceBonusDurations(Bonus::NDays);
	gs.globalEffects.reduceBonusDurations(Bonus::OneWeek);
	//TODO not really a single root hierarchy, what about bonuses placed elsewhere? [not an issue with H3 mechanics but in the future...]

	for(auto * army : troopMixingArmies)
		army->updateMoraleBonusFromArmy();

	for(auto & manaPack : pack.heroesMana)
		manaPack.visit(*this);

	for(auto & movePack : pack.heroesMovement)
		movePack.visit(*this);

	gs.heroesPool->onNewDay(pack.day > 1);

	for(auto & entry : pack.playerIncome)
	{
		gs.getPlayerState(entry.first)->resources += entry.second;
		gs.getPlayerState(entry.first)->resources.amin(GameConstants::PLAYER_RESOURCES_CAP);
	}

	// New Horizons Mystic Pond picks are authored by the server as part of the
	// week-start packet.  Apply them to the town before any client opens its
	// building dialog; the town field is serialized with the rest of gamestate.
	for(const auto & [townID, resources] : pack.newHorizonsMysticPondResults)
	{
		if(auto * town = gs.getTown(townID))
			town->newHorizonsMysticPondResources = resources;
	}

	for(auto & creatureSet : pack.availableCreatures) //set available creatures in towns
		creatureSet.visit(*this);

	for (const auto & townID : gs.getMap().getAllTowns())
	{
		auto t = gs.getTown(townID);
		t->built = 0;
		t->spellResearchCounterDay = 0;
	}

	if(pack.newRumor)
		gs.currentRumor = *pack.newRumor;
}

void GameStatePackVisitor::visitSetScriptVariable(SetScriptVariable & pack)
{
	gs.getMap().getScriptVariables().set(pack.scope, pack.name, pack.value);
}

void GameStatePackVisitor::visitSetQuestHint(SetQuestHint & pack)
{
	auto * questSource = dynamic_cast<QuestSource *>(gs.getObjInstance(pack.object));
	if(!questSource || !questSource->getActiveQuest())
		throw std::runtime_error("SetQuestHint: object is not a quest source!");

	questSource->getQuest().scriptHintText = pack.hint;
}

void GameStatePackVisitor::visitSetObjectProperty(SetObjectProperty & pack)
{
	// A weekly use marker does not alter the bonus graph or Mana capacity.
	if(pack.what != ObjProperty::NEW_HORIZONS_LAND_SURVEYOR_LAST_WEEK)
		spellPointBonusGraphChanged = true;
	CGObjectInstance *obj = gs.getObjInstance(pack.id);
	if(!obj)
	{
		logNetwork->error("Wrong object ID - property cannot be set!");
		return;
	}

	if(pack.what == ObjProperty::OWNER && obj->asOwnable())
	{
		PlayerColor oldOwner = obj->getOwner();
		PlayerColor newOwner = pack.identifier.as<PlayerColor>();
		if(oldOwner.isValidPlayer())
			gs.getPlayerState(oldOwner)->removeOwnedObject(obj);

		if(newOwner.isValidPlayer())
			gs.getPlayerState(newOwner)->addOwnedObject(obj);
	}

	if(pack.what == ObjProperty::OWNER)
	{
		if(obj->ID == Obj::TOWN)
		{
			auto * t = dynamic_cast<CGTownInstance *>(obj);
			assert(t);

			PlayerColor oldOwner = t->tempOwner;
			if(oldOwner.isValidPlayer())
			{
				auto * state = gs.getPlayerState(oldOwner);
				if(state->getTowns().empty())
					state->daysWithoutCastle = 0;
			}
			if(pack.identifier.as<PlayerColor>().isValidPlayer())
			{
				//reset counter before NewTurn to avoid no town message if game loaded at turn when one already captured
				PlayerState * p = gs.getPlayerState(pack.identifier.as<PlayerColor>());
				if(p->daysWithoutCastle)
					p->daysWithoutCastle = std::nullopt;
			}
		}

		obj->detachFromBonusSystem(gs);
		obj->setProperty(pack.what, pack.identifier);
		obj->attachToBonusSystem(gs);
	}
	else //not an armed instance
	{
		obj->setProperty(pack.what, pack.identifier);
	}
}

void GameStatePackVisitor::visitHeroMasteryOffer(HeroMasteryOffer & pack)
{
	auto * hero = gs.getHero(pack.offer.hero);
	if(!hero)
		throw std::runtime_error("Mastery offer for a missing hero");
	hero->applyMasteryOffer(pack.offer);
}

void GameStatePackVisitor::visitHeroMasteryChosen(HeroMasteryChosen & pack)
{
	auto * hero = gs.getHero(pack.hero);
	if(!hero)
		throw std::runtime_error("Mastery choice for a missing hero");
	hero->applyMasteryChoice(pack.sequence, pack.choice);
}

void GameStatePackVisitor::visitHeroLevelUp(HeroLevelUp & pack)
{
	auto * hero = gs.getHero(pack.heroId);
	assert(hero);
	if(!pack.perks.empty() || pack.perkOfferSeed != 0)
	{
		if(pack.perkOfferSeed < 0 || !newHorizonsHeroes::usesPerkRules(hero->getPerkState().rules))
			throw std::runtime_error("Invalid New Horizons perk offer envelope");
		const size_t maxSkills = static_cast<size_t>(hero->getPerkState().rules["maxSkillChoices"].Integer());
		const size_t maxPerks = static_cast<size_t>(hero->getPerkState().rules["maxPerkChoices"].Integer());
		if(pack.skills.size() > maxSkills || pack.perks.size() > maxPerks)
			throw std::runtime_error("Oversized New Horizons level-up offer");
		const auto expected = hero->getPerkState().prepareOffer([hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		}, static_cast<uint64_t>(pack.perkOfferSeed));
		if(expected != pack.perks)
			throw std::runtime_error("Forged New Horizons level-up perk offer");
	}
	hero->captureMasteryEligibility(hero->level + 1, pack.artilleryExpertBeforeGain, pack.logisticsExpertBeforeGain);
	hero->levelUp(pack.primaryGains);
}

void GameStatePackVisitor::visitHeroPerkChosen(HeroPerkChosen & pack)
{
	auto * hero = gs.getHero(pack.hero);
	if(!hero)
		throw std::runtime_error("Perk choice for a missing hero");
	hero->applyPerkSelection(pack.selection);
}

void GameStatePackVisitor::visitCommanderLevelUp(CommanderLevelUp & pack)
{
	auto * hero = gs.getHero(pack.heroId);
	assert(hero);
	const auto & commander = hero->getCommander();
	assert(commander);
	commander->levelUp();
}

void GameStatePackVisitor::visitBattleStart(BattleStart & pack)
{
	spellPointBonusGraphChanged = true;
	if(!pack.info)
		throw std::runtime_error("Missing BattleStart state");
	// Internal connections can deliver packets without binary deserialization,
	// so validate both sides before localInit attaches armies or either hero is
	// mutated from its saved pool snapshot.
	validateBattleSpellPointSnapshots(*pack.info);
	// Validate before localInit attaches armies or changes the canonical battle.
	heroCommands::validateRules(pack.info->getHeroCommandRules());
	pack.info->normalizeLegacyHeroCommandState();
	pack.info->validateFocusFireStates();
	const auto & deploymentState = pack.info->getDeploymentState();
	deploymentState.validateShape();
	if(deploymentState.independent)
	{
		const auto activeSide = deploymentState.activeSide();
		const auto expectedDistance = deploymentState.activeDistance();
		if(pack.info->tacticsSide != activeSide || pack.info->tacticDistance != expectedDistance)
			throw std::runtime_error("Independent BattleStart deployment state has an invalid tactics projection");
	}
	// BattleStart may arrive in-process without passing through binary decoding,
	// so reject malformed continuation references before unit initialization too.
	pack.info->validateDoubleCommandStructure();
	pack.info->validatePreCombatOrderStructure();
	assert(pack.battleID == gs.nextBattleID);

	pack.info->battleID = gs.nextBattleID;
	pack.info->localInit();
	// The stack descriptors omit CUnitState. Only now are alive/ghost/controller
	// checks meaningful for a continuation received in this BattleStart packet.
	pack.info->validateDoubleCommandContexts();
	pack.info->validatePreCombatOrderContexts();

	if (pack.info->getDefendedTown() && pack.info->getSide(BattleSide::DEFENDER).heroID.hasValue())
	{
		CGTownInstance * town = gs.getTown(pack.info->townID);
		CGHeroInstance * hero = gs.getHero(pack.info->getSideHero(BattleSide::DEFENDER)->id);

		if (hero)
		{
			hero->detachFrom(town->townAndVis);
			hero->attachTo(*town);
			grantTownDefendingHeroBonuses(*town, *hero);
		}
	}

	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if (pack.info->getSide(i).heroID.hasValue())
		{
			CGHeroInstance * hero = gs.getHero(pack.info->getSideHero(i)->id);
			auto & side = pack.info->getSide(i);
			if(newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
			{
				hero->restoreSpellPointSnapshot(side.initialNormalSpellPoints, side.initialBufferSpellPoints);
				if(side.temporaryBufferRemaining > 0 && !hero->grantBufferSpellPoints(side.temporaryBufferRemaining))
					throw std::runtime_error("Failed to grant temporary combat Spell Points");
				if(side.additionalMana < 0)
				{
					// Preserve signed combat-mana penalties, consuming available
					// energy Buffer-first just like any other Mana drain.
					const int64_t drain = std::min(hero->getManaAvailable(), -static_cast<int64_t>(side.additionalMana));
					if(!hero->spendSpellPoints(drain))
						throw std::runtime_error("Failed to apply combat Spell Point penalty");
				}
			}
			else
			{
				const auto initial = std::clamp<int64_t>(static_cast<int64_t>(side.initialMana) + side.additionalMana,
					0, std::numeric_limits<int32_t>::max());
				hero->setNormalSpellPoints(static_cast<int32_t>(initial));
			}
		}
	}

	gs.currentBattles.push_back(std::move(pack.info));
	gs.nextBattleID = BattleID(gs.nextBattleID.getNum() + 1);
}

void GameStatePackVisitor::visitBattleNextRound(BattleNextRound & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("BattleNextRound references a missing battle");
	for(const auto sideID : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		applyFormulaReserveClosureReward(*battle, sideID);
		applySpellBufferRoundExpiryReward(*battle, sideID);
	}
	battle->nextRound();
}

void GameStatePackVisitor::visitBattleDeploymentPhaseChanged(BattleDeploymentPhaseChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Deployment phase update references a missing battle");
	const auto & previous = battle->getDeploymentState();
	if(!previous.independent || !pack.state.independent)
		throw std::runtime_error("Deployment phase update does not target an independent deployment");
	pack.state.validateTransitionFrom(previous);
	battle->setDeploymentState(pack.state);
}

void GameStatePackVisitor::visitBattleSetActiveStack(BattleSetActiveStack & pack)
{
	gs.getBattle(pack.battleID)->nextTurn(pack.stack, pack.reason);
}

void GameStatePackVisitor::visitBattleTriggerEffect(BattleTriggerEffect & pack)
{
	CStack * st = gs.getBattle(pack.battleID)->getStack(pack.stackID);
	assert(st);
	switch(pack.effect)
	{
		case BonusType::HP_REGENERATION:
		{
			int64_t toHeal = pack.val;
			st->heal(toHeal, EHealLevel::HEAL, EHealPower::PERMANENT);
			break;
		}
		case BonusType::MANA_DRAIN:
		{
			CGHeroInstance * h = gs.getHero(ObjectInstanceID(pack.additionalInfo));
			const int32_t bufferBefore = h->getBufferSpellPoints();
			if(!h->spendSpellPoints(pack.val))
				throw std::runtime_error("Invalid authoritative Mana Drain amount");
			st->drainedMana = true;
			if(auto * side = findBattleSide(gs, h->id))
			{
				const int32_t spentFromBuffer = bufferBefore - h->getBufferSpellPoints();
				side->temporaryBufferRemaining = std::max<int32_t>(0,
					side->temporaryBufferRemaining - std::min(side->temporaryBufferRemaining, spentFromBuffer));
			}
			break;
		}
		case BonusType::POISON:
		{
			auto b = st->getLocalBonus(Selector::source(BonusSource::SPELL_EFFECT, SpellID(SpellID::POISON))
										   .And(Selector::type()(BonusType::STACK_HEALTH)));
			if (b)
				b->val = pack.val;
			break;
		}
		case BonusType::ENCHANTER:
		case BonusType::MORALE:
			break;
		case BonusType::FEARFUL:
			st->fear = true;
			break;
		default:
			logNetwork->error("Unrecognized trigger effect type %d", static_cast<int>(pack.effect));
	}
}

void GameStatePackVisitor::visitBattleUpdateGateState(BattleUpdateGateState & pack)
{
	if(gs.getBattle(pack.battleID))
		gs.getBattle(pack.battleID)->si.gateState = pack.state;
}

void GameStatePackVisitor::visitBattleResultAccepted(BattleResultAccepted & pack)
{
	spellPointBonusGraphChanged = true;
	// Remove any "until next battle" bonuses
	if(const auto attackerHero = gs.getHero(pack.heroResult[BattleSide::ATTACKER].heroID))
		attackerHero->removeBonusesRecursive(Bonus::OneBattle);
	if(const auto defenderHero = gs.getHero(pack.heroResult[BattleSide::DEFENDER].heroID))
		defenderHero->removeBonusesRecursive(Bonus::OneBattle);
}

void GameStatePackVisitor::visitBattleStackMoved(BattleStackMoved & pack)
{
	BattleStatePackVisitor battleVisitor(*gs.getBattle(pack.battleID));
	pack.visitTyped(battleVisitor);
}

void GameStatePackVisitor::visitBattleAttack(BattleAttack & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("BattleAttack references a missing battle");
	pack.validatePerfectFortuneMarker();
	if(pack.perfectFortuneState)
	{
		const auto * strikeSource = battle->getStack(pack.stackAttacking, false);
		if(!strikeSource || !battle->getPerfectFortuneState(pack.perfectFortuneSide).available()
			|| battle->playerToSide(battle->battleGetActionController(strikeSource)) != pack.perfectFortuneSide
			|| !battle->battleCanUsePerfectFortune(strikeSource, nullptr, pack.shot()))
			throw std::runtime_error("Invalid Perfect Fortune strike transition");
	}
	if(pack.chainGateTriggered && !chainGateKillQualifies(*battle, pack.stackAttacking, pack.bsa))
		throw std::runtime_error("Invalid Chain Gate attack trigger");
	const auto bloodrageCandidates = bloodrageDeathCandidates(*battle, pack.bsa);
	CStack * attacker = battle->getStack(pack.stackAttacking);
	assert(attacker);
	std::array<bool, 2> lastStandSidesToConsume{};
	bool lastStandEndsActiveActivation = false;
	for(const BattleStackAttacked & hit : pack.bsa)
	{
		const bool endedActivation = hit.newState.data["state"]["armorerLastStandEndedActivation"].Bool();
		const bool passiveDefend = hit.newState.data["state"]["armorerLastStandDefending"].Bool();
		if(hit.armorerLastStandEndsActivation
			&& (hit.armorerLastStandSide == BattleSide::NONE || !endedActivation || !pack.lastStandRetaliation()
				|| battle->activeStack != static_cast<int32_t>(hit.stackAttacked)))
			throw std::runtime_error("Last Stand may end only the active stack after a retaliation");
		lastStandEndsActiveActivation = lastStandEndsActiveActivation || hit.armorerLastStandEndsActivation;
		if(hit.armorerLastStandSide == BattleSide::NONE)
			continue;
		if(hit.armorerLastStandSide != BattleSide::ATTACKER && hit.armorerLastStandSide != BattleSide::DEFENDER)
			throw std::runtime_error("Invalid Last Stand side in attack packet");
		const auto sideIndex = static_cast<size_t>(hit.armorerLastStandSide);
		const auto * target = battle->getStack(hit.stackAttacked, false);
		const auto * strikeSource = battle->getStack(hit.attackerID, false);
		const auto * hero = battle->battleGetFightingHero(hit.armorerLastStandSide);
		const bool physicalDamage = strikeSource && !pack.spellLike()
			&& !(pack.shot() && strikeSource->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK));
		if(lastStandSidesToConsume[sideIndex] || battle->armorerLastStandUsed(hit.armorerLastStandSide)
			|| !target || !target->alive() || hit.damageAmount < 0 || hit.killed() || hit.willRebirth()
			|| hit.cloneKilled() || !passiveDefend
			|| hit.attackerID != pack.stackAttacking || !strikeSource
			|| !newHorizonsArmorer::isEligiblePhysicalAttack(strikeSource, physicalDamage, pack.spellLike())
			|| battle->playerToSide(battle->battleGetOwner(target)) != hit.armorerLastStandSide
			|| !newHorizonsArmorer::canTriggerLastStand(hero, target))
			throw std::runtime_error("Invalid Last Stand attack trigger");
		if(hit.newState.id != hit.stackAttacked
			|| hit.newState.operation != UnitChanges::EOperation::UPDATE)
			throw std::runtime_error("Last Stand attack update does not identify its surviving stack");
		auto projectedState = target->acquireState();
		projectedState->load(hit.newState.data);
		if(projectedState->unitId() != hit.stackAttacked || !projectedState->alive()
			|| projectedState->getAvailableHealth() != 1 || projectedState->getCount() != 1
			|| !projectedState->armorerLastStandDefending)
			throw std::runtime_error("Last Stand attack snapshot does not preserve one creature at 1 HP");
		lastStandSidesToConsume[sideIndex] = true;
	}
	if(pack.lastStandRetaliation() != lastStandEndsActiveActivation)
		throw std::runtime_error("Last Stand retaliation marker does not match its activation-ending hit");
	if(pack.relentlessAssaultState)
	{
		pack.relentlessAssaultState->validateShape();
		if((pack.relentlessAssaultSide != BattleSide::ATTACKER && pack.relentlessAssaultSide != BattleSide::DEFENDER)
			|| !attacker)
			throw std::runtime_error("Invalid Relentless Assault attack state side or attacker");
		const auto * hero = battle->battleGetFightingHero(pack.relentlessAssaultSide);
		if((pack.relentlessAssaultSide != BattleSide::ATTACKER && pack.relentlessAssaultSide != BattleSide::DEFENDER)
			|| battle->playerToSide(battle->battleGetOwner(attacker)) != pack.relentlessAssaultSide
			|| !hero || !hero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT)
			|| attacker->isGhost() || attacker->isTurret()
			|| attacker->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| attacker->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
			throw std::runtime_error("Invalid Relentless Assault attack state update");
		const auto targetId = pack.relentlessAssaultState->activationTargetUnitId;
		const auto * target = targetId == RelentlessAssaultState::INVALID_TARGET
			? nullptr : battle->battleGetUnitByID(targetId);
		if(!pack.relentlessAssaultState->activationInitialized
			|| !pack.relentlessAssaultState->activationHadEligibleAttack
			|| targetId >= battle->nextUnitId()
			|| pack.bsa.empty() || pack.bsa.front().stackAttacked != targetId
			|| (target && target->unitSide() == pack.relentlessAssaultSide))
			throw std::runtime_error("Invalid Relentless Assault primary target update");
	}
	if(pack.fortuneState)
		battle->getSide(pack.fortuneSide).sylvanLuck = *pack.fortuneState;
	if(pack.perfectFortuneState)
		battle->setPerfectFortuneState(pack.perfectFortuneSide, *pack.perfectFortuneState);

	pack.attackerChanges.visit(*this);

	for(BattleStackAttacked & stack : pack.bsa)
	{
		battle->updateUnit(stack.newState.id, stack.newState.data, stack.newState.healthDelta);
		removeExhaustedGuardianSpirit(*battle, stack);
	}
	for(const BattleStackAttacked & hit : pack.bsa)
	{
		if(hit.armorerLastStandSide == BattleSide::NONE)
			continue;
		const auto * survivor = battle->getStack(hit.stackAttacked, false);
		if(!survivor || !survivor->alive() || survivor->getAvailableHealth() != 1
			|| survivor->getCount() != 1)
			throw std::runtime_error("Last Stand attack update did not preserve one creature at 1 HP");
	}
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(lastStandSidesToConsume[static_cast<size_t>(side)])
			battle->consumeArmorerLastStand(side);
	if(pack.relentlessAssaultState)
		battle->setRelentlessAssaultState(pack.relentlessAssaultSide, *pack.relentlessAssaultState);
	recordBloodrageDeaths(*battle, bloodrageCandidates);
	refreshBloodrageLivingUnits(*battle, pack.bsa);
	if(pack.chainGateTriggered)
		battle->armChainGate(battle->gatedDemonicStackSide(pack.stackAttacking));

	if(!attacker->isTimeStopped())
		attacker->removeBonusesRecursive(Bonus::UntilAttack);

	if(!pack.counter() && !attacker->isTimeStopped())
		attacker->removeBonusesRecursive(Bonus::UntilOwnAttack);
}

void GameStatePackVisitor::visitEndAction(EndAction & pack)
{
	if(pack.endsFortuneActivation)
	{
		auto * battle = gs.getBattle(pack.battleID);
		if(!battle)
			throw std::runtime_error("EndAction references a missing battle");
		if(auto * activeStack = battle->getStack(battle->activeStack, false))
			activeStack->setActivationMovementBonus(0);
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			battle->getSide(side).sylvanLuck.endActivation();
	}
}

void GameStatePackVisitor::visitStartAction(StartAction & pack)
{
	const bool targeted = pack.ba.actionType == EActionType::HERO_COMMAND
		&& pack.ba.command == HeroCommand::FOCUS_FIRE;
	const auto * battleContext = gs.getBattle(pack.battleID);
	if(!battleContext)
		throw std::runtime_error(targeted ? "Missing targeted StartAction battle context" : "Missing StartAction battle context");
	if(pack.ba.metamagicDecline)
		throw std::runtime_error("Retired Metamagic decline StartAction");
	if(pack.preCombatOrderState
		&& pack.ba.side != BattleSide::ATTACKER && pack.ba.side != BattleSide::DEFENDER)
		throw std::runtime_error("Battle Plan StartAction has an invalid side");
	const bool canonicalOrder = pack.ba.actionType == EActionType::HERO_COMMAND
		&& heroCommands::isCanonicalRules(battleContext->getHeroCommandRules());
	std::optional<DoubleCommandState> acceptedDoubleCommandState;
	std::optional<PreCombatOrderState> acceptedPreCombatOrderState;
	if(pack.ba.side == BattleSide::ATTACKER || pack.ba.side == BattleSide::DEFENDER)
	{
		if(battleContext->getRound() == 1 && !battleContext->battleTacticDist()
			&& battleContext->getActivationSerial() == 0)
		{
			std::optional<BattleSide> pendingSide;
			bool availableOpening = false;
			for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			{
				const auto & state = battleContext->getPreCombatOrderState(side);
				availableOpening = availableOpening || state.phase == PreCombatOrderState::Phase::AVAILABLE;
				if(state.orderPending())
				{
					if(pendingSide || !battleContext->battleHasPendingPreCombatOrder(side))
						throw std::runtime_error("Battle Plan opening state has no valid active anchor");
					pendingSide = side;
				}
			}
			if((pendingSide && (*pendingSide != pack.ba.side || pack.ba.actionType != EActionType::HERO_COMMAND))
				|| (!pendingSide && availableOpening))
				throw std::runtime_error("Battle Plan opening Orders must resolve before creature actions");
		}
		const auto & opening = battleContext->getPreCombatOrderState(pack.ba.side);
		if(opening.orderPending())
		{
			if(!canonicalOrder || !battleContext->battleHasPendingPreCombatOrder(pack.ba.side))
				throw std::runtime_error("StartAction cannot interrupt a required Battle Plan Order");
			auto completed = opening;
			completed.complete();
			if(pack.preCombatOrderState != completed)
				throw std::runtime_error("Battle Plan StartAction has an invalid completion state");
			acceptedPreCombatOrderState = std::move(completed);
		}
		else if(pack.preCombatOrderState)
			throw std::runtime_error("Battle Plan StartAction has no pending opening Order");
		if(opening.phase == PreCombatOrderState::Phase::AVAILABLE
			&& battleContext->getRound() == 1 && !battleContext->battleTacticDist()
			&& battleContext->getActivationSerial() == 0)
			throw std::runtime_error("Battle Plan opening Order must precede creature actions");

		const auto & continuation = battleContext->getDoubleCommandState(pack.ba.side);
		if(continuation.orderPending()
			&& (!canonicalOrder || pack.ba.command == continuation.firstOrder
				|| !battleContext->battleHasPendingDoubleCommand(pack.ba.side)))
			throw std::runtime_error("StartAction cannot interrupt a required Double Command Order");
		if(continuation.secondWindReady())
			throw std::runtime_error("StartAction cannot interrupt a ready Double Command Second Wind");
	}
	if(pack.doubleCommandState && !canonicalOrder)
		throw std::runtime_error("Double Command StartAction state requires a canonical Hero Order");
	if(pack.preserveOtherOrders && !canonicalOrder)
		throw std::runtime_error("Non-Order StartAction cannot preserve canonical Hero Orders");
	if(pack.orderState.has_value() != canonicalOrder)
		throw std::runtime_error("Inconsistent canonical Order StartAction payload");
	if(canonicalOrder)
	{
		if(pack.ba.stackNumber != static_cast<uint32_t>(pack.ba.side == BattleSide::ATTACKER ? -1 : -2))
			throw std::runtime_error("Invalid canonical Order StartAction issuer");
		std::vector<uint32_t> targetUnitIds;
		for(const auto & target : pack.ba.target)
		{
			if(target.unitValue < 0 || target.hexValue != BattleHex::INVALID)
				throw std::runtime_error("Invalid canonical Order StartAction destination");
			targetUnitIds.push_back(static_cast<uint32_t>(target.unitValue));
		}
		const auto expected = battleContext->battlePrepareHeroOrderState(pack.ba.side, pack.ba.command, targetUnitIds);
		if(!expected || expected != pack.orderState)
			throw std::runtime_error("Invalid canonical Order StartAction snapshot");
		const auto existingOrders = battleContext->getHeroOrderStates(pack.ba.side);
		if(pack.preserveOtherOrders != !existingOrders.empty())
			throw std::runtime_error("Canonical Order StartAction has an invalid preserve-existing-orders mode");
		if(pack.preserveOtherOrders && std::ranges::any_of(existingOrders, [&pack](const HeroOrderState & order)
			{
				return order.command == pack.ba.command;
			}))
			throw std::runtime_error("Canonical Order StartAction cannot duplicate an active Order");

		const auto & side = battleContext->getSide(pack.ba.side);
		const auto & previousDoubleCommand = side.doubleCommandState;
		std::optional<DoubleCommandState> expectedDoubleCommand;
		if(previousDoubleCommand.secondWindReady())
			throw std::runtime_error("StartAction cannot interrupt a ready Double Command Second Wind");
		if(previousDoubleCommand.orderPending())
		{
			const auto firstOrder = battleContext->getHeroOrderState(pack.ba.side, previousDoubleCommand.firstOrder);
			const auto selected = battleContext->battleGetOrderActionAllowance(pack.ba.side);
			if(!battleContext->battleHasPendingDoubleCommand(pack.ba.side) || !firstOrder
				|| firstOrder->issuedRound != previousDoubleCommand.issuedRound
				|| pack.ba.command == previousDoubleCommand.firstOrder
				|| !selected || selected->source != HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND)
				throw std::runtime_error("Canonical Order does not satisfy the pending Double Command continuation");
			auto next = previousDoubleCommand;
			next.completeFollowup(pack.ba.command);
			expectedDoubleCommand = std::move(next);
		}
		else if(pack.doubleCommandState)
		{
			const auto selected = battleContext->battleGetOrderActionAllowance(pack.ba.side);
			const auto * anchor = battleContext->battleActiveUnit();
			if(!selected || selected->allowance != HeroActionAllowanceState::AllowanceKind::HERO
				|| !anchor || !pack.orderState)
				throw std::runtime_error("Double Command requires an accepted flexible-Hero Order action");
			// Reject allocation failure before consuming the primary Hero Action.
			if(side.heroActionAllowances.nextGrantId == std::numeric_limits<uint32_t>::max())
				throw std::runtime_error("Double Command allowance grant IDs are exhausted");
			const HeroActionAllowanceState::Receipt receipt{selected->grantId,
				HeroActionAllowanceState::ActionKind::ORDER, selected->allowance, selected->source,
				battleContext->getRound()};
			auto next = previousDoubleCommand;
			const auto deferredTarget = pack.ba.command == HeroCommand::SECOND_WIND
				? pack.orderState->primaryTargetUnitId : DoubleCommandState::INVALID_UNIT_ID;
			if(!next.begin(receipt, true, pack.ba.command, battleContext->getRound(), anchor->unitId(), deferredTarget))
				throw std::runtime_error("Invalid Double Command primary Order transition");
			expectedDoubleCommand = std::move(next);
		}
		if(pack.doubleCommandState != expectedDoubleCommand)
			throw std::runtime_error("StartAction Double Command transition does not match its action receipt");
		acceptedDoubleCommandState = std::move(expectedDoubleCommand);
	}
	if(pack.focusFire.has_value() != targeted)
		throw std::runtime_error("Inconsistent targeted StartAction payload");
	if(targeted)
	{
		if(pack.ba.spell.hasValue() || pack.ba.target.size() != 1 || pack.ba.target.front().unitValue < 0
			|| pack.ba.target.front().hexValue != BattleHex::INVALID
			|| pack.ba.stackNumber != static_cast<uint32_t>(pack.ba.side == BattleSide::ATTACKER ? -1 : -2))
			throw std::runtime_error("Invalid targeted StartAction destination");
		// Validate against the saved battle context before changing any budget/state.
		const auto expected = battleContext->battlePrepareFocusFireState(
			pack.ba.side, pack.ba.target.front().unitValue);
		if(!expected || expected != pack.focusFire)
			throw std::runtime_error("Invalid targeted StartAction snapshot");
	}
	if(pack.ba.side == BattleSide::ATTACKER || pack.ba.side == BattleSide::DEFENDER)
	{
		auto * battle = gs.getBattle(pack.battleID);
		if(pack.ba.metamagicFollowup
			&& !battleContext->battleCanUseMetamagicFollowup(pack.ba.side, pack.ba.spell))
			throw std::runtime_error("Metamagic follow-up StartAction without a selected Metamagic grant");
		if(pack.ba.timeStopHeroActionPass)
		{
			const auto * active = battle->battleActiveUnit();
			if(pack.ba.actionType != EActionType::NO_ACTION || !active || !active->isTimeStopped()
				|| active->unitId() != pack.ba.stackNumber
				|| battle->playerToSide(battle->battleGetOwner(active)) != pack.ba.side)
				throw std::runtime_error("Invalid Time Stop Hero Action pass StartAction");
			battle->expireTimeStops(pack.ba.side);
		}
		// A spell action is not necessarily a Hero Action: inspect the selected
		// allowance rather than inferring provenance from Metamagic packet flags.
		// This runs at StartAction so Time Stop ends before the Hero-paid spell
		// resolves. The cast visitor later consumes the same deterministic grant.
		if(pack.ba.actionType == EActionType::HERO_SPELL)
		{
			const bool sharedActionBudget = heroCommands::supportedByRules(
				battle->getHeroCommandRules(), HeroCommand::CHARGE);
			bool spendsHeroAction = !pack.ba.metamagicFollowup;
			if(sharedActionBudget)
			{
				const auto selected = battleContext->battleGetSpellActionAllowance(pack.ba.side, pack.ba.spell);
				if(!selected)
					throw std::runtime_error("Hero spell StartAction has no eligible payload-aware allowance");
				spendsHeroAction = selected->allowance == HeroActionAllowanceState::AllowanceKind::HERO;
			}
			if(spendsHeroAction)
				battle->expireTimeStops(pack.ba.side);
		}
	}
	if(pack.ba.actionType == EActionType::HERO_COMMAND)
	{
		if(heroCommands::isDoctrine(pack.ba.command)
			|| !heroCommands::supportedByRules(gs.getBattle(pack.battleID)->getHeroCommandRules(), pack.ba.command))
			throw std::runtime_error("Legacy or unsupported Hero Doctrine cannot be applied");
		auto * commandBattle = gs.getBattle(pack.battleID);
		auto & side = commandBattle->getSide(pack.ba.side);
		const bool sharedActionBudget = heroCommands::supportedByRules(
			commandBattle->getHeroCommandRules(), HeroCommand::CHARGE);
		std::optional<HeroActionAllowanceState::Receipt> orderReceipt;
		auto nextAllowances = side.heroActionAllowances;
		if(sharedActionBudget)
		{
			const auto mandate = commandBattle->battleGetDivineMandateStatus(pack.ba.side);
			const bool mandateAvailable = mandate.active
				&& mandate.completedPairs < mandate.maximumPairs;
			const auto orderGrantFilter = [mandateAvailable](const HeroActionAllowanceState::Grant & grant)
			{
				return grant.source != HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
					|| mandateAvailable;
			};
			const auto selected = nextAllowances.eligibleAllowance(
				HeroActionAllowanceState::ActionKind::ORDER, commandBattle->getRound(), orderGrantFilter);
			if(!selected)
				throw std::runtime_error("Accepted Order has no eligible action allowance");
			if(acceptedPreCombatOrderState
				&& (selected->allowance != HeroActionAllowanceState::AllowanceKind::ORDER
					|| selected->source != HeroActionAllowanceState::GrantSource::BATTLE_PLAN))
				throw std::runtime_error("Battle Plan Order has no selected dedicated allowance");
			orderReceipt = nextAllowances.consumeAllowance(selected->grantId,
				HeroActionAllowanceState::ActionKind::ORDER, commandBattle->getRound(), orderGrantFilter);
			if(!orderReceipt)
				throw std::runtime_error("Could not commit accepted Order action allowance");
			DivineMandateTransition::applyAcceptedAction(nextAllowances, *orderReceipt,
				commandBattle->getRound(), false, mandate.maximumPairs);
		}
		if(acceptedPreCombatOrderState && !orderReceipt)
			throw std::runtime_error("Battle Plan Order did not consume its dedicated allowance");
		if(acceptedDoubleCommandState)
		{
			if(!side.doubleCommandState.orderPending())
			{
				if(!orderReceipt || orderReceipt->allowance != HeroActionAllowanceState::AllowanceKind::HERO
					|| nextAllowances.nextGrantId == std::numeric_limits<uint32_t>::max())
					throw std::runtime_error("Invalid Double Command primary Order receipt");
				nextAllowances.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER,
					HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND, commandBattle->getRound());
			}
			else if(!orderReceipt || orderReceipt->source != HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND)
				throw std::runtime_error("Double Command follow-up did not consume its grant");
			commandBattle->setDoubleCommandState(pack.ba.side, *acceptedDoubleCommandState);
		}
		if(sharedActionBudget)
			side.heroActionAllowances = std::move(nextAllowances);
		if(acceptedDoubleCommandState)
			side.validateDoubleCommandState();
		const bool spendsHeroAction = sharedActionBudget
			? orderReceipt && orderReceipt->allowance == HeroActionAllowanceState::AllowanceKind::HERO
			: true;
		if(spendsHeroAction)
		{
			commandBattle->expireTimeStops(pack.ba.side);
			side.counterspellArmed = false;
			side.metamagicCountersequenceArmed = false;
		}
		std::optional<AlternatingHeroActionState> nextWarcastingState;
		if(newHorizonsWarcasting::enabled(commandBattle->getMagicRules())
			&& (!sharedActionBudget || (orderReceipt
				&& orderReceipt->allowance == HeroActionAllowanceState::AllowanceKind::HERO)))
		{
			auto next = side.warcastingState;
			const auto * hero = commandBattle->battleGetFightingHero(pack.ba.side);
			const int effectiveBonus = newHorizonsWarcasting::orderBonus(
				hero, next, commandBattle->getRound());
			next.recordAcceptedAction(AlternatingHeroActionState::Action::ORDER,
				commandBattle->getRound(), newHorizonsWarcasting::empowerment(hero,
					AlternatingHeroActionState::Action::ORDER), newHorizonsWarcasting::readinessLifetimeRounds(hero));
			if(canonicalOrder && (!pack.orderState || effectiveBonus != pack.orderState->warcastingBonusPercent))
				throw std::runtime_error("Warcasting Order snapshot does not match current readiness");
			nextWarcastingState = std::move(next);
		}
		// Preserve the legacy action-history marker for save validators and
		// presentation. Shared-budget availability is derived only from the
		// receipt-backed ledger above.
		side.heroCommandUsed = true;
		if(nextWarcastingState)
			side.warcastingState = *nextWarcastingState;
		if(targeted)
			side.focusFire = pack.focusFire;
		if(canonicalOrder)
		{
			if(pack.preserveOtherOrders)
				commandBattle->setHeroOrderState(pack.ba.side, pack.orderState);
			else
				commandBattle->setHeroOrderStates(pack.ba.side,
					pack.orderState ? std::vector<HeroOrderState>{*pack.orderState} : std::vector<HeroOrderState>{});
			if(acceptedPreCombatOrderState)
				commandBattle->setPreCombatOrderState(pack.ba.side, *acceptedPreCombatOrderState);
		}
		else
		{
			commandBattle->setHeroOrderStates(pack.ba.side, {});
		}
		return;
	}
	CStack *st = gs.getBattle(pack.battleID)->getStack(pack.ba.stackNumber);

	if(pack.ba.actionType == EActionType::END_TACTIC_PHASE)
	{
		auto * battle = gs.getBattle(pack.battleID);
		if(!battle->getDeploymentState().independent)
			battle->tacticDistance = 0;
		return;
	}

	if(gs.getBattle(pack.battleID)->tacticDistance)
	{
		// moves in tactics phase do not affect creature status
		// (tactics stack queue is managed by client)
		return;
	}

	if (pack.ba.isUnitAction())
	{
		assert(st); // stack must exists for all non-hero actions
		bool masterGateContinuation = false;
		if(pack.ba.actionType == EActionType::DEMONIC_GATING)
		{
			if(pack.ba.side != BattleSide::ATTACKER && pack.ba.side != BattleSide::DEFENDER)
				throw std::runtime_error("Invalid Demonic Gating StartAction side");
			// Mobile Gate commits its reserve stack only after the authoritative
			// movement has completed and the destination is revalidated.
			if(pack.ba.target.size() != 2)
			{
				auto * actionBattle = gs.getBattle(pack.battleID);
				auto & side = actionBattle->getSide(pack.ba.side);
				const auto found = side.demonicReserve.find(pack.ba.gatingCreature);
				if(found == side.demonicReserve.end() || found->second <= 0 || pack.ba.target.size() != 1)
					throw std::runtime_error("Invalid Demonic Gating StartAction snapshot");
				const auto * hero = battleContext->battleGetFightingHero(pack.ba.side);
				masterGateContinuation = !side.masterGateUsed && hero && hero->hasActivePerk(
					"new-horizons:demonicGating", "new-horizons:demonicGating.masterGate");
				if(masterGateContinuation)
				{
					const auto * active = battleContext->battleActiveUnit();
					if(!active || active != st || active->unitSide() != pack.ba.side)
						throw std::runtime_error("Master Gate StartAction does not match the active stack");
					side.masterGateUsed = true;
				}
				SideInBattle::PendingDemonicGate gate;
				gate.creature = found->first;
				gate.count = found->second;
				gate.position = pack.ba.target.front().hexValue;
				gate.arrivalRound = gs.getBattle(pack.battleID)->getRound() + 1;
				gate.sourceUnitId = pack.ba.stackNumber;
				side.demonicReserve.erase(found);
				side.pendingDemonicGates.push_back(gate);
			}
		}

		switch(pack.ba.actionType)
		{
			case EActionType::DEFEND:
				st->defending = true;
				st->bulwarkPreemptiveUsed = false;
				st->waiting = false;
				break;
			case EActionType::WAIT:
				st->afterWait();
				break;
			case EActionType::MONSTER_SPELL:
			{
				SpellID spellID = pack.ba.spell;
				if (spellID.hasValue() && spellID.toSpell()->canCastWithoutSkip())
				{
					//state does not change
				}
				else
				{
					st->waiting = false;
					st->movedThisRound = true;
				}
				st->castSpellThisTurn = true;
				break;
			}
			case EActionType::HERO_SPELL: //no change in current stack state
				break;
			case EActionType::NO_ACTION:
				// Automatic no-op is how the turn queue advances a stopped stack.
				// It must not count as movement/action state while the stack is
				// outside time (ordinary NO_ACTION keeps the legacy bookkeeping).
				// If the controlling side has no fighting hero, there is no future
				// Hero Action boundary that could release its origin. Treat this
				// authenticated pass as the safe terminal boundary instead of
				// scheduling the same stopped anchor forever.
				if(pack.ba.timeStopHeroActionPass)
					break;
				if(st->isTimeStopped())
				{
					auto * battleState = gs.getBattle(pack.battleID);
					const auto controlSide = battleState->playerToSide(battleState->battleGetOwner(st));
					if(!battleState->battleGetFightingHero(controlSide))
						battleState->expireTimeStops(controlSide);
				}
				if(!st->isTimeStopped())
				{
					st->waiting = false;
					st->movedThisRound = true;
				}
				break;
			default: //any active stack action - attack, catapult, heal, spell...
				st->waiting = false;
				st->movedThisRound = true;
				break;
		}
		if(masterGateContinuation)
			st->movedThisRound = false;
	}
	else
	{
		if(pack.ba.actionType == EActionType::HERO_SPELL)
			gs.getBattle(pack.battleID)->getSide(pack.ba.side).usedSpellsHistory.push_back(pack.ba.spell);
	}
}

void GameStatePackVisitor::visitBattleHeroOrderStateChanged(BattleHeroOrderStateChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for canonical Hero Order state update");
	if(!heroCommands::isCanonicalRules(battle->getHeroCommandRules()))
		throw std::runtime_error("Canonical Hero Order state update in a legacy battle");
	if(pack.side != BattleSide::ATTACKER && pack.side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid side in canonical Hero Order state update");
	if(!pack.states)
		throw std::runtime_error("Canonical Hero Order update has no full collection");
	const auto projection = pack.states->empty()
		? std::optional<HeroOrderState>() : std::optional<HeroOrderState>(pack.states->back());
	if(pack.state != projection)
		throw std::runtime_error("Canonical Hero Order update has an invalid latest-order projection");
	if(!pack.states->empty())
	{
		for(const auto & order : *pack.states)
		{
			order.validateShape();
			if(order.issuedRound != battle->getRound()
				|| !heroCommands::supportedByRules(battle->getHeroCommandRules(), order.command))
				throw std::runtime_error("Canonical Hero Order state update does not match battle context");
		}
		if(pack.state->command != battle->getActiveOrder(pack.side))
			throw std::runtime_error("Canonical Hero Order state update does not match latest issued Order");
	}
	else if(battle->getActiveOrder(pack.side) != HeroCommand::NONE)
	{
		throw std::runtime_error("Canonical Hero Order state update would clear an active Order");
	}
	validateHeroOrderStateMutation(*battle, pack.side, *pack.states);
	if(pack.doubleCommandState)
	{
		auto & side = battle->getSide(pack.side);
		pack.doubleCommandState->validateTransitionFrom(side.doubleCommandState);
		if(side.doubleCommandState.orderPending()
			&& !pack.doubleCommandState->orderPending())
		{
			const bool anotherLegalOrderExists = std::ranges::any_of(heroCommands::CANONICAL_COMMANDS,
				[&](HeroCommand command)
				{
					return command != side.doubleCommandState.firstOrder
						&& battle->battleCanBeginHeroCommand(pack.side, command);
				});
			if(anotherLegalOrderExists)
				throw std::runtime_error("Cannot exhaust Double Command while a distinct Order remains available");
		}
	}
	if(pack.doubleCommandState && pack.preCombatOrderState)
		throw std::runtime_error("One Hero Order state update cannot change both continuations");
	if(pack.preCombatOrderState)
	{
		const auto & previous = battle->getPreCombatOrderState(pack.side);
		pack.preCombatOrderState->validateTransitionFrom(previous);
		if(*pack.preCombatOrderState != previous)
		{
			if(previous.phase == PreCombatOrderState::Phase::AVAILABLE
				&& pack.preCombatOrderState->orderPending())
			{
				if(battle->getRound() != 1 || battle->getActivationSerial() != 0
					|| (pack.side == BattleSide::DEFENDER
						&& battle->getPreCombatOrderState(BattleSide::ATTACKER).isUnresolved()))
					throw std::runtime_error("Battle Plan opening Order is outside its deterministic opening window");
				const auto anchorId = pack.preCombatOrderState->anchorStackId;
				const auto * anchor = battle->battleGetStackByID(anchorId, false);
				if(battle->getSide(pack.side).heroActionAllowances.nextGrantId
					== std::numeric_limits<uint32_t>::max())
					throw std::runtime_error("Battle Plan allowance grant IDs are exhausted");
				if(!anchor || !anchor->alive() || anchor->isGhost() || anchor->isTurret()
					|| anchor->hasBonusOfType(BonusType::SIEGE_WEAPON)
					|| anchor->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
					|| anchor->unitSlot() == SlotID::WAR_MACHINES_SLOT
					|| battle->playerToSide(battle->battleGetOwner(anchor)) != pack.side)
					throw std::runtime_error("Battle Plan opening Order has no legal stack anchor");
			}
			else if(previous.phase == PreCombatOrderState::Phase::AVAILABLE
				&& pack.preCombatOrderState->phase == PreCombatOrderState::Phase::COMPLETED)
			{
				if(battle->getRound() != 1 || battle->getActivationSerial() != 0)
					throw std::runtime_error("Battle Plan exhaustion is outside its opening window");
			}
			else
				throw std::runtime_error("Battle Plan state updates cannot complete a pending Order");
		}
	}
	battle->setHeroOrderStates(pack.side, *pack.states);
	if(pack.doubleCommandState)
		battle->setDoubleCommandState(pack.side, *pack.doubleCommandState);
	if(pack.preCombatOrderState)
		battle->setPreCombatOrderState(pack.side, *pack.preCombatOrderState);
	battle->getSide(pack.side).validateDoubleCommandState();
}

void GameStatePackVisitor::visitBattleDemonicGatingStateChanged(BattleDemonicGatingStateChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle || (pack.side != BattleSide::ATTACKER && pack.side != BattleSide::DEFENDER))
		throw std::runtime_error("Invalid Demonic Gating battle state update");
	auto & side = battle->getSide(pack.side);
	if(pack.masterGateContinuationUnitId)
	{
		const auto unitId = *pack.masterGateContinuationUnitId;
		const auto * unit = battle->battleGetStackByID(unitId, false);
		const auto * active = battle->battleActiveUnit();
		const auto * hero = battle->battleGetFightingHero(pack.side);
		const auto newlyPending = std::ranges::find_if(pack.pending, [&](const auto & gate)
		{
			return gate.sourceUnitId == unitId
				&& std::ranges::none_of(side.pendingDemonicGates, [&](const auto & prior)
				{
					return prior.sourceUnitId == gate.sourceUnitId && prior.creature == gate.creature;
				});
		});
		const auto consumedReserve = newlyPending == pack.pending.end() ? side.demonicReserve.end()
			: side.demonicReserve.find(newlyPending->creature);
		const auto remainingReserve = newlyPending == pack.pending.end() ? pack.reserve.end()
			: pack.reserve.find(newlyPending->creature);
		if(side.masterGateUsed || !pack.masterGateUsed || !unit || !unit->alive()
			|| unit->unitSide() != pack.side || !active || active->unitId() != unitId
			|| !unit->movedThisRound || !hero || !hero->hasActivePerk(
				"new-horizons:demonicGating", "new-horizons:demonicGating.masterGate")
			|| newlyPending == pack.pending.end() || consumedReserve == side.demonicReserve.end()
			|| newlyPending->count != consumedReserve->second
			|| (remainingReserve != pack.reserve.end() && remainingReserve->second >= consumedReserve->second))
			throw std::runtime_error("Invalid Master Gate mobile continuation state update");
	}
	else if(pack.masterGateUsed != side.masterGateUsed)
	{
		throw std::runtime_error("Demonic Gating update changed Master Gate state without a continuation");
	}
	side.demonicReserve = std::move(pack.reserve);
	side.pendingDemonicGates = std::move(pack.pending);
	side.gatedDemonicStacks = std::move(pack.gated);
	side.chainGateArmed = pack.chainGateArmed;
	side.masterGateUsed = pack.masterGateUsed;
	if(pack.masterGateContinuationUnitId)
		battle->getStack(*pack.masterGateContinuationUnitId)->movedThisRound = false;
}

void GameStatePackVisitor::visitBattleAdverseRerollStateChanged(BattleAdverseRerollStateChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for adverse combat reroll state update");
	pack.validateShape();
	pack.validateTransitionFrom(battle->getAdverseCombatRerollState(pack.side));
	battle->setAdverseCombatRerollState(pack.side, pack.state);
}

void GameStatePackVisitor::visitBattleMoraleSuppressionStateChanged(BattleMoraleSuppressionStateChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for Rally Morale suppression state update");
	pack.validateShape();
	pack.validateTransitionFrom(battle->getMoraleSuppressionState(pack.side));
	battle->setMoraleSuppressionState(pack.side, pack.state);
}

void GameStatePackVisitor::visitBattleReducedExtraActivationStateChanged(BattleReducedExtraActivationStateChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for reduced extra activation state update");
	pack.validateTransitionFrom(battle->getReducedExtraActivationState(pack.side));
	battle->setReducedExtraActivationState(pack.side, pack.state);
}

void GameStatePackVisitor::visitSetArmorerDefiantState(SetArmorerDefiantState & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for Defiant consumption");
	pack.validateAgainst(*battle);
	battle->setArmorerDefiantState(pack.side, pack.state);
}

void GameStatePackVisitor::visitSetSpellResponseState(SetSpellResponseState & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for Spell Response state update");
	pack.validateTransitionFrom(battle->getSpellResponseState(pack.side), battle->getRound());
	battle->setSpellResponseState(pack.side, pack.state);
}

void GameStatePackVisitor::visitSetOverwhelmingFormulaState(SetOverwhelmingFormulaState & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for Overwhelming Formula state update");
	pack.validateTransitionFrom(battle->getOverwhelmingFormulaState(pack.side));
	battle->setOverwhelmingFormulaState(pack.side, pack.state);
}

void GameStatePackVisitor::visitSetBattlecraftMasteryAward(SetBattlecraftMasteryAward & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("Missing battle for Battlefield Mastery award");
	pack.validateTransitionFrom(battle->getBattlecraftMasteryAwardRound(pack.side), battle->getRound());
	battle->awardBattlecraftMastery(pack.side, pack.unitId, pack.round, pack.action);
}

void GameStatePackVisitor::visitBattleSpellCast(BattleSpellCast & pack)
{
	if(pack.paidHeroManaCost < 0 || pack.paidCounterspellManaCost < 0
		|| (pack.paidHeroManaCost > 0 && (!pack.castByHero || !pack.activeCast
			|| (pack.side != BattleSide::ATTACKER && pack.side != BattleSide::DEFENDER)))
		|| (pack.paidCounterspellManaCost > 0
			&& (!pack.castByHero || !pack.activeCast || !pack.counterspellNegated
				|| (pack.side != BattleSide::ATTACKER && pack.side != BattleSide::DEFENDER)
				|| (pack.counterspellSide != BattleSide::ATTACKER && pack.counterspellSide != BattleSide::DEFENDER)
				|| pack.counterspellSide == pack.side)))
		throw std::runtime_error("Invalid accepted hero Mana expenditure metadata");

	if(pack.castByHero && pack.side != BattleSide::NONE)
	{
		auto * battle = gs.getBattle(pack.battleID);
		if(!battle)
			throw std::runtime_error("Accepted hero Mana expenditure references a missing battle");
		auto & casterSide = battle->getSide(pack.side);
		const auto * hero = battle->battleGetFightingHero(pack.side);
		if(pack.paidHeroManaCost > 0 && !hero)
			throw std::runtime_error("Accepted hero Mana expenditure has no fighting hero");
		if(pack.paidCounterspellManaCost > 0 && !battle->battleGetFightingHero(pack.counterspellSide))
			throw std::runtime_error("Accepted Counterspell Mana expenditure has no fighting hero");
		const bool sharedActionBudget = heroCommands::supportedByRules(
			battle->getHeroCommandRules(), HeroCommand::CHARGE);
		const auto lightSchoolNumber = SpellSchool::decode("new-horizons:light");
		const auto & savedMagicRules = battle->getMagicRules();
		const auto spellSchools = newHorizonsMagic::spellSchools(savedMagicRules, pack.spellID);
		const bool isLightSpell = newHorizonsMagic::spellAllowedByHeroRoster(savedMagicRules, pack.spellID)
			&& lightSchoolNumber >= 0
			&& std::find(spellSchools.begin(), spellSchools.end(), SpellSchool(lightSchoolNumber)) != spellSchools.end();
		const auto mandate = battle->battleGetDivineMandateStatus(pack.side);
		const bool mandateAvailable = mandate.active && mandate.completedPairs < mandate.maximumPairs;
		const auto spellGrantFilter = [isLightSpell, mandateAvailable](const HeroActionAllowanceState::Grant & grant)
		{
			return HeroActionAllowanceState::grantAllowedForSpell(grant, isLightSpell)
				&& (grant.source != HeroActionAllowanceState::GrantSource::DIVINE_MANDATE || mandateAvailable);
		};
		std::optional<HeroSpellAllowanceTransition::Result> spellTransition;
		if(pack.metamagicGrand && !pack.metamagicFollowup)
			throw std::runtime_error("Grand Metamagic metadata requires a follow-up cast");
		if(pack.metamagicManaRefund > 0
			&& (!pack.metamagicFollowup || pack.metamagicGrand || casterSide.metamagicPendingCount != 1
				|| !hero || !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules())
				|| !newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_FORMULA_RESERVE)))
			throw std::runtime_error("Invalid Formula Reserve Metamagic refund");
		if(sharedActionBudget)
		{
			auto nextAllowances = casterSide.heroActionAllowances;
			auto nextMetamagicUsesConsumed = casterSide.metamagicUsesConsumed;
			auto nextMetamagicPendingCount = casterSide.metamagicPendingCount;
			auto nextMetamagicGrandUsed = casterSide.metamagicGrandUsed;
			const auto selected = nextAllowances.eligibleAllowance(
				HeroActionAllowanceState::ActionKind::SPELL, battle->getRound(), spellGrantFilter);
			if(!selected)
				throw std::runtime_error("Accepted Hero spell has no eligible action allowance");
			auto transition = HeroSpellAllowanceTransition::commitAcceptedCast(
				nextAllowances, selected->grantId, battle->getRound(),
				pack.metamagicFollowup, pack.metamagicGrand,
				static_cast<uint8_t>(hero ? newHorizonsMagic::metamagicRank(hero) : 0),
				hero && newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
				nextMetamagicUsesConsumed, nextMetamagicPendingCount,
				nextMetamagicGrandUsed, casterSide.metamagicSequenceSpells.size(), spellGrantFilter);
			if(!transition)
				throw std::runtime_error("Accepted Hero spell has forged or inconsistent allowance metadata");
			DivineMandateTransition::applyAcceptedAction(nextAllowances, transition->receipt,
				battle->getRound(), isLightSpell, mandate.maximumPairs);
			casterSide.heroActionAllowances = std::move(nextAllowances);
			casterSide.metamagicUsesConsumed = nextMetamagicUsesConsumed;
			casterSide.metamagicPendingCount = nextMetamagicPendingCount;
			casterSide.metamagicGrandUsed = nextMetamagicGrandUsed;
			spellTransition = std::move(*transition);
		}
		else if(pack.metamagicFollowup && casterSide.metamagicPendingCount == 0)
			throw std::runtime_error("Metamagic follow-up cast without pending sequence");
		const bool spendsHeroAllowance = spellTransition
			? spellTransition->receipt.allowance == HeroActionAllowanceState::AllowanceKind::HERO
			: !pack.metamagicFollowup;

		if(!pack.metamagicFollowup)
			casterSide.castSpellsCount++;
		// The accepted cast packet is the own-ward boundary. Invalid or rejected
		// requests never reach this visitor. An enemy hero spell still clears the
		// opposing ward below, independent of its allowance.
		if(spendsHeroAllowance)
		{
			casterSide.counterspellArmed = false;
			casterSide.metamagicCountersequenceArmed = false;
		}
		if(pack.temporalFieldCast)
			casterSide.temporalFieldUsed = true;
		if(pack.counterspellSide == BattleSide::ATTACKER || pack.counterspellSide == BattleSide::DEFENDER)
		{
			battle->getSide(pack.counterspellSide).counterspellArmed = false;
			battle->getSide(pack.counterspellSide).metamagicCountersequenceArmed = false;
		}
		if(!pack.counterspellNegated && newHorizonsMagic::isCounterspell(pack.spellID.toSpell()))
		{
			casterSide.counterspellArmed = true;
			casterSide.metamagicCountersequenceArmed = pack.metamagicFollowup
				&& newHorizonsMagic::hasMetamagicPerk(
					battle->battleGetFightingHero(pack.side), newHorizonsMagic::METAMAGIC_COUNTERSEQUENCE);
		}

		if(pack.metamagicManaRefund > 0)
			casterSide.metamagicFormulaReserveUsed = true;
		if(sharedActionBudget)
		{
			const auto & receipt = spellTransition->receipt;
			if(receipt.allowance == HeroActionAllowanceState::AllowanceKind::HERO)
			{
				casterSide.heroCommandUsed = true;
				if(spellTransition->pendingMetamagicGrants != 0)
				{
					casterSide.metamagicFirstSpell = pack.spellID;
					casterSide.metamagicFirstTargetUnitId = pack.metamagicTargetUnitId;
					casterSide.metamagicSequenceSpells = {pack.spellID};
					casterSide.metamagicFirstCounterspellNegated = pack.counterspellNegated;
				}
			}
			else if(receipt.source == HeroActionAllowanceState::GrantSource::METAMAGIC
				|| receipt.source == HeroActionAllowanceState::GrantSource::METAMAGIC_GRAND)
			{
				casterSide.metamagicSequenceSpells.push_back(pack.spellID);
				if(spellTransition->pendingMetamagicGrants == 0)
					casterSide.clearMetamagicSequence();
			}
		}
		else if(pack.metamagicFollowup)
		{
			const int rank = hero ? newHorizonsMagic::metamagicRank(hero) : 0;
			const bool grandActivation = HeroSpellAllowanceTransition::activatesGrand(
				pack.metamagicFollowup,
				casterSide.metamagicPendingCount,
				casterSide.metamagicSequenceSpells.size(),
				casterSide.metamagicUsesConsumed,
				static_cast<uint8_t>(rank),
				hero && newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND),
				casterSide.metamagicGrandUsed);
			if(pack.metamagicGrand != grandActivation)
				throw std::runtime_error("Battle spell cast has forged Grand Metamagic outcome");

			if(pack.metamagicGrand)
			{
				if(!hero || casterSide.metamagicPendingCount != 1 || casterSide.metamagicSequenceSpells.size() != 1
					|| casterSide.metamagicGrandUsed || rank < 3 || casterSide.metamagicUsesConsumed != 2
					|| !newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_GRAND))
					throw std::runtime_error("Invalid server-derived Grand Metamagic outcome");
				++casterSide.metamagicUsesConsumed;
				casterSide.metamagicGrandUsed = true;
				casterSide.metamagicPendingCount = 2;
			}
			else if(casterSide.metamagicSequenceSpells.size() == 1)
			{
				const int rank = hero ? newHorizonsMagic::metamagicRank(hero) : 0;
				if(!hero || rank <= casterSide.metamagicUsesConsumed)
					throw std::runtime_error("Metamagic use was not available when follow-up was accepted");
				++casterSide.metamagicUsesConsumed;
			}
			--casterSide.metamagicPendingCount;
			casterSide.metamagicSequenceSpells.push_back(pack.spellID);
			if(casterSide.metamagicPendingCount == 0)
				casterSide.clearMetamagicSequence();
		}
		else if(hero)
		{
			const int rank = newHorizonsMagic::metamagicRank(hero);
			if(rank > casterSide.metamagicUsesConsumed && rank > 0)
			{
				casterSide.metamagicPendingCount = 1;
				casterSide.metamagicFirstSpell = pack.spellID;
				casterSide.metamagicFirstTargetUnitId = pack.metamagicTargetUnitId;
				casterSide.metamagicSequenceSpells = {pack.spellID};
				casterSide.metamagicFirstCounterspellNegated = pack.counterspellNegated;
			}
		}
		if(spendsHeroAllowance && newHorizonsWarcasting::enabled(battle->getMagicRules()))
		{
			auto next = casterSide.warcastingState;
			if(newHorizonsWarcasting::battleMeditationEligible(
				battle->getMagicRules(), hero, casterSide.warcastingState, battle->getRound()))
				next.lastManaRecoveryRound = battle->getRound();
			next.recordAcceptedAction(AlternatingHeroActionState::Action::SPELL, battle->getRound(),
				newHorizonsWarcasting::empowerment(hero, AlternatingHeroActionState::Action::SPELL),
				newHorizonsWarcasting::readinessLifetimeRounds(hero));
			casterSide.warcastingState = std::move(next);
		}
		const auto addManaSpent = [](SideInBattle & side, int32_t amount)
		{
			if(amount <= 0)
				return;
			if(side.acceptedHeroManaSpent > std::numeric_limits<int64_t>::max() - amount)
				side.acceptedHeroManaSpent = std::numeric_limits<int64_t>::max();
			else
				side.acceptedHeroManaSpent += amount;
		};
		addManaSpent(casterSide, pack.paidHeroManaCost);
		if(pack.paidCounterspellManaCost > 0)
			addManaSpent(battle->getSide(pack.counterspellSide), pack.paidCounterspellManaCost);
		// This marker is set only after every accepted-cast validation above has
		// succeeded; creature casts and rejected hero requests never consume it.
		casterSide.heroSpellCastCompleted = true;
		casterSide.recordCompletedHeroSpellLevel(battle->battleGetSpellLevel(pack.spellID));
	}
}

void GameStatePackVisitor::visitSetStackEffect(SetStackEffect & pack)
{
	BattleStatePackVisitor battleVisitor(*gs.getBattle(pack.battleID));
	pack.visitTyped(battleVisitor);
}

void GameStatePackVisitor::visitStacksInjured(StacksInjured & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	std::set<BattleSide> chainGateSides;
	for(const auto & hit : pack.stacks)
		if(chainGateKillQualifies(*battle, hit.attackerID, pack.stacks))
			chainGateSides.insert(battle->gatedDemonicStackSide(hit.attackerID));
	const auto bloodrageCandidates = bloodrageDeathCandidates(*battle, pack.stacks);
	BattleStatePackVisitor battleVisitor(*battle);
	for (auto attackInfo : pack.stacks)
	{
		auto injuredStack = battle->getStack(attackInfo.stackAttacked);
		if(injuredStack && !injuredStack->isTimeStopped())
			injuredStack->removeBonusesRecursive(Bonus::UntilTakingIndirectDamage);
	}
	pack.visitTyped(battleVisitor);
	for(const auto & hit : pack.stacks)
		removeExhaustedGuardianSpirit(*battle, hit);
	recordBloodrageDeaths(*battle, bloodrageCandidates);
	refreshBloodrageLivingUnits(*battle, pack.stacks);
	for(const auto side : chainGateSides)
		battle->armChainGate(side);
}

void GameStatePackVisitor::visitBattleUnitsChanged(BattleUnitsChanged & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	std::set<uint32_t> removed;
	for(const auto & change : pack.changedStacks)
	{
		const auto * unit = battle->getStack(change.id, false);
		if(change.operation == BattleChanges::EOperation::REMOVE && unit && unit->alive()
			&& !unit->acquireState()->summoned && !unit->isClone())
			removed.insert(change.id);
	}
	BattleStatePackVisitor battleVisitor(*battle);
	pack.visitTyped(battleVisitor);
	recordBloodrageDeaths(*battle, removed);
	for(const auto & change : pack.changedStacks)
	{
		const auto * unit = battle->getStack(change.id, false);
		if(unit && unit->alive())
			battle->clearBloodrageStackDeath(change.id);
	}
}

void GameStatePackVisitor::restorePreBattleState(BattleID battleID)
{
	auto battleIter = std::ranges::find_if(gs.currentBattles, [&](const auto & battle)
	{
		return battle->battleID == battleID;
	});

	const auto & currentBattle = **battleIter;

	if (currentBattle.getDefendedTown() && currentBattle.getSideHero(BattleSide::DEFENDER))
	{
		CGTownInstance * town = gs.getTown(currentBattle.townID);
		CGHeroInstance * hero = gs.getHero(currentBattle.getSideHero(BattleSide::DEFENDER)->id);

		if (hero)
		{
			hero->removeBonusesRecursive(CSelector([](const Bonus * bonus)
			{
				return bonus && bonus->source == BonusSource::TOWN_STRUCTURE
					&& bonus->stacking.starts_with(TOWN_DEFENDING_HERO_BONUS_STACKING_PREFIX);
			}));
			hero->detachFrom(*town);
			hero->attachTo(town->townAndVis);
		}
	}
}

void GameStatePackVisitor::visitBattleCancelled(BattleCancelled & pack)
{
	spellPointBonusGraphChanged = true;
	restorePreBattleState(pack.battleID);

	auto battleIter = std::ranges::find_if(gs.currentBattles, [&](const auto & battle)
	{
		return battle->battleID == pack.battleID;
	});

	const auto & currentBattle = **battleIter;

	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if (currentBattle.getSide(i).heroID.hasValue())
		{
			CGHeroInstance * hero = gs.getHero(currentBattle.getSideHero(i)->id);
			const auto & side = currentBattle.getSide(i);
			if(newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
				hero->restoreSpellPointSnapshot(side.initialNormalSpellPoints, side.initialBufferSpellPoints);
			else
				hero->setNormalSpellPoints(side.initialMana);
		}
	}

	assert(battleIter != gs.currentBattles.end());
	gs.currentBattles.erase(battleIter);
}

void GameStatePackVisitor::visitBattleResultsApplied(BattleResultsApplied & pack)
{
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle)
		throw std::runtime_error("BattleResultsApplied references a missing battle");
	for(const auto sideID : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		applyFormulaReserveClosureReward(*battle, sideID);
		battle->getSide(sideID).clearMetamagicSequence();
	}

	spellPointBonusGraphChanged = true;
	restorePreBattleState(pack.battleID);
	pack.learnedSpells.visit(*this);

	for(auto & growing : pack.growingArtifacts)
		growing.visit(*this);

	for(auto & discharging : pack.dischargingArtifacts)
		discharging.visit(*this);

	for(auto & movingPack : pack.movingArtifacts)
		movingPack.visit(*this);

	auto battleIter = std::ranges::find_if(gs.currentBattles, [&](const auto & battle)
	{
		return battle->battleID == pack.battleID;
	});
	const auto & currentBattle = **battleIter;

	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if (currentBattle.getSide(i).heroID.hasValue())
		{
			CGHeroInstance * hero = gs.getHero(currentBattle.getSideHero(i)->id);
			const auto & side = currentBattle.getSide(i);
			if(newHorizonsMagic::spellPointRulesActive(hero->getMagicRules()))
			{
				// Remove only the unused combat-only part. Buffer granted during the
				// battle remains, and Normal restoration is not rolled back.
				if(side.temporaryBufferRemaining > 0)
				{
					if(!hero->removeBufferSpellPoints(side.temporaryBufferRemaining))
						throw std::runtime_error("Invalid remaining temporary combat Spell Points");
				}
				if(pack.necromancy.active && pack.necromancy.applied
					&& pack.necromancy.manaRecovered > 0
					&& hero->getOwner() == pack.victor
					&& !hero->restoreNormalSpellPoints(pack.necromancy.manaRecovered))
					throw std::runtime_error("Invalid Black Harvest Normal Spell Point recovery");
			}
			else
			{
				hero->setNormalSpellPoints(std::min(hero->getNormalSpellPoints(), side.initialMana));
				// Preserve the legacy scalar cleanup path for old rule snapshots.
				if(pack.necromancy.active && pack.necromancy.applied
					&& pack.necromancy.manaRecovered > 0
					&& hero->getOwner() == pack.victor)
					hero->setNormalSpellPoints(std::min<int64_t>(
						static_cast<int64_t>(hero->getNormalSpellPoints()) + pack.necromancy.manaRecovered,
						hero->manaLimit()));
			}
		}
	}

	// Release heroes from the battle - all battle consequences have been
	// applied. Any subsequent RemoveObject for one of these heroes is the
	// expected post-battle cleanup (BattleResultProcessor::battleFinalize).
	// visitRemoveObject below throws if a hero is removed while still flagged
	// as engaged - that path indicates a bug elsewhere.
	auto * mutBattle = gs.getBattle(pack.battleID);
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		mutBattle->getSide(i).heroID = ObjectInstanceID::NONE;
}

void GameStatePackVisitor::visitBattleEnded(BattleEnded & pack)
{
	spellPointBonusGraphChanged = true;
	auto battleIter = std::ranges::find_if(gs.currentBattles, [&](const auto & battle)
	{
		return battle->battleID == pack.battleID;
	});
	assert(battleIter != gs.currentBattles.end());
	gs.currentBattles.erase(battleIter);
}

void GameStatePackVisitor::visitBattleObstaclesChanged(BattleObstaclesChanged & pack)
{
	BattleStatePackVisitor battleVisitor(*gs.getBattle(pack.battleID));
	pack.visitTyped(battleVisitor);
}

void GameStatePackVisitor::visitCatapultAttack(CatapultAttack & pack)
{
	BattleStatePackVisitor battleVisitor(*gs.getBattle(pack.battleID));
	pack.visitTyped(battleVisitor);
}

void GameStatePackVisitor::visitBattleSetStackProperty(BattleSetStackProperty & pack)
{
	CStack * stack = gs.getBattle(pack.battleID)->getStack(pack.stackID, false);
	if(stack && stack->isTimeStopped())
	{
		// Resource/state packets are activation mutations too.  Time Stop keeps
		// the unit present and blocking, but rejects forged or stale property
		// changes until the authoritative expiry hook releases it.
		return;
	}
	switch(pack.which)
	{
		case BattleSetStackProperty::CASTS:
		{
			if(pack.absolute)
				logNetwork->error("Can not change casts in absolute mode");
			else
				stack->casts.use(-pack.val);
			break;
		}
		case BattleSetStackProperty::ENCHANTER_COUNTER:
		{
			auto & counter = gs.getBattle(pack.battleID)->getSide(gs.getBattle(pack.battleID)->whatSide(stack->unitOwner())).enchanterCounter;
			if(pack.absolute)
				counter = pack.val;
			else
				counter += pack.val;
			vstd::amax(counter, 0);
			break;
		}
		case BattleSetStackProperty::UNBIND:
		{
			stack->removeBonusesRecursive(Selector::type()(BonusType::BIND_EFFECT));
			break;
		}
		case BattleSetStackProperty::CLONED:
		{
			stack->cloned = true;
			break;
		}
		case BattleSetStackProperty::HAS_CLONE:
		{
			stack->cloneID = pack.val;
			break;
		}
	}
}

void GameStatePackVisitor::visitPlayerCheated(PlayerCheated & pack)
{
	assert(pack.player.isValidPlayer());

	gs.getPlayerState(pack.player)->enteredLosingCheatCode = pack.losingCheatCode;
	gs.getPlayerState(pack.player)->enteredWinningCheatCode = pack.winningCheatCode;
	gs.getPlayerState(pack.player)->cheated = !pack.localOnlyCheat;
}

void GameStatePackVisitor::visitPlayerStartsTurn(PlayerStartsTurn & pack)
{
	//assert(gs.actingPlayers.count(player) == 0);//Legal - may happen after loading of deserialized map state
	gs.actingPlayers.insert(pack.player);
}

void GameStatePackVisitor::visitPlayerEndsTurn(PlayerEndsTurn & pack)
{
	assert(gs.actingPlayers.count(pack.player) == 1);
	gs.actingPlayers.erase(pack.player);
}

void GameStatePackVisitor::visitDaysWithoutTown(DaysWithoutTown & pack)
{
	auto & playerState = gs.players.at(pack.player);
	playerState.daysWithoutCastle = pack.daysWithoutCastle;
}

void GameStatePackVisitor::visitTurnTimeUpdate(TurnTimeUpdate & pack)
{
	auto & playerState = gs.players.at(pack.player);
	playerState.turnTimer = pack.turnTimer;
}

void GameStatePackVisitor::visitEntitiesChanged(EntitiesChanged & pack)
{
	spellPointBonusGraphChanged = true;
	for(const auto & change : pack.changes)
		gs.updateEntity(change.metatype, change.entityIndex, change.data);
}

void GameStatePackVisitor::visitSetRewardableConfiguration(SetRewardableConfiguration & pack)
{
	auto * objectPtr = gs.getObjInstance(pack.objectID);

	if (!pack.buildingID.hasValue())
	{
		auto * rewardablePtr = dynamic_cast<CRewardableObject *>(objectPtr);
		assert(rewardablePtr);
		rewardablePtr->configuration = pack.configuration;
		rewardablePtr->initializeGuards();
	}
	else
	{
		auto * townPtr = dynamic_cast<CGTownInstance*>(objectPtr);
		TownBuildingInstance * buildingPtr = nullptr;

		for (auto & building : townPtr->rewardableBuildings)
			if (building.second->getBuildingType() == pack.buildingID)
				buildingPtr = building.second.get();

		auto * rewardablePtr = dynamic_cast<TownRewardableBuildingInstance *>(buildingPtr);
		assert(rewardablePtr);
		rewardablePtr->configuration = pack.configuration;
	}
}

void BattleStatePackVisitor::visitBattleStackMoved(BattleStackMoved & pack)
{
	battleState.moveUnit(pack.stack, pack.tilesToMove.back());
}

void BattleStatePackVisitor::visitBattleHeroOrderStateChanged(BattleHeroOrderStateChanged & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Canonical Hero Order state update targets another battle");
	if(pack.side != BattleSide::ATTACKER && pack.side != BattleSide::DEFENDER)
		throw std::runtime_error("Invalid side in canonical Hero Order state update");
	if(!pack.states)
		throw std::runtime_error("Canonical Hero Order update has no full collection");
	const auto projection = pack.states->empty()
		? std::optional<HeroOrderState>() : std::optional<HeroOrderState>(pack.states->back());
	if(pack.state != projection)
		throw std::runtime_error("Canonical Hero Order update has an invalid latest-order projection");
	for(const auto & order : *pack.states)
	{
		order.validateShape();
		if(order.issuedRound != battleState.getRound()
			|| !heroCommands::supportedByRules(battleState.getHeroCommandRules(), order.command))
			throw std::runtime_error("Canonical Hero Order state update does not match battle context");
	}
	if(!pack.states->empty() && pack.state->command != battleState.getActiveOrder(pack.side))
		throw std::runtime_error("Canonical Hero Order state update does not match latest issued Order");
	if(pack.states->empty() && battleState.getActiveOrder(pack.side) != HeroCommand::NONE)
		throw std::runtime_error("Canonical Hero Order state update would clear an active Order");
	validateHeroOrderStateMutation(battleState, pack.side, *pack.states);
	if(pack.doubleCommandState)
	{
		const auto & previous = battleState.getDoubleCommandState(pack.side);
		pack.doubleCommandState->validateTransitionFrom(previous);
		if(previous.orderPending() && !pack.doubleCommandState->orderPending())
		{
			// BattleInfo has the live legality callback. Detached IBattleState
			// projections do not necessarily have one, so they still validate the
			// represented state transition while authoritative receipt checks stay
			// in GameStatePackVisitor.
			if(const auto * callback = dynamic_cast<const CBattleInfoCallback *>(&battleState))
			{
				const bool anotherLegalOrderExists = std::ranges::any_of(heroCommands::CANONICAL_COMMANDS,
					[&](HeroCommand command)
					{
						return command != previous.firstOrder && callback->battleCanBeginHeroCommand(pack.side, command);
					});
				if(anotherLegalOrderExists)
					throw std::runtime_error("Cannot exhaust Double Command while a distinct Order remains available");
			}
		}
	}
	if(pack.doubleCommandState && pack.preCombatOrderState)
		throw std::runtime_error("One Hero Order state update cannot change both continuations");
	if(pack.preCombatOrderState)
	{
		const auto & previous = battleState.getPreCombatOrderState(pack.side);
		pack.preCombatOrderState->validateTransitionFrom(previous);
		if(*pack.preCombatOrderState != previous)
		{
			if(previous.phase == PreCombatOrderState::Phase::AVAILABLE
				&& pack.preCombatOrderState->orderPending())
			{
				if(battleState.getRound() != 1 || battleState.getActivationSerial() != 0
					|| (pack.side == BattleSide::DEFENDER
						&& battleState.getPreCombatOrderState(BattleSide::ATTACKER).isUnresolved()))
					throw std::runtime_error("Battle Plan opening Order is outside its deterministic opening window");
				const auto anchorId = pack.preCombatOrderState->anchorStackId;
				const auto anchors = battleState.getUnitsIf([anchorId](const battle::Unit * unit)
				{
					return unit && unit->unitId() == anchorId;
				});
				if(anchors.size() != 1 || !anchors.front()->alive() || anchors.front()->isGhost()
					|| anchors.front()->isTurret()
					|| anchors.front()->hasBonusOfType(BonusType::SIEGE_WEAPON)
					|| anchors.front()->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER
					|| anchors.front()->unitSlot() == SlotID::WAR_MACHINES_SLOT)
					throw std::runtime_error("Battle Plan opening Order has no legal stack anchor");
				if(const auto * callback = dynamic_cast<const CBattleInfoCallback *>(&battleState);
					callback && callback->playerToSide(callback->battleGetOwner(anchors.front())) != pack.side)
					throw std::runtime_error("Battle Plan opening Order anchor is controlled by the wrong side");
			}
			else if(previous.phase == PreCombatOrderState::Phase::AVAILABLE
				&& pack.preCombatOrderState->phase == PreCombatOrderState::Phase::COMPLETED)
			{
				if(battleState.getRound() != 1 || battleState.getActivationSerial() != 0)
					throw std::runtime_error("Battle Plan exhaustion is outside its opening window");
			}
			else
				throw std::runtime_error("Battle Plan state updates cannot complete a pending Order");
		}
	}
	battleState.setHeroOrderStates(pack.side, *pack.states);
	if(pack.doubleCommandState)
		battleState.setDoubleCommandState(pack.side, *pack.doubleCommandState);
	if(pack.preCombatOrderState)
		battleState.setPreCombatOrderState(pack.side, *pack.preCombatOrderState);
}

void BattleStatePackVisitor::visitBattleDeploymentPhaseChanged(BattleDeploymentPhaseChanged & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Deployment phase update targets another battle");
	const auto & previous = battleState.getDeploymentState();
	if(!previous.independent || !pack.state.independent)
		throw std::runtime_error("Deployment phase update does not target an independent deployment");
	pack.state.validateTransitionFrom(previous);
	battleState.setDeploymentState(pack.state);
}

void BattleStatePackVisitor::visitBattleAdverseRerollStateChanged(BattleAdverseRerollStateChanged & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Adverse combat reroll state update targets another battle");
	pack.validateShape();
	pack.validateTransitionFrom(battleState.getAdverseCombatRerollState(pack.side));
	battleState.setAdverseCombatRerollState(pack.side, pack.state);
}

void BattleStatePackVisitor::visitBattleMoraleSuppressionStateChanged(BattleMoraleSuppressionStateChanged & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Rally Morale suppression state update targets another battle");
	pack.validateShape();
	pack.validateTransitionFrom(battleState.getMoraleSuppressionState(pack.side));
	battleState.setMoraleSuppressionState(pack.side, pack.state);
}

void BattleStatePackVisitor::visitBattleReducedExtraActivationStateChanged(BattleReducedExtraActivationStateChanged & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Reduced extra activation state update targets another battle");
	pack.validateTransitionFrom(battleState.getReducedExtraActivationState(pack.side));
	battleState.setReducedExtraActivationState(pack.side, pack.state);
}

void BattleStatePackVisitor::visitSetArmorerDefiantState(SetArmorerDefiantState & pack)
{
	const auto * battle = dynamic_cast<const CBattleInfoCallback *>(&battleState);
	if(!battle)
		throw std::runtime_error("Defiant consumption requires a shared battle callback");
	pack.validateAgainst(*battle);
	battleState.setArmorerDefiantState(pack.side, pack.state);
}

void BattleStatePackVisitor::visitSetSpellResponseState(SetSpellResponseState & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Spell Response state update targets another battle");
	pack.validateTransitionFrom(battleState.getSpellResponseState(pack.side), battleState.getRound());
	battleState.setSpellResponseState(pack.side, pack.state);
}

void BattleStatePackVisitor::visitSetOverwhelmingFormulaState(SetOverwhelmingFormulaState & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Overwhelming Formula state update targets another battle");
	pack.validateTransitionFrom(battleState.getOverwhelmingFormulaState(pack.side));
	battleState.setOverwhelmingFormulaState(pack.side, pack.state);
}

void BattleStatePackVisitor::visitSetBattlecraftMasteryAward(SetBattlecraftMasteryAward & pack)
{
	if(pack.battleID != battleState.getBattleID())
		throw std::runtime_error("Battlefield Mastery award targets another battle");
	pack.validateTransitionFrom(battleState.getBattlecraftMasteryAwardRound(pack.side), battleState.getRound());
	battleState.awardBattlecraftMastery(pack.side, pack.unitId, pack.round, pack.action);
}

void BattleStatePackVisitor::visitCatapultAttack(CatapultAttack & pack)
{
	const auto * town = battleState.getDefendedTown();
	if(!town)
		throw std::runtime_error("CatapultAttack without town!");

	if(town->fortificationsLevel().wallsHealth == 0)
		throw std::runtime_error("CatapultAttack without walls!");

	const auto damage = pack.structuralDamage > 0 ? pack.structuralDamage : pack.damageDealt;
	if(const auto hp = battleState.getWallStructuralHP(pack.attackedPart); hp > 0)
		battleState.setWallStructuralHP(pack.attackedPart, hp - damage);
	else
	{
		auto newWallState = SiegeInfo::applyDamage(battleState.getWallState(pack.attackedPart), damage);
		battleState.setWallState(pack.attackedPart, newWallState);
	}

	if(pack.killedTowerShooter != -1)
		battleState.removeUnit(pack.killedTowerShooter);
}

void BattleStatePackVisitor::visitBattleObstaclesChanged(BattleObstaclesChanged & pack)
{
	switch(pack.change.operation)
	{
		case BattleChanges::EOperation::REMOVE:
			battleState.removeObstacle(pack.change.id);
			break;
		case BattleChanges::EOperation::ADD:
			battleState.addObstacle(pack.change);
			break;
		case BattleChanges::EOperation::UPDATE:
			battleState.updateObstacle(pack.change);
			break;
		default:
			throw std::runtime_error("Unknown obstacle operation");
	}
}

void BattleStatePackVisitor::visitSetStackEffect(SetStackEffect & pack)
{
	pack.validateConfusionMarkers();
	const SpellID hydrasVitality(SpellID::decode("new-horizons:hydrasVitality"));
	const auto hydrasVitalitySource = BonusSourceID(hydrasVitality);
	std::set<uint32_t> capacityAffectedStacks;
	auto collectCapacityChanges = [&capacityAffectedStacks, &hydrasVitalitySource](const auto & changes)
	{
		for(const auto & stackEffects : changes)
			if(std::ranges::any_of(stackEffects.second, [&hydrasVitalitySource](const Bonus & bonus)
			{
				return bonus.type == BonusType::STACK_HEALTH
					|| (bonus.type == BonusType::HP_REGENERATION
						&& bonus.source == BonusSource::SPELL_EFFECT && bonus.sid == hydrasVitalitySource);
			}))
				capacityAffectedStacks.insert(stackEffects.first);
	};
	collectCapacityChanges(pack.toRemove);
	collectCapacityChanges(pack.toUpdate);
	collectCapacityChanges(pack.toAdd);

	for(const auto & stackData : pack.toRemove)
		battleState.removeUnitBonus(stackData.first, stackData.second);

	for(const auto & stackData : pack.toUpdate)
		battleState.updateUnitBonus(stackData.first, stackData.second);

	for(const auto & stackData : pack.toAdd)
		battleState.addUnitBonus(stackData.first, stackData.second);

	if(capacityAffectedStacks.empty())
		return;
	const auto hydrasCapacity = Selector::source(BonusSource::SPELL_EFFECT, hydrasVitalitySource)
		.And(Selector::type()(BonusType::STACK_HEALTH));
	const auto hydrasRegeneration = Selector::source(BonusSource::SPELL_EFFECT, hydrasVitalitySource)
		.And(Selector::type()(BonusType::HP_REGENERATION));
	for(const auto * unit : battleState.getUnitsIf([&capacityAffectedStacks](const battle::Unit * candidate)
		{
			return candidate && capacityAffectedStacks.contains(candidate->unitId());
		}))
	{
		auto state = unit->acquireState();
		if(!state->health.isCapacityHealthTracking())
			continue;
		state->normalizeCapacityHealth();
		if(!unit->hasBonus(hydrasCapacity) && !unit->hasBonus(hydrasRegeneration))
			state->clearCapacityHealthReference();
		battleState.updateUnit(unit->unitId(), state->save(), 0);
	}
}

void BattleStatePackVisitor::visitStacksInjured(StacksInjured & pack)
{
	for(const BattleStackAttacked & stack : pack.stacks)
	{
		battleState.updateUnit(stack.newState.id, stack.newState.data, stack.newState.healthDelta);
	}
}

void BattleStatePackVisitor::visitBattleUnitsChanged(BattleUnitsChanged & pack)
{
	for(auto & elem : pack.changedStacks)
	{
		switch(elem.operation)
		{
			case BattleChanges::EOperation::UPDATE:
				battleState.updateUnit(elem.id, elem.data, elem.healthDelta);
				break;
			case BattleChanges::EOperation::REMOVE:
				battleState.removeUnit(elem.id);
				break;
			case BattleChanges::EOperation::ADD:
				battleState.addUnit(elem.id, elem.data);
				break;
			default:
				throw std::runtime_error("Unknown unit operation");
				break;
		}
	}
}
