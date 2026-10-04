/*
 * BattleResultProcessor.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleResultProcessor.h"
#include "battle/BattleInfo.h"

#include "../CGameHandler.h"
#include "../TurnTimerHandler.h"
#include "../processors/HeroPoolProcessor.h"
#include "../queries/QueriesProcessor.h"
#include "../queries/BattleQueries.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/CStack.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/entities/artifact/ArtifactUtils.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactFittingSet.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/spells/NewHorizonsMagic.h"

#include <vcmi/spells/Spell.h>

namespace
{
constexpr auto wisdomSkillId = "new-horizons:wisdom";
constexpr auto manaConservationPerkId = "new-horizons:wisdom.manaConservation";

struct RaisedArmyAddition
{
	SlotID slot;
	CreatureID creature;
	TQuantity count;
};

using RaisedArmyPlan = std::vector<RaisedArmyAddition>;

/// Return a detached source-species view for post-battle accounting while a
/// replacement form is active. Result projection must not change the live
/// battle stack's form or health.
std::shared_ptr<battle::CUnitState> acquireOriginalFormState(const CStack & stack)
{
	if(!stack.hasBattleForm())
		return {};

	auto state = stack.acquireState();
	state->endBattleForm();
	return state;
}

/// Return a Skeleton upgrade offered by a currently owned Necropolis town
/// whose corresponding upgrade dwelling is already built.
CreatureID availableNecropolisSkeletonUpgrade(const PlayerState * ownerState)
{
	if(!ownerState)
		return CreatureID::NONE;

	const auto skeleton = CreatureID(CreatureID::decode("core:skeleton"));
	const auto * skeletonType = (*LIBRARY->creh)[skeleton];
	if(!skeletonType)
		return CreatureID::NONE;

	for(const auto * town : ownerState->getTowns())
	{
		if(!town || town->getFactionID() != FactionID::NECROPOLIS)
			continue;

		const auto * townType = town->getTown();
		if(!townType)
			continue;

		const auto levels = std::min(townType->creatures.size(), town->creatures.size());
		for(size_t level = 0; level < levels; ++level)
		{
			const auto & configuredCreatures = townType->creatures[level];
			const auto & offeredCreatures = town->creatures[level].second;
			for(size_t upgrade = 1; upgrade < configuredCreatures.size(); ++upgrade)
			{
				const auto creature = configuredCreatures[upgrade];
				if(!skeletonType->upgrades.contains(creature) || !vstd::contains(offeredCreatures, creature))
					continue;

				const auto dwelling = BuildingID::getDwellingFromLevel(
					static_cast<int>(level), static_cast<int>(upgrade));
				if(!dwelling.hasValue() || !townType->buildings.count(dwelling) || !town->hasBuilt(dwelling))
					continue;

				if(LIBRARY->creatures()->getById(creature))
					return creature;
			}
		}
	}
	return CreatureID::NONE;
}

bool originalArmyContainsLivingChampion(const CArmedInstance * army,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules)
{
	if(!army)
		return false;

	for(const auto & entry : army->Slots())
	{
		const auto & stack = entry.second;
		if(!stack || stack->getCount() <= 0)
			continue;

		const auto creatureId = stack->getCreatureID();
		const auto * creature = creatureId.toCreature();
		if(!creature || creature->hasBonusOfType(BonusType::UNDEAD)
			|| creature->hasBonusOfType(BonusType::NON_LIVING)
			|| creature->hasBonusOfType(BonusType::MECHANICAL))
			continue;

		const auto category = newHorizonsCreatures::creatureCategoryView(categoryRules, creatureId);
		if(category && category->category == newHorizonsCreatures::CreatureCategory::CHAMPION)
			return true;
	}
	return false;
}

/// Select the nearest currently owned Necropolis by the same squared-distance
/// and first-on-tie convention used by the adventure spell town selector.
const CGTownInstance * nearestOwnedNecropolisTown(const PlayerState * ownerState, PlayerColor owner,
	const int3 & origin)
{
	if(!ownerState)
		return nullptr;

	const CGTownInstance * nearest = nullptr;
	ui32 nearestDistance = std::numeric_limits<ui32>::max();
	for(const auto * town : ownerState->getTowns())
	{
		if(!town || town->getOwner() != owner || town->getFactionID() != FactionID::NECROPOLIS)
			continue;

		const ui32 distance = town->visitablePos().dist2dSQ(origin);
		if(!nearest || distance < nearestDistance)
		{
			nearest = town;
			nearestDistance = distance;
		}
	}
	return nearest;
}

bool validRaisedArmyOutputs(const std::vector<std::pair<CreatureID, int64_t>> & outputs)
{
	for(const auto & [creature, count] : outputs)
		if(count < 0 || (count > 0 && (!creature.hasValue() || !creature.toCreature())))
			return false;
	return true;
}

bool validRaisedArmyState(const CArmedInstance & army)
{
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		if(const auto * stack = army.getStackPtr(SlotID(i)))
		{
			if(stack->getCount() < 0 || !stack->getCreatureID().hasValue()
				|| !stack->getCreatureID().toCreature())
				return false;
		}
	}
	return true;
}

// Resolve every output against one projected army before emitting any packs.
// A matching stack may be full even though another slot can admit the reward.
std::optional<RaisedArmyPlan> planRaisedArmy(const CArmedInstance & army,
	const std::vector<std::pair<CreatureID, int64_t>> & outputs)
{
	struct ProjectedSlot
	{
		CreatureID creature = CreatureID::NONE;
		int64_t count = 0;
	};
	std::array<ProjectedSlot, GameConstants::ARMY_SIZE> slots;
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
		if(const auto * stack = army.getStackPtr(SlotID(i)))
		{
			if(stack->getCount() < 0)
				return std::nullopt;
			slots[i] = {stack->getCreatureID(), stack->getCount()};
		}

	RaisedArmyPlan plan;
	for(const auto & [creature, count] : outputs)
	{
		if(count < 0 || (count > 0 && !creature.hasValue()))
			return std::nullopt;
		if(count == 0)
			continue;
		int64_t capacity = std::numeric_limits<TQuantity>::max();
		if(const auto * hero = dynamic_cast<const CGHeroInstance *>(&army))
		{
			if(const auto leadership = hero->getLeadershipSlotCapacity(creature))
				capacity = std::min<int64_t>(capacity, leadership->maximum);
		}
		int64_t remaining = count;
		// Fill every matching stack first, then reserve distinct empty slots.
		for(const bool empty : {false, true})
			for(int i = 0; i < GameConstants::ARMY_SIZE && remaining > 0; ++i)
			{
				auto & slot = slots[i];
				if(empty ? slot.creature.hasValue() : slot.creature != creature)
					continue;
				const int64_t amount = std::min(remaining, std::max<int64_t>(0, capacity - slot.count));
				if(amount == 0)
					continue;
				plan.push_back({SlotID(i), creature, static_cast<TQuantity>(amount)});
				slot.creature = creature;
				slot.count += amount;
				remaining -= amount;
			}
		if(remaining > 0)
			return std::nullopt;
	}
	return plan;
}

void applyRaisedArmy(CGameHandler & handler, const CArmedInstance & army, const RaisedArmyPlan & plan)
{
	for(const auto & addition : plan)
	{
		const StackLocation location(army.id, addition.slot);
		const bool applied = army.hasStackAtSlot(addition.slot)
			? handler.changeStackCount(location, addition.count, ChangeValueMode::RELATIVE)
			: handler.insertNewStack(location, addition.creature.toCreature(), addition.count);
		// The simulation thread applies this preflighted plan without yielding.
		// A rejection is an internal error, not an ordinary capacity failure.
		if(!applied)
			throw std::runtime_error("Preflighted Necromancy army addition was rejected");
	}
}
}

BattleResultProcessor::BattleResultProcessor(CGameHandler * gameHandler)
	: gameHandler(gameHandler)
{
}

CasualtiesAfterBattle::CasualtiesAfterBattle(const CBattleInfoCallback & battle, BattleSide sideInBattle):
	army(battle.battleGetArmyObject(sideInBattle))
{
	heroWithDeadCommander = ObjectInstanceID();
	std::set<uint32_t> gatedUnitIds;
	if(const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle()))
		for(const auto & gated : concrete->getSide(sideInBattle).gatedDemonicStacks)
			gatedUnitIds.insert(gated.unitId);

	PlayerColor color = battle.sideToPlayer(sideInBattle);

	auto allStacks = battle.battleGetStacksIf([color, &gatedUnitIds](const CStack * stack){

		if(gatedUnitIds.contains(stack->unitId()))
			return false;

		if (stack->summoned)//don't take into account temporary summoned stacks
			return false;

		if(stack->unitOwner() != color) //remove only our stacks
			return false;

		if (stack->isTurret())
			return false;

		return true;
	});

	for(const CStack * stConst : allStacks)
	{
		// Keep the historical cleanup path for ordinary stacks. For active forms,
		// project source-species casualties on a detached state so result
		// accounting cannot alter the live battle unit.
		auto originalFormState = acquireOriginalFormState(*stConst);
		battle::CUnitState * st = originalFormState.get();
		if(!st)
			st = const_cast<CStack *>(stConst);

		logGlobal->debug("Calculating casualties for %s", stConst->nodeName());

		st->health.takeResurrected();

		if(st->unitSlot() == SlotID::WAR_MACHINES_SLOT)
		{
			auto warMachine = st->unitType()->warMachine;

			if(warMachine == ArtifactID::NONE)
			{
				logGlobal->error("Invalid creature in war machine virtual slot. Stack: %s", stConst->nodeName());
			}
			//catapult artifact remain even if "creature" killed in siege
			else if(warMachine != ArtifactID::CATAPULT && st->getCount() <= 0)
			{
				logGlobal->debug("War machine has been destroyed");
				auto hero = dynamic_cast<const CGHeroInstance*> (army);
				if (hero)
					removedWarMachines.push_back (ArtifactLocation(hero->id, hero->getArtPos(warMachine, true)));
				else
					logGlobal->error("War machine in army without hero");
			}
		}
		else if(st->unitSlot() == SlotID::SUMMONED_SLOT_PLACEHOLDER)
		{
			if(st->alive() && st->getCount() > 0)
			{
				logGlobal->debug("Permanently summoned %d units.", st->getCount());
				const CreatureID summonedType = st->creatureId();
				summoned[summonedType] += st->getCount();
			}
		}
		else if(st->unitSlot() == SlotID::COMMANDER_SLOT_PLACEHOLDER)
		{
			if (nullptr == stConst->base)
			{
				logGlobal->error("Stack with no base in commander slot. Stack: %s", stConst->nodeName());
			}
			else
			{
				auto c = dynamic_cast <const CCommanderInstance *>(stConst->base);
				if(c)
				{
					auto h = dynamic_cast <const CGHeroInstance *>(army);
					if(h && h->getCommander() == c && (st->getCount() == 0 || !st->alive()))
					{
						logGlobal->debug("Commander is dead.");
						heroWithDeadCommander = army->id; //TODO: unify commander handling
					}
				}
				else
					logGlobal->error("Stack with invalid instance in commander slot. Stack: %s", stConst->nodeName());
			}
		}
		else if(stConst->base && !army->slotEmpty(st->unitSlot()))
		{
			logGlobal->debug("Count: %d; base count: %d", st->getCount(), army->getStackCount(st->unitSlot()));
			if(st->getCount() == 0 || !st->alive())
			{
				logGlobal->debug("Stack has been destroyed.");
				StackLocation sl(army->id, st->unitSlot());
				newStackCounts.push_back(TStackAndItsNewCount(sl, 0));
			}
			else if(st->getCount() != army->getStackCount(st->unitSlot()))
			{
				logGlobal->debug("Stack size changed: %d -> %d units.", army->getStackCount(st->unitSlot()), st->getCount());
				StackLocation sl(army->id, st->unitSlot());
				newStackCounts.push_back(TStackAndItsNewCount(sl, st->getCount()));
			}
		}
		else
		{
			logGlobal->warn("Unable to process stack: %s", stConst->nodeName());
		}
	}
}

void CasualtiesAfterBattle::updateArmy(CGameHandler *gh)
{
	if (gh->gameInfo().getObjInstance(army->id) == nullptr)
		throw std::runtime_error("Object " + army->getObjectNameTextID() + " is not on the map!");

	for (const auto & ncount : newStackCounts)
	{
		if (ncount.second > 0)
			gh->changeStackCount(ncount.first, ncount.second, ChangeValueMode::ABSOLUTE);
		else
			gh->eraseStack(ncount.first, true);
	}
	for (auto summoned_iter : summoned)
	{
		SlotID slot = army->getSlotFor(summoned_iter.first);
		if (slot.validSlot())
		{
			StackLocation location(army->id, slot);
			gh->addToSlot(location, summoned_iter.first.toCreature(), summoned_iter.second);
		}
		else
		{
			//even if it will be possible to summon anything permanently it should be checked for free slot
			//necromancy is handled separately
			gh->complain("No free slot to put summoned creature");
		}
	}
	for (auto al : removedWarMachines)
	{
		gh->removeArtifact(al);
	}
	if (heroWithDeadCommander != ObjectInstanceID())
	{
		SetCommanderProperty scp;
		scp.heroid = heroWithDeadCommander;
		scp.which = SetCommanderProperty::ALIVE;
		scp.amount = 0;
		gh->sendAndApply(scp);
	}
}

FinishingBattleHelper::FinishingBattleHelper(const CBattleInfoCallback & info, const BattleResult & result, int remainingBattleQueriesCount)
{
	const auto attackerHero = info.getBattle()->getSideHero(BattleSide::ATTACKER);
	const auto defenderHero = info.getBattle()->getSideHero(BattleSide::DEFENDER);
	if (result.winner == BattleSide::ATTACKER)
	{
		winnerId = attackerHero ? attackerHero->id : ObjectInstanceID::NONE;
		loserId = defenderHero ? defenderHero->id : ObjectInstanceID::NONE;
		victor = info.getBattle()->getSidePlayer(BattleSide::ATTACKER);
		loser = info.getBattle()->getSidePlayer(BattleSide::DEFENDER);
	}
	else
	{
		winnerId = defenderHero ? defenderHero->id : ObjectInstanceID::NONE;
		loserId = attackerHero ? attackerHero->id : ObjectInstanceID::NONE;
		victor = info.getBattle()->getSidePlayer(BattleSide::DEFENDER);
		loser = info.getBattle()->getSidePlayer(BattleSide::ATTACKER);
	}

	winnerSide = result.winner;

	this->remainingBattleQueriesCount = remainingBattleQueriesCount;
}

void BattleResultProcessor::endBattle(const CBattleInfoCallback & battle)
{
	auto const & giveExp = [&battle](BattleResult &r)
	{
		if (r.winner == BattleSide::NONE)
		{
			// draw
			return;
		}
		r.exp[BattleSide::ATTACKER] = 0;
		r.exp[BattleSide::DEFENDER] = 0;
		for (auto i = r.casualties[battle.otherSide(r.winner)].begin(); i!=r.casualties[battle.otherSide(r.winner)].end(); i++)
		{
			r.exp[r.winner] += i->first.toCreature()->valOfBonuses(BonusType::STACK_HEALTH) * i->second;
		}
	};

	LOG_TRACE(logGlobal);

	auto * battleResult = battleResults.at(battle.getBattle()->getBattleID()).get();
	const auto * heroAttacker = battle.battleGetFightingHero(BattleSide::ATTACKER);
	const auto * heroDefender = battle.battleGetFightingHero(BattleSide::DEFENDER);

	//Fill BattleResult structure with exp info
	giveExp(*battleResult);

	if (battleResult->result == EBattleResult::NORMAL) // give 500 exp for defeating hero, unless he escaped
	{
		if(heroAttacker)
			battleResult->exp[BattleSide::DEFENDER] += 500;
		if(heroDefender)
			battleResult->exp[BattleSide::ATTACKER] += 500;
	}

	// Give 500 exp to winner if a town was conquered during the battle
	const auto * defendedTown = battle.battleGetDefendedTown();
	if (defendedTown && battleResult->winner == BattleSide::ATTACKER)
		battleResult->exp[BattleSide::ATTACKER] += 500;

	const auto * concreteBattle = dynamic_cast<const BattleInfo *>(battle.getBattle());
	const auto fieldStudyAdditionalPercent = [&battle, battleResult, concreteBattle](
		BattleSide side, const CGHeroInstance * hero) -> int32_t
	{
		if(!hero || battleResult->winner != side
			|| !hero->hasActivePerk("new-horizons:learning", "new-horizons:learning.fieldStudy")
			|| !concreteBattle)
			return 0;

		const auto & heroArmy = concreteBattle->getSide(side);
		const auto & opponentArmy = concreteBattle->getSide(battle.otherSide(side));
		if(!heroArmy.initialArmyValue || !opponentArmy.initialArmyValue
			|| !(opponentArmy.heroID.hasValue() || opponentArmy.initialArmyIsWandering))
			return 0;

		return *opponentArmy.initialArmyValue > *heroArmy.initialArmyValue ? 25 : 0;
	};

	if(heroAttacker)
		battleResult->exp[BattleSide::ATTACKER] = heroAttacker->calculateXp(
			battleResult->exp[BattleSide::ATTACKER], fieldStudyAdditionalPercent(BattleSide::ATTACKER, heroAttacker));
	if(heroDefender)
		battleResult->exp[BattleSide::DEFENDER] = heroDefender->calculateXp(
			battleResult->exp[BattleSide::DEFENDER], fieldStudyAdditionalPercent(BattleSide::DEFENDER, heroDefender));

	auto attackerQuery = gameHandler->queries->topQuery(battle.sideToPlayer(BattleSide::ATTACKER));

	QueryPtr battleQuery;
	const auto * defenderPlayer = gameHandler->gameInfo().getPlayerState(battle.getBattle()->getSidePlayer(BattleSide::DEFENDER));
	bool isDefenderHuman = defenderPlayer && defenderPlayer->isHuman();
	if(gameHandler->queries->queryAs<CBattleQuery>(attackerQuery))
		battleQuery = attackerQuery;
	else if(isDefenderHuman)
	{
		auto defenderQuery = gameHandler->queries->topQuery(battle.sideToPlayer(BattleSide::DEFENDER));
		if(gameHandler->queries->queryAs<CBattleQuery>(defenderQuery))
			battleQuery = defenderQuery;
	}

	if (!battleQuery)
	{
		logGlobal->error("Cannot find battle query!");
		gameHandler->complain("Player " + std::to_string(battle.sideToPlayer(BattleSide::ATTACKER).getNum()) + " has no battle query at the top!");
		return;
	}

	auto * typedBattleQuery = gameHandler->queries->queryAs<CBattleQuery>(battleQuery);
	typedBattleQuery->result = std::make_optional(*battleResult);

	//Check how many battle gameHandler->queries were created (number of players blocked by battle)
	const int queriedPlayers = gameHandler->queries->countQuery(battleQuery);

	assert(finishingBattles.count(battle.getBattle()->getBattleID()) == 0);
	finishingBattles[battle.getBattle()->getBattleID()] = std::make_unique<FinishingBattleHelper>(battle, *battleResult, queriedPlayers);

	// in battles against neutrals, 1st player can ask to replay battle manually
	const auto * attackerPlayer = gameHandler->gameInfo().getPlayerState(battle.getBattle()->getSidePlayer(BattleSide::ATTACKER));
	bool isAttackerHuman = attackerPlayer && attackerPlayer->isHuman();
	bool onlyOnePlayerHuman = isAttackerHuman != isDefenderHuman;
	// in battles against neutrals attacker can ask to replay battle manually, additionally in battles against AI player human side can also ask for replay
	if(onlyOnePlayerHuman)
	{
		auto battleDialogQuery = std::make_shared<CBattleDialogQuery>(gameHandler, battle.getBattle(), typedBattleQuery->result);
		battleResult->queryID = battleDialogQuery->queryID;
		gameHandler->queries->addQuery(battleDialogQuery);
	}
	else
		battleResult->queryID = QueryID::NONE;

	//set same battle result for all gameHandler->queries
	for(const auto & q : gameHandler->queries->allQueries())
	{
		auto * otherBattleQuery = gameHandler->queries->queryAs<CBattleQuery>(q);
		if(otherBattleQuery && otherBattleQuery->battleID == battle.getBattle()->getBattleID())
			otherBattleQuery->result = typedBattleQuery->result;
	}

	gameHandler->turnTimerHandler->onBattleEnd(battle.getBattle()->getBattleID());
	gameHandler->sendAndApply(*battleResult);

	if (battleResult->queryID == QueryID::NONE)
		endBattleConfirm(battle);
}

void BattleResultProcessor::endBattleConfirm(const CBattleInfoCallback & battle)
{
	auto attackerQuery = gameHandler->queries->topQuery(battle.sideToPlayer(BattleSide::ATTACKER));

	QueryPtr battleQueryPtr;
	auto defenderPlayer = battle.sideToPlayer(BattleSide::DEFENDER);
	if(gameHandler->queries->queryAs<CBattleQuery>(attackerQuery))
		battleQueryPtr = attackerQuery;
	else if(defenderPlayer.isValidPlayer())
	{
		auto defenderQuery = gameHandler->queries->topQuery(battle.sideToPlayer(BattleSide::DEFENDER));
		if(gameHandler->queries->queryAs<CBattleQuery>(defenderQuery))
			battleQueryPtr = defenderQuery;
	}

	auto * typedBattleQuery = gameHandler->queries->queryAs<CBattleQuery>(battleQueryPtr);
	if(!typedBattleQuery)
	{
		logGlobal->trace("No battle query, battle end was confirmed by another player");
		return;
	}

	const auto * battleResult = battleResults.at(battle.getBattle()->getBattleID()).get();
	const auto * finishingBattle = finishingBattles.at(battle.getBattle()->getBattleID()).get();

	//calculate casualties before deleting battle
	CasualtiesAfterBattle cab1(battle, BattleSide::ATTACKER);
	CasualtiesAfterBattle cab2(battle, BattleSide::DEFENDER);

	cab1.updateArmy(gameHandler);
	cab2.updateArmy(gameHandler); //take casualties after battle is deleted

	const auto attackerHero = battle.battleGetFightingHero(BattleSide::ATTACKER);
	const auto defenderHero = battle.battleGetFightingHero(BattleSide::DEFENDER);

	// Demonic Reserve is a real owned army pool. Replace the strategic snapshot
	// with the uncommitted reserve plus every surviving gated stack (and every
	// still-pending gate) before the battle object is removed.
	if(const auto * concrete = dynamic_cast<const BattleInfo *>(battle.getBattle()))
	{
		for(const auto sideId : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			const auto * hero = battle.battleGetFightingHero(sideId);
			if(!hero)
				continue;
			const auto & side = concrete->getSide(sideId);
			if(side.demonicReserve.empty() && side.pendingDemonicGates.empty()
				&& side.gatedDemonicStacks.empty() && hero->getDemonicReserve().empty())
				continue;
			auto reserve = side.demonicReserve;
			const bool endlessLegion = !finishingBattle->isDraw() && finishingBattle->winnerSide == sideId
				&& hero->hasActivePerk("new-horizons:demonicGating",
					"new-horizons:demonicGating.endlessLegion");
			for(const auto & pending : side.pendingDemonicGates)
				reserve[pending.creature] += pending.count;
			for(const auto & gated : side.gatedDemonicStacks)
			{
				const auto * stack = battle.battleGetStackByID(gated.unitId, false);
				auto originalFormState = stack ? acquireOriginalFormState(*stack) : nullptr;
				if(originalFormState)
					originalFormState->health.takeResurrected();
				const battle::CUnitState * resultState = originalFormState.get();
				if(!resultState)
					resultState = stack;
				const TQuantity survivors = resultState && resultState->alive() ? resultState->getCount() : 0;
				if(survivors > 0)
					reserve[gated.creature] += survivors;
				if(endlessLegion)
				{
					const auto category = battle.battleGetCreatureCategory(gated.creature);
					if(category && category->category != newHorizonsCreatures::CreatureCategory::CHAMPION)
					{
						const TQuantity restored = gated.endlessLegionRestoration(survivors);
						if(restored > 0)
							reserve[gated.creature] += restored;
					}
				}
			}
			SetNewHorizonsDemonicReserve update;
			update.heroId = hero->id;
			update.reserve = std::move(reserve);
			gameHandler->sendAndApply(update);
		}
	}


	//give exp
	if(!finishingBattle->isDraw() && battleResult->exp[finishingBattle->winnerSide])
	{
		const auto winnerHero = battle.battleGetFightingHero(finishingBattle->winnerSide);

		gameHandler->giveStackExperience(battle.battleGetArmyObject(finishingBattle->winnerSide), battleResult->exp[finishingBattle->winnerSide]);
		if (winnerHero)
		{
			gameHandler->giveExperienceWithoutLevelUp(winnerHero, battleResult->exp[finishingBattle->winnerSide]);
			typedBattleQuery->heroesWithDeferredLevelUp.push_back(winnerHero->id);
		}
	}

	// Add statistics
	if(!finishingBattle->isDraw())
	{
		const CGHeroInstance * loserHero = battle.battleGetFightingHero(CBattleInfoEssentials::otherSide(finishingBattle->winnerSide));
		const CGHeroInstance * strongestHero = nullptr;

		if (loserHero != nullptr)
		{
			for(auto & hero : gameHandler->gameState().getPlayerState(finishingBattle->loser)->getHeroes())
				if(!strongestHero || hero->exp > strongestHero->exp)
					strongestHero = hero;
			if(strongestHero->id == finishingBattle->loserId && strongestHero->level > 5 && finishingBattle->victor.isValidPlayer())
				gameHandler->statistics->getPlayerAccumulator(finishingBattle->victor).lastDefeatedStrongestHeroDay = gameHandler->gameState().getCalendar().getCurrentDay();
		}
	}

	auto attackerPlayer = battle.sideToPlayer(BattleSide::ATTACKER);
	auto isAttackerNeutral = attackerPlayer == PlayerColor::NEUTRAL;
	auto isDefenderNeutral = defenderPlayer == PlayerColor::NEUTRAL;

	if(isAttackerNeutral || isDefenderNeutral)
	{
		if(!isAttackerNeutral)
			gameHandler->statistics->getPlayerAccumulator(attackerPlayer).numBattlesNeutral++;
		if(!isDefenderNeutral)
			gameHandler->statistics->getPlayerAccumulator(defenderPlayer).numBattlesNeutral++;
		if(!finishingBattle->isDraw())
		{
			auto winnerPlayer = battle.sideToPlayer(finishingBattle->winnerSide);
			auto isWinnerNeutral = winnerPlayer == PlayerColor::NEUTRAL;
			if (!isWinnerNeutral)
				gameHandler->statistics->getPlayerAccumulator(winnerPlayer).numWinBattlesNeutral++;
		}
	}
	else
	{
		gameHandler->statistics->getPlayerAccumulator(attackerPlayer).numBattlesPlayer++;
		gameHandler->statistics->getPlayerAccumulator(defenderPlayer).numBattlesPlayer++;
		if(!finishingBattle->isDraw())
		{
			auto winnerPlayer = battle.sideToPlayer(finishingBattle->winnerSide);
			gameHandler->statistics->getPlayerAccumulator(winnerPlayer).numWinBattlesPlayer++;
		}
	}

	BattleResultAccepted raccepted;
	raccepted.battleID = battle.getBattle()->getBattleID();
	raccepted.heroResult[BattleSide::ATTACKER].heroID = attackerHero ? attackerHero->id : ObjectInstanceID::NONE;
	raccepted.heroResult[BattleSide::DEFENDER].heroID = defenderHero ? defenderHero->id : ObjectInstanceID::NONE;
	raccepted.heroResult[BattleSide::ATTACKER].armyID = battle.battleGetArmyObject(BattleSide::ATTACKER)->id;
	raccepted.heroResult[BattleSide::DEFENDER].armyID = battle.battleGetArmyObject(BattleSide::DEFENDER)->id;
	raccepted.heroResult[BattleSide::ATTACKER].exp = battleResult->exp[BattleSide::ATTACKER];
	raccepted.heroResult[BattleSide::DEFENDER].exp = battleResult->exp[BattleSide::DEFENDER];
	raccepted.winnerSide = finishingBattle->winnerSide;
	gameHandler->sendAndApply(raccepted);

	gameHandler->queries->popIfTop(battleQueryPtr); // Workaround to remove battle query for AI case. TODO Think of a cleaner solution.
	//--> continuation (battleFinalize) occurs on removing query
}

bool BattleResultProcessor::applyNewHorizonsNecromancy(const BattleResult & result,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules,
	int32_t initialMana, const CGHeroInstance * winnerHero,
	BattleResultsApplied & resultsApplied)
{
	if(!winnerHero || !winnerHero->usesNewHorizonsNecromancy())
		return false;

	const auto skeleton = CreatureID(CreatureID::decode("core:skeleton"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	const auto wight = CreatureID(CreatureID::decode("core:wight"));
	const auto boneDragon = CreatureID(CreatureID::decode("core:boneDragon"));
	const auto losingSide = CBattleInfoEssentials::otherSide(result.winner);
	const auto & eligible = result.necromancyEligibilityCaptured
		? result.necromancyEligibleCasualties[losingSide]
		: result.casualties[losingSide];
	const auto eligibleCount = newHorizonsNecromancy::countLivingEligibleCasualties(eligible);
	const auto eligibleCoreCount = newHorizonsNecromancy::countLivingEligibleCoreCasualties(eligible, categoryRules);
	const auto eligibleEliteCount = newHorizonsNecromancy::countLivingEligibleEliteCasualties(eligible, categoryRules);

	const bool boneCollector = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::BONE_COLLECTOR_ID);
	const bool corpsePreservation = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
	const bool darkConversion = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DARK_CONVERSION_ID);
	const bool soulHarvester = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::SOUL_HARVESTER_ID);
	const bool deathLord = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DEATH_LORD_ID);
	const bool graveKnowledge = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	const bool masterOfBones = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::MASTER_OF_BONES_ID);
	const bool ossuary = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::OSSUARY_ID);
	const bool lordOfTheDead = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::LORD_OF_THE_DEAD_ID);
	const bool blackHarvest = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::BLACK_HARVEST_ID);
	const auto * ownerState = masterOfBones || ossuary
		? gameHandler->gameInfo().getPlayerState(winnerHero->tempOwner)
		: nullptr;
	const auto skeletonOutput = masterOfBones
		? availableNecropolisSkeletonUpgrade(ownerState)
		: CreatureID::NONE;
	newHorizonsNecromancy::SpecialCasualtyCounts nonlivingCasualties;
	newHorizonsNecromancy::SpecialCasualtyCounts undeadCasualties;
	if(result.necromancySpecialEligibilityCaptured)
	{
		if(deathLord)
			nonlivingCasualties = newHorizonsNecromancy::countEligibleNonlivingCasualties(
				result.necromancyNonlivingEligibleCasualties[losingSide], categoryRules);
		if(graveKnowledge)
			undeadCasualties = newHorizonsNecromancy::countEligibleUndeadCasualties(
				result.necromancyUndeadEligibleCasualties[losingSide], categoryRules);
	}

	const bool hasTwoPoolSpellPoints = newHorizonsMagic::spellPointRulesActive(winnerHero->getMagicRules());
	const int32_t currentNormal = winnerHero->getNormalSpellPoints();
	const int32_t postBattleMana = hasTwoPoolSpellPoints
		? currentNormal
		: std::min<int32_t>(currentNormal, initialMana);
	// Black Harvest uses remaining Normal capacity. Combat-only Buffer never
	// suppresses a recovery that fits in Normal.
	auto summary = newHorizonsNecromancy::resolve(winnerHero->getNewHorizonsNecromancyRank(), eligibleCount, eligibleCoreCount,
		boneCollector, corpsePreservation, darkConversion,
		true, true, postBattleMana,
		blackHarvest ? winnerHero->manaLimit() : postBattleMana, eligibleEliteCount, soulHarvester, true, skeletonOutput,
		nonlivingCasualties, undeadCasualties, lordOfTheDead,
		result.necromancyDefeatedArmyHadLivingChampion, true,
		winnerHero->getNewHorizonsNecromancyAmplifierBonusPercent());
	if(!summary.active)
		return false;

	const auto raisedSkeleton = summary.skeletonCreature.hasValue() ? summary.skeletonCreature : skeleton;
	const std::vector<std::pair<CreatureID, int64_t>> outputs = {
		{raisedSkeleton, summary.skeletonsRaised},
		{zombie, summary.zombiesRaised},
		{wight, summary.wightsRaised},
		{boneDragon, summary.boneDragonsRaised}};
	if(!validRaisedArmyOutputs(outputs))
		throw std::runtime_error("Invalid Necromancy army output");
	if(!validRaisedArmyState(*winnerHero))
		throw std::runtime_error("Invalid winner army state during Necromancy resolution");
	const bool hasRaisedOutput = summary.skeletonsRaised > 0 || summary.zombiesRaised > 0
		|| summary.wightsRaised > 0 || summary.boneDragonsRaised > 0;

	auto acceptedPlan = planRaisedArmy(*winnerHero, outputs);
	const CArmedInstance * receivingArmy = winnerHero;
	const CGTownInstance * destinationTown = nullptr;
	if(!acceptedPlan && ossuary && hasRaisedOutput)
	{
		// Ossuary has one deterministic destination attempt. Do not search farther
		// towns if the nearest owned Necropolis cannot accept the complete batch.
		destinationTown = nearestOwnedNecropolisTown(ownerState, winnerHero->tempOwner,
			winnerHero->visitablePos());
		// getUpperArmy is the visible garrison side (garrison Hero when present,
		// otherwise the town army); the visiting Hero is deliberately excluded.
		const auto * townArmy = destinationTown ? destinationTown->getUpperArmy() : nullptr;
		if(townArmy && townArmy->getOwner() == winnerHero->tempOwner)
		{
			if(!validRaisedArmyState(*townArmy))
				throw std::runtime_error("Invalid Necropolis army state during Ossuary resolution");
			acceptedPlan = planRaisedArmy(*townArmy, outputs);
			if(acceptedPlan)
				receivingArmy = townArmy;
		}
		else
		{
			acceptedPlan.reset();
		}
	}
	if(!acceptedPlan)
	{
		summary.applied = false;
		summary.blockedByArmyCapacity = true;
		summary.skeletonsRaised = 0;
		summary.zombiesRaised = 0;
		summary.wightsRaised = 0;
		summary.lordOfDeadSkeletonsConsumed = 0;
		summary.boneDragonsRaised = 0;
		summary.skeletonCreature = CreatureID::NONE;
		summary.darkConversionChosen = false;
		summary.manaRecovered = 0;
		summary.raisedCreature = CreatureID::NONE;
		summary.ossuaryTown = ObjectInstanceID::NONE;
		resultsApplied.necromancy = summary;
		return true;
	}

	applyRaisedArmy(*gameHandler, *receivingArmy, *acceptedPlan);
	if(destinationTown
		&& (summary.skeletonsRaised > 0 || summary.zombiesRaised > 0
			|| summary.wightsRaised > 0 || summary.boneDragonsRaised > 0))
		summary.ossuaryTown = destinationTown->id;

	// Keep the legacy descriptor useful for clients when there is one output
	// kind. Combined conversions may produce multiple output stacks; leaving the
	// legacy single-stack field empty avoids a misleading partial popup while
	// New Horizons clients consume the complete summary below.
	if(summary.skeletonsRaised > 0 && summary.zombiesRaised == 0 && summary.wightsRaised == 0
		&& summary.boneDragonsRaised == 0)
		resultsApplied.raisedStack = CStackBasicDescriptor(raisedSkeleton, summary.skeletonsRaised);
	else if(summary.zombiesRaised > 0 && summary.skeletonsRaised == 0 && summary.wightsRaised == 0
		&& summary.boneDragonsRaised == 0)
		resultsApplied.raisedStack = CStackBasicDescriptor(zombie, summary.zombiesRaised);
	else if(summary.wightsRaised > 0 && summary.skeletonsRaised == 0 && summary.zombiesRaised == 0
		&& summary.boneDragonsRaised == 0)
		resultsApplied.raisedStack = CStackBasicDescriptor(wight, summary.wightsRaised);
	else if(summary.boneDragonsRaised > 0 && summary.skeletonsRaised == 0
		&& summary.zombiesRaised == 0 && summary.wightsRaised == 0)
		resultsApplied.raisedStack = CStackBasicDescriptor(boneDragon, summary.boneDragonsRaised);
	resultsApplied.necromancy = summary;
	return true;
}

void BattleResultProcessor::battleFinalize(const BattleID & battleID, const BattleResult & result)
{
	LOG_TRACE(logGlobal);

	assert(finishingBattles.count(battleID) != 0);
	if(finishingBattles.count(battleID) == 0)
		return;

	auto & finishingBattle = finishingBattles[battleID];

	finishingBattle->remainingBattleQueriesCount--;
	logGlobal->trace("Decremented gameHandler->queries count to %d", finishingBattle->remainingBattleQueriesCount);

	if(finishingBattle->remainingBattleQueriesCount > 0)
		//Battle results will be handled when all battle gameHandler->queries are closed
		return;

	//TODO consider if we really want it to work like above. ATM each player as unblocked as soon as possible
	// but the battle consequences are applied after final player is unblocked. Hard to abuse...
	// Still, it looks like a hole.

	const auto battle = std::find_if(gameHandler->gameState().currentBattles.begin(), gameHandler->gameState().currentBattles.end(),
		[battleID](const auto & desiredBattle)
		{
			return desiredBattle->battleID == battleID;
		});
	assert(battle != gameHandler->gameState().currentBattles.end());

	const CGHeroInstance * winnerHero = nullptr;
	const CGHeroInstance * loserHero = nullptr;

	const auto attackerHero = (*battle)->battleGetFightingHero(BattleSide::ATTACKER);
	const auto defenderHero = (*battle)->battleGetFightingHero(BattleSide::DEFENDER);
	const int64_t attackerHeroManaSpent = (*battle)->getSide(BattleSide::ATTACKER).acceptedHeroManaSpent;
	const int64_t defenderHeroManaSpent = (*battle)->getSide(BattleSide::DEFENDER).acceptedHeroManaSpent;
	const auto attackerSide = (*battle)->getSidePlayer(BattleSide::ATTACKER);
	const auto defenderSide = (*battle)->getSidePlayer(BattleSide::DEFENDER);
	bool winnerHasUnitsLeft = true;

	if (!finishingBattle->isDraw())
	{
		winnerHero = (*battle)->battleGetFightingHero(finishingBattle->winnerSide);
		loserHero = (*battle)->battleGetFightingHero(CBattleInfoEssentials::otherSide(finishingBattle->winnerSide));
		winnerHasUnitsLeft = winnerHero ? winnerHero->stacksCount() > 0 || (winnerHero->getCommander() && winnerHero->getCommander()->alive) : true;
	}

	BattleResultsApplied resultsApplied;

	std::vector<ArtifactInstanceID> dyingArtifacts;

	const auto addArtifactToDischarging = [&resultsApplied, &dyingArtifacts](const std::map<ArtifactPosition, ArtSlotInfo> & artMap,
			const ObjectInstanceID & id, const std::optional<SlotID> & creature = std::nullopt)
	{
		for(const auto & [slot, slotInfo] : artMap)
		{
			auto artInst = slotInfo.getArt();
			assert(artInst);
			const auto condition = artInst->getType()->getDischargeCondition();
			if (condition == DischargeArtifactCondition::BATTLE)
			{
				if (artInst->getCharges() <= 1 && artInst->getType()->getRemoveOnDepletion())
					dyingArtifacts.push_back(artInst->getId());

				auto & discharging = resultsApplied.dischargingArtifacts.emplace_back(artInst->getId(), 1);
				discharging.artLoc.emplace(id, creature, slot);
			}
		}
	};

	if(winnerHero && winnerHasUnitsLeft)
	{
		// Eagle Eye handling
		if(auto eagleEyeLevel = winnerHero->valOfBonuses(BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT))
		{
			resultsApplied.learnedSpells.eagleEyeBonus = true;
			resultsApplied.learnedSpells.learn = 1;
			resultsApplied.learnedSpells.hid = finishingBattle->winnerId;
			for(const auto & spellId : (*battle)->getUsedSpells(CBattleInfoEssentials::otherSide(result.winner)))
			{
				const auto spell = spellId.toEntity(LIBRARY->spells());
				if(spell
					&& winnerHero->getSpellLevel(spell) <= eagleEyeLevel
					&& winnerHero->canLearnSpell(spell)
					&& gameHandler->getRandomGenerator().nextInt(99) < winnerHero->valOfBonuses(BonusType::LEARN_BATTLE_SPELL_CHANCE))
				{
					resultsApplied.learnedSpells.spells.insert(spell->getId());
				}
			}
		}

		// Growing artifacts handling
		const auto addArtifactToGrowing = [&resultsApplied](const std::map<ArtifactPosition, ArtSlotInfo> & artMap)
		{
			for(const auto & [slot, slotInfo] : artMap)
			{
				const auto artInst = slotInfo.getArt();
				assert(artInst);
				if(artInst->getType()->isGrowing())
					resultsApplied.growingArtifacts.emplace_back(artInst->getId());
			}
		};

		if(const auto commander = winnerHero->getCommander(); commander && commander->alive)
			addArtifactToGrowing(commander->artifactsWorn);
		addArtifactToGrowing(winnerHero->artifactsWorn);

		// Charged artifacts handling
		addArtifactToDischarging(winnerHero->artifactsWorn, winnerHero->id);
		if(const auto commander = winnerHero->getCommander())
			addArtifactToDischarging(commander->artifactsWorn, winnerHero->id, winnerHero->findStack(winnerHero->getCommander()));

		// Necromancy handling.  New Horizons uses a count-based, server-owned
		// resolver; legacy heroes retain the original health-weighted path.
		if(winnerHero->usesNewHorizonsNecromancy())
			applyNewHorizonsNecromancy(result, (*battle)->getCreatureCategoryRules(),
				(*battle)->getSide(result.winner).initialMana, winnerHero, resultsApplied);
		else
		{
			// Give raised units to winner, if any were raised, units will be given after casualties are taken
			resultsApplied.raisedStack = winnerHero->calculateNecromancy(result);
			if(resultsApplied.raisedStack.getCreature() && !finishingBattle->isDraw())
			{
				const auto plan = planRaisedArmy(*winnerHero,
					{{resultsApplied.raisedStack.getCreature()->getId(), resultsApplied.raisedStack.getCount()}});
				if(plan)
					applyRaisedArmy(*gameHandler, *winnerHero, *plan);
				else
					resultsApplied.raisedStack = CStackBasicDescriptor();
			}
		}
	}

	if(loserHero)
	{
		// Charged artifacts handling
		addArtifactToDischarging(loserHero->artifactsWorn, loserHero->id);
		if(const auto commander = loserHero->getCommander())
			addArtifactToDischarging(commander->artifactsWorn, loserHero->id, loserHero->findStack(loserHero->getCommander()));
	}

	// Moving artifacts handling
	if(result.result == EBattleResult::NORMAL && winnerHero && winnerHasUnitsLeft)
	{
		CArtifactFittingSet artFittingSet(*winnerHero);
		const auto addArtifactToTransfer = [&artFittingSet, &dyingArtifacts](BulkMoveArtifacts & pack, const ArtifactPosition & srcSlot, const CArtifactInstance * art)
		{
			assert(art);
			if (vstd::contains(dyingArtifacts, art->getId()))
				return; // artifact will be removed soon and can't be transferred to winner hero

			const auto dstSlot = ArtifactUtils::getArtAnyPosition(&artFittingSet, art->getTypeId());
			if(dstSlot != ArtifactPosition::PRE_FIRST)
			{
				pack.artsPack0.emplace_back(MoveArtifactInfo(srcSlot, dstSlot));
				if(ArtifactUtils::isSlotEquipment(dstSlot))
					pack.artsPack0.back().askAssemble = true;
				artFittingSet.putArtifact(dstSlot, art);
			}
		};

		if (loserHero)
		{
			BulkMoveArtifacts packHero(finishingBattle->victor, finishingBattle->loserId, finishingBattle->winnerId, false);
			packHero.srcArtHolder = finishingBattle->loserId;
			for(const auto & slot : ArtifactUtils::commonWornSlots())
			{
				if(const auto artSlot = loserHero->artifactsWorn.find(slot); artSlot != loserHero->artifactsWorn.end() && ArtifactUtils::isArtRemovable(*artSlot))
				{
					addArtifactToTransfer(packHero, artSlot->first, artSlot->second.getArt());
				}
			}
			for(const auto & artSlot : loserHero->artifactsInBackpack)
			{
				if(const auto art = artSlot.getArt(); art->getTypeId() != ArtifactID::GRAIL)
					addArtifactToTransfer(packHero, loserHero->getArtPos(art), art);
			}
			if(!packHero.artsPack0.empty())
				resultsApplied.movingArtifacts.emplace_back(std::move(packHero));

			if(loserHero->getCommander())
			{
				BulkMoveArtifacts packCommander(finishingBattle->victor, finishingBattle->loserId, finishingBattle->winnerId, false);
				packCommander.srcCreature = loserHero->findStack(loserHero->getCommander());
				for(const auto & artSlot : loserHero->getCommander()->artifactsWorn)
					addArtifactToTransfer(packCommander, artSlot.first, artSlot.second.getArt());

				if(!packCommander.artsPack0.empty())
					resultsApplied.movingArtifacts.emplace_back(std::move(packCommander));
			}

			auto armyObj = dynamic_cast<const CArmedInstance*>(gameHandler->gameInfo().getObj(finishingBattle->loserId));
			for(const auto & armySlot : armyObj->stacks)
			{
				BulkMoveArtifacts packArmy(finishingBattle->victor, finishingBattle->loserId, finishingBattle->winnerId, false);
				packArmy.srcArtHolder = armyObj->id;
				packArmy.srcCreature = armySlot.first;
				for(const auto & artSlot : armySlot.second->artifactsWorn)
					addArtifactToTransfer(packArmy, artSlot.first, armySlot.second->getArt(artSlot.first));

				if(!packArmy.artsPack0.empty())
					resultsApplied.movingArtifacts.emplace_back(std::move(packArmy));
			}
		}
	}

	resultsApplied.battleID = battleID;
	resultsApplied.victor = finishingBattle->victor;
	resultsApplied.loser = finishingBattle->loser;
	// Capture this specific reward before the result pack also removes temporary
	// Buffer and applies other recovery effects. A total pool delta would conflate
	// Formula Reserve with those unrelated effects.
	BattleLogMessage metamagicRewards;
	metamagicRewards.battleID = battleID;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * hero = (*battle)->battleGetFightingHero(side);
		const auto & state = (*battle)->getSide(side);
		if(!hero || state.metamagicPendingCount == 0 || state.metamagicSequenceSpells.size() <= 1
			|| !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules())
			|| !newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_FORMULA_RESERVE))
			continue;
		const int restored = std::min(newHorizonsMagic::METAMAGIC_FORMULA_RESERVE_POINTS,
			std::max(0, hero->manaLimit() - hero->getNormalSpellPoints()));
		MetaString line = MetaString::createFromTextID(hero->getNameTextID());
		line.appendRawString(": Formula Reserve restores ");
		line.appendNumber(restored);
		line.appendRawString(" Normal Spell Points as the Metamagic sequence ends with combat.");
		metamagicRewards.lines.push_back(std::move(line));
	}
	//BattleResultsApplied does not end the battle, it only applies most of its consequences
	gameHandler->sendAndApply(resultsApplied);
	if(!metamagicRewards.lines.empty())
		gameHandler->sendAndApply(metamagicRewards);
	// Mana Conservation is ordinary Normal-pool recovery. Apply it only after
	// BattleResultsApplied has finished its Buffer cleanup and result restorations,
	// then before any surviving hero is removed into the hero pool.
	BattleLogMessage manaConservationRewards;
	manaConservationRewards.battleID = battleID;
	const auto recoverManaConservation = [this, &manaConservationRewards](const CGHeroInstance * hero,
		int64_t spent, bool eligible)
	{
		if(!eligible || !hero || spent <= 0
			|| !newHorizonsMagic::spellPointRulesActive(hero->getMagicRules())
			|| !hero->hasActivePerk(wisdomSkillId, manaConservationPerkId))
			return;
		const int64_t requested = std::min<int64_t>(20, spent / 5);
		const int32_t recoverable = static_cast<int32_t>(std::min<int64_t>(requested,
			std::max<int64_t>(0, static_cast<int64_t>(hero->manaLimit()) - hero->getNormalSpellPoints())));
		if(recoverable <= 0)
			return;
		const int32_t before = hero->getNormalSpellPoints();
		gameHandler->restoreSpellPoints(hero->id, recoverable);
		const int32_t restored = hero->getNormalSpellPoints() - before;
		if(restored <= 0)
			return;
		MetaString line = MetaString::createFromTextID(hero->getNameTextID());
		line.appendRawString(": Mana Conservation restores ");
		line.appendNumber(restored);
		line.appendRawString(" Normal Spell Points after combat.");
		manaConservationRewards.lines.push_back(std::move(line));
	};
	const bool drawHeroesRetreat = finishingBattle->isDraw()
		&& gameHandler->gameInfo().getSettings().getBoolean(EGameSettings::HEROES_RETREAT_ON_WIN_WITHOUT_TROOPS);
	const bool attackerIsWinner = !finishingBattle->isDraw()
		&& finishingBattle->winnerSide == BattleSide::ATTACKER;
	const bool defenderIsWinner = !finishingBattle->isDraw()
		&& finishingBattle->winnerSide == BattleSide::DEFENDER;
	const bool attackerEscapes = !finishingBattle->isDraw()
		&& finishingBattle->winnerSide != BattleSide::ATTACKER
		&& (result.result == EBattleResult::ESCAPE || result.result == EBattleResult::SURRENDER);
	const bool defenderEscapes = !finishingBattle->isDraw()
		&& finishingBattle->winnerSide != BattleSide::DEFENDER
		&& (result.result == EBattleResult::ESCAPE || result.result == EBattleResult::SURRENDER);
	recoverManaConservation(attackerHero, attackerHeroManaSpent,
		attackerIsWinner || attackerEscapes || (drawHeroesRetreat && attackerHero));
	recoverManaConservation(defenderHero, defenderHeroManaSpent,
		defenderIsWinner || defenderEscapes || (drawHeroesRetreat && defenderHero));
	if(!manaConservationRewards.lines.empty())
		gameHandler->sendAndApply(manaConservationRewards);

	// Remove beaten hero
	if(loserHero)
	{
		RemoveObject ro(loserHero->id, finishingBattle->victor);
		gameHandler->sendAndApply(ro);
	}

	//retreat the victor if he/she has no pernament creatures left
	if (winnerHero && !winnerHasUnitsLeft)
	{
		RemoveObject ro(winnerHero->id, finishingBattle->loser);
		gameHandler->sendAndApply(ro);
		gameHandler->heroPool->onHeroEscaped(finishingBattle->victor, winnerHero);
	}

	// For draw case both heroes should be removed
	if(finishingBattle->isDraw())
	{

		if (attackerHero)
		{
			RemoveObject ro(attackerHero->id, defenderSide);
			gameHandler->sendAndApply(ro);
		}

		if (defenderHero)
		{
			RemoveObject ro(defenderHero->id, attackerSide);
			gameHandler->sendAndApply(ro);
		}

		if(gameHandler->gameInfo().getSettings().getBoolean(EGameSettings::HEROES_RETREAT_ON_WIN_WITHOUT_TROOPS))
		{
			if (attackerHero)
				gameHandler->heroPool->onHeroEscaped(attackerSide, attackerHero);
			if (defenderHero)
				gameHandler->heroPool->onHeroEscaped(defenderSide, defenderHero);
		}
	}

	if (result.result == EBattleResult::SURRENDER)
	{
		gameHandler->statistics->getPlayerAccumulator(finishingBattle->loser).numHeroSurrendered++;
		gameHandler->heroPool->onHeroSurrendered(finishingBattle->loser, loserHero);
	}

	if (result.result == EBattleResult::ESCAPE)
	{
		gameHandler->statistics->getPlayerAccumulator(finishingBattle->loser).numHeroEscaped++;
		gameHandler->heroPool->onHeroEscaped(finishingBattle->loser, loserHero);
	}

	//notify all players that battle has ended after all consequences are applied
	BattleEnded ended;
	ended.battleID = battleID;
	ended.victor = finishingBattle->victor;
	ended.loser = finishingBattle->loser;
	gameHandler->sendAndApply(ended);

	//handle victory/loss of engaged players
	gameHandler->checkVictoryLossConditions({finishingBattle->loser, finishingBattle->victor});

	finishingBattles.erase(battleID);
	battleResults.erase(battleID);
}

void BattleResultProcessor::setBattleResult(const CBattleInfoCallback & battle, EBattleResult resultType, BattleSide victoriusSide)
{
	assert(battleResults.count(battle.getBattle()->getBattleID()) == 0);

	battleResults[battle.getBattle()->getBattleID()] = std::make_unique<BattleResult>();

	auto & battleResult = battleResults[battle.getBattle()->getBattleID()];
	battleResult->battleID = battle.getBattle()->getBattleID();
	battleResult->result = resultType;
	battleResult->winner = victoriusSide; //surrendering side loses
	battleResult->attacker = battle.getBattle()->getSidePlayer(BattleSide::ATTACKER);
	const auto * winnerHero = victoriusSide == BattleSide::NONE
		? nullptr : battle.battleGetFightingHero(victoriusSide);
	const bool excludeMagicalCasualties = winnerHero && winnerHero->usesNewHorizonsNecromancy()
		&& !winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
	const bool deathLord = winnerHero && winnerHero->usesNewHorizonsNecromancy()
		&& winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::DEATH_LORD_ID);
	const bool graveKnowledge = winnerHero && winnerHero->usesNewHorizonsNecromancy()
		&& winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID);
	const bool lordOfTheDead = winnerHero && winnerHero->usesNewHorizonsNecromancy()
		&& winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::LORD_OF_THE_DEAD_ID);
	if(lordOfTheDead)
		battleResult->necromancyDefeatedArmyHadLivingChampion = originalArmyContainsLivingChampion(
			battle.battleGetArmyObject(CBattleInfoEssentials::otherSide(victoriusSide)),
			battle.getBattle()->getCreatureCategoryRules());

	auto allStacks = battle.battleGetStacksIf([](const CStack * stack){

		if (stack->summoned)//don't take into account temporary summoned stacks
			return false;

		if (stack->isTurret())
			return false;

		return true;
	});

	for(const auto & st : allStacks) //setting casualties
	{
		auto originalFormState = acquireOriginalFormState(*st);
		const battle::CUnitState * resultState = originalFormState.get();
		if(!resultState)
			resultState = st;
		const CreatureID resultCreature = resultState->creatureId();
		const CCreature * resultCreatureType = resultState->unitType();
		si32 killed = resultState->getKilled();
		if(killed > 0)
		{
			battleResult->casualties[st->unitSide()][resultCreature] += killed;
			// New Horizons uses an explicit provenance-compatible corpse snapshot.
			// Temporary summons, clones, legacy DISINTEGRATE stacks, undead and
			// other nonliving creatures never enter the living-casualty pool.
			// Destroyed remains are never eligible. Ordinary magical casualties
			// additionally require the winner's active Corpse Preservation perk.
			// Resolve this once for both the Necromancy choice and its application.
			const si32 unusableRemains = std::min(killed, resultState->getUnusableRemains());
			const si32 usableCasualties = killed - unusableRemains;
			const si32 excludedMagical = excludeMagicalCasualties
				? std::clamp<si32>(resultState->getMagicalCasualties(), 0, usableCasualties) : 0;
			const si32 eligibleCasualties = usableCasualties - excludedMagical;
			const bool hasUsableRemains = eligibleCasualties > 0
				&& !st->summoned && !st->isClone()
				&& !st->hasBonusOfType(BonusType::DISINTEGRATE)
				&& resultCreatureType;
			if(!hasUsableRemains)
				continue;

			if(!resultCreatureType->hasBonusOfType(BonusType::UNDEAD)
				&& !resultCreatureType->hasBonusOfType(BonusType::NON_LIVING)
				&& !resultCreatureType->hasBonusOfType(BonusType::MECHANICAL))
				battleResult->necromancyEligibleCasualties[st->unitSide()][resultCreature] += eligibleCasualties;

			if(deathLord && resultCreatureType->hasBonusOfType(BonusType::NON_LIVING)
				&& !resultCreatureType->hasBonusOfType(BonusType::UNDEAD)
				&& !resultCreatureType->hasBonusOfType(BonusType::MECHANICAL))
				battleResult->necromancyNonlivingEligibleCasualties[st->unitSide()][resultCreature] += eligibleCasualties;

			if(graveKnowledge && resultCreatureType->hasBonusOfType(BonusType::UNDEAD)
				&& !resultCreatureType->hasBonusOfType(BonusType::MECHANICAL))
				battleResult->necromancyUndeadEligibleCasualties[st->unitSide()][resultCreature] += eligibleCasualties;
		}
	}
	battleResult->necromancyEligibilityCaptured = true;
	battleResult->necromancySpecialEligibilityCaptured = true;
}

bool BattleResultProcessor::battleIsEnding(const CBattleInfoCallback & battle) const
{
	return battleResults.count(battle.getBattle()->getBattleID()) != 0;
}
