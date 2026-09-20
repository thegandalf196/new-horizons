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
#include "../../lib/CPlayerState.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/battle/SideInBattle.h"
#include "../../lib/entities/artifact/ArtifactUtils.h"
#include "../../lib/entities/artifact/CArtifact.h"
#include "../../lib/entities/artifact/CArtifactFittingSet.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"

#include <vcmi/spells/Spell.h>

BattleResultProcessor::BattleResultProcessor(CGameHandler * gameHandler)
	: gameHandler(gameHandler)
{
}

CasualtiesAfterBattle::CasualtiesAfterBattle(const CBattleInfoCallback & battle, BattleSide sideInBattle):
	army(battle.battleGetArmyObject(sideInBattle))
{
	heroWithDeadCommander = ObjectInstanceID();

	PlayerColor color = battle.sideToPlayer(sideInBattle);

	auto allStacks = battle.battleGetStacksIf([color](const CStack * stack){

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
		// Use const cast - in order to call non-const "takeResurrected" for proper calculation of casualties
		// TODO: better solution
		auto * st = const_cast<CStack*>(stConst);

		logGlobal->debug("Calculating casualties for %s", st->nodeName());

		st->health.takeResurrected();

		if(st->unitSlot() == SlotID::WAR_MACHINES_SLOT)
		{
			auto warMachine = st->unitType()->warMachine;

			if(warMachine == ArtifactID::NONE)
			{
				logGlobal->error("Invalid creature in war machine virtual slot. Stack: %s", st->nodeName());
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
			if (nullptr == st->base)
			{
				logGlobal->error("Stack with no base in commander slot. Stack: %s", st->nodeName());
			}
			else
			{
				auto c = dynamic_cast <const CCommanderInstance *>(st->base);
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
					logGlobal->error("Stack with invalid instance in commander slot. Stack: %s", st->nodeName());
			}
		}
		else if(st->base && !army->slotEmpty(st->unitSlot()))
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
			logGlobal->warn("Unable to process stack: %s", st->nodeName());
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

	if(heroAttacker)
		battleResult->exp[BattleSide::ATTACKER] = heroAttacker->calculateXp(battleResult->exp[BattleSide::ATTACKER]);//scholar skill
	if(heroDefender)
		battleResult->exp[BattleSide::DEFENDER] = heroDefender->calculateXp(battleResult->exp[BattleSide::DEFENDER]);

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

void BattleResultProcessor::askNecromancyChoice(const BattleID & battleID, const CGHeroInstance * hero,
	const PendingNecromancy & pending)
{
	const auto player = hero->getOwner();
	if(!player.isValidPlayer())
		return;

	// Query state is created before the packet is sent.  The callback resumes
	// the exact finalization that was paused for this choice; no client-provided
	// count, creature ID or mana value is trusted.
	auto query = std::make_shared<CNecromancyQuery>(gameHandler, player, pending.choices,
		[this, battleID](std::optional<CreatureID> chosen)
		{
			auto pendingIt = pendingNecromancy.find(battleID);
			if(pendingIt == pendingNecromancy.end() || !chosen)
				return;
			pendingIt->second.selected = *chosen;
			const auto resultIt = battleResults.find(battleID);
			if(resultIt != battleResults.end())
				battleFinalize(battleID, *resultIt->second);
			});

	BlockingDialog dialog(false, true);
	dialog.queryID = query->queryID;
	dialog.player = player;
	dialog.text = MetaString::createFromRawString(
		"Necromancy: choose whether to convert groups of three Skeletons into Zombies.");
	for(const auto creature : pending.choices)
	{
		const auto amount = pending.offeredCounts.find(creature);
		dialog.components.emplace_back(ComponentType::CREATURE, creature,
			amount == pending.offeredCounts.end() ? 0 : amount->second);
	}
	gameHandler->queries->addQuery(query);
	gameHandler->sendAndApply(dialog);
}

bool BattleResultProcessor::applyNewHorizonsNecromancy(const BattleID & battleID, const BattleResult & result,
	int32_t initialMana, const CGHeroInstance * winnerHero,
	BattleResultsApplied & resultsApplied, std::optional<CreatureID> selected)
{
	if(!winnerHero || !winnerHero->usesNewHorizonsNecromancy())
		return false;

	const auto skeleton = CreatureID(CreatureID::decode("core:skeleton"));
	const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
	const auto losingSide = CBattleInfoEssentials::otherSide(result.winner);
	const auto & eligible = result.necromancyEligibilityCaptured
		? result.necromancyEligibleCasualties[losingSide]
		: result.casualties[losingSide];
	const auto eligibleCount = result.necromancyEligibilityCaptured
		? newHorizonsNecromancy::countLivingEligibleCasualties(eligible)
		: newHorizonsNecromancy::countLivingEligibleCasualties(result.casualties[losingSide]);

	const bool boneCollector = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::BONE_COLLECTOR_ID);
	const bool corpsePreservation = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
	const bool darkConversion = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DARK_CONVERSION_ID);
	const bool blackHarvest = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::BLACK_HARVEST_ID);

	const auto existingSlot = [winnerHero](CreatureID creature)
	{
		const auto slot = winnerHero->getSlotFor(creature);
		return slot.validSlot() && winnerHero->hasStackAtSlot(slot)
			&& winnerHero->getCreature(slot) == creature.toCreature() ? slot : SlotID();
	};
	const auto existingSkeletonSlot = existingSlot(skeleton);
	const auto existingZombieSlot = existingSlot(zombie);
	const auto freeSlots = winnerHero->getFreeSlots();
	const bool skeletonAvailable = existingSkeletonSlot.validSlot() || !freeSlots.empty();
	const bool zombieAvailable = existingZombieSlot.validSlot() || !freeSlots.empty();
	const bool zombieChoice = selected && *selected == zombie;
	const int32_t postBattleMana = std::min<int32_t>(winnerHero->mana, initialMana);
	// Black Harvest is a separate perk gate.  The resolver's mana field is
	// otherwise zero even when a large conversion is performed. Use the same
	// post-battle clamp baseline as BattleResultsApplied so combat-only bonus
	// mana cannot suppress a recovery that will fit after the clamp.
	auto summary = newHorizonsNecromancy::resolve(winnerHero->getNewHorizonsNecromancyRank(), eligibleCount,
		boneCollector, corpsePreservation, darkConversion, zombieChoice,
		skeletonAvailable, zombieAvailable, postBattleMana,
		blackHarvest ? winnerHero->manaLimit() : postBattleMana);
	if(!summary.active)
		return false;

	// A dark-conversion answer is legal only if the exact output it produces
	// still fits.  This is rechecked after the query, immediately before state
	// mutation, to keep the operation atomic.
	if(summary.blockedByArmyCapacity)
	{
		resultsApplied.necromancy = summary;
		return true;
	}

	// Reserve every destination before emitting either mutation. getSlotFor()
	// returns the same first empty slot for two absent creature types, so using
	// it independently would reject a legal conversion even when more empty
	// slots exist (or risk a partial result). Existing matching stacks do not
	// consume an empty destination.
	const auto destinations = newHorizonsNecromancy::reserveDestinations(
		existingSkeletonSlot, existingZombieSlot, freeSlots,
		summary.skeletonsRaised, summary.zombiesRaised);
	if(!destinations.fits)
	{
		summary.applied = false;
		summary.blockedByArmyCapacity = true;
		summary.skeletonsRaised = 0;
		summary.zombiesRaised = 0;
		summary.manaRecovered = 0;
		resultsApplied.necromancy = summary;
		return true;
	}

	auto addRaised = [this, winnerHero](SlotID slot, CreatureID creature, int32_t count) -> bool
	{
		if(count <= 0)
			return true;
		if(!slot.validSlot())
			return false;
		const auto location = StackLocation(winnerHero->id, slot);
		if(winnerHero->hasStackAtSlot(slot))
			return gameHandler->changeStackCount(location, count, ChangeValueMode::RELATIVE);
		return gameHandler->insertNewStack(location, creature.toCreature(), count);
	};

	if(!addRaised(destinations.skeleton, skeleton, summary.skeletonsRaised)
		|| !addRaised(destinations.zombie, zombie, summary.zombiesRaised))
	{
		// The preflight above should make this unreachable on the authoritative
		// simulation thread.  Keep the summary truthful if an invariant is ever
		// violated rather than claiming creatures were raised.
		summary.applied = false;
		summary.blockedByArmyCapacity = true;
		summary.skeletonsRaised = 0;
		summary.zombiesRaised = 0;
		summary.manaRecovered = 0;
		resultsApplied.necromancy = summary;
		return true;
	}

	// Keep the legacy descriptor useful for clients when there is one output
	// stack.  A Dark Conversion result may contain two stacks; leaving the
	// legacy single-stack field empty avoids showing a misleading partial popup
	// while New Horizons clients consume the complete summary below.
	if(summary.skeletonsRaised > 0 && summary.zombiesRaised == 0)
		resultsApplied.raisedStack = CStackBasicDescriptor(skeleton, summary.skeletonsRaised);
	else if(summary.zombiesRaised > 0 && summary.skeletonsRaised == 0)
		resultsApplied.raisedStack = CStackBasicDescriptor(zombie, summary.zombiesRaised);
	resultsApplied.necromancy = summary;
	(void)battleID;
	return true;
}

void BattleResultProcessor::battleFinalize(const BattleID & battleID, const BattleResult & result)
{
	LOG_TRACE(logGlobal);

	assert(finishingBattles.count(battleID) != 0);
	if(finishingBattles.count(battleID) == 0)
		return;

	auto & finishingBattle = finishingBattles[battleID];

	// A Dark Conversion prompt temporarily suspends finalization after all
	// battle queries have already been removed.  The answer resumes this
	// method, so it must not decrement the completed battle-query count again.
	const bool resumingNecromancy = pendingNecromancy.contains(battleID);
	if(!resumingNecromancy)
	{
		finishingBattle->remainingBattleQueriesCount--;
		logGlobal->trace("Decremented gameHandler->queries count to %d", finishingBattle->remainingBattleQueriesCount);
	}

	if (!resumingNecromancy && finishingBattle->remainingBattleQueriesCount > 0)
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

	// Resolve the only player choice before collecting any other post-battle
	// consequences.  If a query is needed, finalization pauses here and the
	// callback resumes it with the server-owned choice.  This keeps random
	// Eagle Eye/artifact outcomes from being generated twice.
	std::optional<CreatureID> newHorizonsNecromancyChoice;
	if(winnerHero && winnerHasUnitsLeft && winnerHero->usesNewHorizonsNecromancy())
	{
		const auto pendingIt = pendingNecromancy.find(battleID);
		if(pendingIt != pendingNecromancy.end())
		{
			newHorizonsNecromancyChoice = pendingIt->second.selected;
		}
		else
		{
			const auto skeleton = CreatureID(CreatureID::decode("core:skeleton"));
			const auto zombie = CreatureID(CreatureID::decode("core:zombie"));
			const auto losingSide = CBattleInfoEssentials::otherSide(result.winner);
			const auto & eligible = result.necromancyEligibilityCaptured
				? result.necromancyEligibleCasualties[losingSide]
				: result.casualties[losingSide];
			const auto eligibleCount = newHorizonsNecromancy::countLivingEligibleCasualties(eligible);
			const bool boneCollector = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
				newHorizonsNecromancy::BONE_COLLECTOR_ID);
			const bool corpsePreservation = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
				newHorizonsNecromancy::CORPSE_PRESERVATION_ID);
			const bool darkConversion = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
				newHorizonsNecromancy::DARK_CONVERSION_ID);
			const bool blackHarvest = winnerHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
				newHorizonsNecromancy::BLACK_HARVEST_ID);

			// A preview with both destinations enabled gives us the authoritative
			// offered count without mutating the hero.  Actual destination capacity
			// is checked again by applyNewHorizonsNecromancy immediately before the
			// stack/mana packs are emitted.
			const auto preview = newHorizonsNecromancy::resolve(winnerHero->getNewHorizonsNecromancyRank(),
				eligibleCount, boneCollector, corpsePreservation, darkConversion, false,
				true, true, winnerHero->mana, blackHarvest ? winnerHero->manaLimit() : winnerHero->mana);
			std::vector<CreatureID> choices;
			std::map<CreatureID, int32_t> offeredCounts;
			if(preview.skeletonsOffered > 0)
			{
				const auto skeletonSlot = winnerHero->getSlotFor(skeleton);
				const auto zombieSlot = winnerHero->getSlotFor(zombie);
				const bool hasSkeletonStack = skeletonSlot.validSlot()
					&& winnerHero->hasStackAtSlot(skeletonSlot)
					&& winnerHero->getCreature(skeletonSlot) == skeleton.toCreature();
				const bool hasZombieStack = zombieSlot.validSlot()
					&& winnerHero->hasStackAtSlot(zombieSlot)
					&& winnerHero->getCreature(zombieSlot) == zombie.toCreature();
				const auto freeSlotCount = winnerHero->getFreeSlots().size();
				const bool skeletonFits = hasSkeletonStack || freeSlotCount >= 1;
				const int32_t zombies = preview.skeletonsOffered / 3;
				const int32_t remainder = preview.skeletonsOffered % 3;
				const size_t requiredFreeSlots = (hasZombieStack ? 0 : 1)
					+ (remainder > 0 && !hasSkeletonStack ? 1 : 0);
				const bool zombieFits = zombies > 0 && freeSlotCount >= requiredFreeSlots;

				if(skeletonFits)
				{
					choices.push_back(skeleton);
					offeredCounts.emplace(skeleton, preview.skeletonsOffered);
				}
				if(darkConversion && zombieFits)
				{
					choices.push_back(zombie);
					offeredCounts.emplace(zombie, zombies);
				}
			}

			const auto * winnerPlayer = gameHandler->gameInfo().getPlayerState(winnerHero->getOwner());
			// The choice is authoritative and player-facing for every controlled
			// winner.  Computer players receive the same BlockingDialog packet and
			// answer through their normal AI query hook; bypassing the query here
			// would make Dark Conversion behave differently for AI and would leave
			// no exercised path for validating the AI's response.
			if(choices.size() > 1 && winnerHero->getOwner().isValidPlayer() && winnerPlayer)
			{
				PendingNecromancy pending;
				pending.hero = winnerHero->id;
				pending.choices = choices;
				pending.offeredCounts = offeredCounts;
				pendingNecromancy.emplace(battleID, pending);
				askNecromancyChoice(battleID, winnerHero, pending);
				return;
			}

			// Single-option or uncontrolled cases use the only legal output. A
			// controlled human or AI with both choices was queried above.
			if(choices.size() == 1)
				newHorizonsNecromancyChoice = choices.front();
			else if(choices.size() > 1)
				newHorizonsNecromancyChoice = skeleton;
		}
	}

	if(winnerHero && winnerHasUnitsLeft)
	{
		// Eagle Eye handling
		if(auto eagleEyeLevel = winnerHero->valOfBonuses(BonusType::LEARN_BATTLE_SPELL_LEVEL_LIMIT))
		{
			// hero also needs corresponding level of Wisdom to learn a spell
			const int spellLevelLimit = std::min(eagleEyeLevel, winnerHero->maxSpellLevel());

			resultsApplied.learnedSpells.eagleEyeBonus = true;
			resultsApplied.learnedSpells.learn = 1;
			resultsApplied.learnedSpells.hid = finishingBattle->winnerId;
			for(const auto & spellId : (*battle)->getUsedSpells(CBattleInfoEssentials::otherSide(result.winner)))
			{
				const auto spell = spellId.toEntity(LIBRARY->spells());
				if(spell
					&& winnerHero->getSpellLevel(spell) <= spellLevelLimit
					&& !winnerHero->spellbookContainsSpell(spell->getId())
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
			applyNewHorizonsNecromancy(battleID, result, (*battle)->getSide(result.winner).initialMana,
				winnerHero, resultsApplied,
				newHorizonsNecromancyChoice);
		else
		{
			// Give raised units to winner, if any were raised, units will be given after casualties are taken
			resultsApplied.raisedStack = winnerHero->calculateNecromancy(result);
			const SlotID necroSlot = resultsApplied.raisedStack.getCreature() ? winnerHero->getSlotFor(resultsApplied.raisedStack.getCreature()) : SlotID();
			if(necroSlot != SlotID() && !finishingBattle->isDraw())
				gameHandler->addToSlot(StackLocation(finishingBattle->winnerId, necroSlot), resultsApplied.raisedStack.getCreature(), resultsApplied.raisedStack.getCount());
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
	//BattleResultsApplied does not end the battle, it only applies most of its consequences
	gameHandler->sendAndApply(resultsApplied);
	if(resumingNecromancy)
		pendingNecromancy.erase(battleID);

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

	auto allStacks = battle.battleGetStacksIf([](const CStack * stack){

		if (stack->summoned)//don't take into account temporary summoned stacks
			return false;

		if (stack->isTurret())
			return false;

		return true;
	});

	for(const auto & st : allStacks) //setting casualties
	{
		si32 killed = st->getKilled();
		if(killed > 0)
		{
			battleResult->casualties[st->unitSide()][st->creatureId()] += killed;
			// New Horizons uses an explicit provenance-compatible corpse snapshot.
			// Temporary summons, clones, disintegrated remains, undead and other
			// nonliving creatures never enter the living-casualty pool. Ordinary
			// weapon and magical damage do, including when Corpse Preservation is
			// selected; only an effect that actually invalidates the remains is
			// excluded here.
			if(!st->summoned && !st->isClone()
				&& !st->hasBonusOfType(BonusType::DISINTEGRATE)
				&& !st->unitType()->hasBonusOfType(BonusType::UNDEAD)
				&& !st->unitType()->hasBonusOfType(BonusType::NON_LIVING)
				&& !st->unitType()->hasBonusOfType(BonusType::MECHANICAL))
				battleResult->necromancyEligibleCasualties[st->unitSide()][st->creatureId()] += killed;
		}
	}
	battleResult->necromancyEligibilityCaptured = true;
}

bool BattleResultProcessor::battleIsEnding(const CBattleInfoCallback & battle) const
{
	return battleResults.count(battle.getBattle()->getBattleID()) != 0;
}
