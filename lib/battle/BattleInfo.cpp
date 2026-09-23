/*
 * BattleInfo.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleInfo.h"
#include "NewHorizonsBloodrage.h"

#include "BattleLayout.h"
#include "CObstacleInstance.h"
#include "../bonuses/BonusSelector.h"
#include "bonuses/Limiters.h"
#include "bonuses/Updaters.h"
#include "../bonuses/BonusParameters.h"
#include "../CStack.h"
#include "../callback/IGameInfoCallback.h"
#include "../entities/artifact/CArtifact.h"
#include "../entities/building/TownFortifications.h"
#include "../filesystem/Filesystem.h"
#include "../GameLibrary.h"
#include "../IGameSettings.h"
#include "../mapObjects/CGTownInstance.h"
#include "../spells/CSpell.h"
#include "../spells/NewHorizonsSorcery.h"
#include "../texts/CGeneralTextHandler.h"
#include "../BattleFieldHandler.h"
#include "../ObstacleHandler.h"

#include <vstd/RNG.h>

namespace
{
bool isTimeStopBonus(const Bonus & bonus)
{
	// The dedicated marker is authoritative even when a legacy/content-light
	// loader cannot resolve the script's SpellID.  The source check below keeps
	// the old NONE/NOT_ACTIVE/INVINCIBLE fallback markers recognizable.
	if(bonus.type == BonusType::TIME_STOP)
		return true;

	if(bonus.source != BonusSource::SPELL_EFFECT || !bonus.sid.as<SpellID>().hasValue())
		return false;

	const auto * spell = bonus.sid.as<SpellID>().toSpell();
	return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
}

bool isTimeStopStateBonus(const Bonus & bonus)
{
	return isTimeStopBonus(bonus)
		&& (bonus.type == BonusType::TIME_STOP
			|| bonus.type == BonusType::NONE
			|| bonus.type == BonusType::NOT_ACTIVE
			|| bonus.type == BonusType::INVINCIBLE);
}

bool timeStopBelongsToSide(const Bonus & bonus, BattleSide side)
{
	if(!isTimeStopBonus(bonus))
		return false;

	// Early development snapshots did not carry the caster side in addInfo.
	// They are safe to clean up on either hero action rather than leaving a
	// permanent stale marker in a loaded battle.
	if(!bonus.parameters)
		return true;

	try
	{
		return bonus.parameters->toNumber() == static_cast<int32_t>(side);
	}
	catch(const std::exception &)
	{
		return false;
	}
}

bool orderUnitsAdjacent(const battle::Unit * first, const battle::Unit * second)
{
	if(!first || !second)
		return false;
	for(const auto & firstHex : first->getHexes())
	{
		if(!firstHex.isValid())
			continue;
		for(const auto & secondHex : second->getHexes())
			if(secondHex.isValid() && BattleHex::getDistance(firstHex, secondHex) == 1)
				return true;
	}
	return false;
}
}

const SideInBattle & BattleInfo::getSide(BattleSide side) const
{
	return sides.at(side);
}

SideInBattle & BattleInfo::getSide(BattleSide side)
{
	return sides.at(side);
}

const AlternatingHeroActionState & BattleInfo::getWarcastingState(BattleSide side) const
{
	static const AlternatingHeroActionState empty;
	if(!newHorizonsWarcasting::enabled(magicRules))
		return empty;
	return sides.at(side).warcastingState;
}

BattleSide BattleInfo::gatedDemonicStackSide(uint32_t unitId) const
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & gated = sides.at(side).gatedDemonicStacks;
		if(std::ranges::any_of(gated, [unitId](const auto & stack)
		{
			return stack.unitId == unitId;
		}))
			return side;
	}
	return BattleSide::NONE;
}

bool BattleInfo::hasGatedDemonicStack(BattleSide side, uint32_t unitId) const
{
	return gatedDemonicStackSide(unitId) == side;
}

void BattleInfo::armChainGate(BattleSide side)
{
	// The token is intentionally a bool: several kills in one BattleAttack or
	// across one round cannot bank more than the one promised acceleration.
	sides.at(side).chainGateArmed = true;
}

bool BattleInfo::consumeChainGate(BattleSide side)
{
	auto & token = sides.at(side).chainGateArmed;
	if(!token)
		return false;
	token = false;
	return true;
}

void BattleInfo::recordBloodrageStackDeath(uint32_t unitId)
{
	if(sides[BattleSide::ATTACKER].bloodrageRank == 0 && sides[BattleSide::DEFENDER].bloodrageRank == 0)
		return;
	if(!bloodrageDestroyedUnits.insert(unitId).second)
		return;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & state = sides.at(side);
		state.bloodrageDamagePercent = std::min(newHorizonsBloodrage::capForRank(state.bloodrageRank),
			state.bloodrageDamagePercent + newHorizonsBloodrage::incrementForRank(state.bloodrageRank));
	}
}

void BattleInfo::clearBloodrageStackDeath(uint32_t unitId)
{
	bloodrageDestroyedUnits.erase(unitId);
}

bool BattleInfo::consumeHeroOrderUnit(BattleSide side, uint32_t unitId)
{
	auto & state = sides.at(side).orderState;
	if(!state || state->command != HeroCommand::CHARGE || state->containsConsumed(unitId))
		return false;
	state->consumedUnitIds.insert(std::lower_bound(state->consumedUnitIds.begin(), state->consumedUnitIds.end(), unitId), unitId);
	return true;
}

bool BattleInfo::triggerHeroOrderBrace(BattleSide side, uint32_t unitId)
{
	auto & state = sides.at(side).orderState;
	// Brace is a reaction to every qualifying incoming melee attack, not a
	// once-per-unit/round charge. Keep the old entry point for callers while
	// making the trigger itself stateless and therefore deterministic on clients.
	(void)unitId;
	return state && state->command == HeroCommand::BRACE;
}

bool BattleInfo::breakHeroOrderHold(uint32_t unitId)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & state = sides.at(side).orderState;
		if(!state || state->command != HeroCommand::HOLD_THE_LINE || state->containsHoldBroken(unitId))
			continue;
		const auto * anchor = state->anchorFor(unitId);
		const auto * unit = battleGetUnitByID(unitId);
		if(anchor && unit && anchor->position != unit->getPosition().toInt())
		{
			state->holdBrokenUnitIds.insert(std::lower_bound(state->holdBrokenUnitIds.begin(), state->holdBrokenUnitIds.end(), unitId), unitId);
			return true;
		}
	}
	return false;
}

bool BattleInfo::interceptHeroOrderProtect(BattleSide side)
{
	auto & state = sides.at(side).orderState;
	if(!state || state->command != HeroCommand::PROTECT || state->protectIntercepted || state->protectBroken)
		return false;
	state->protectIntercepted = true;
	return true;
}

void BattleInfo::setHeroOrderState(BattleSide side, const std::optional<HeroOrderState> & state)
{
	if(state)
		state->validateShape();
	sides.at(side).orderState = state;
}

void BattleInfo::expireSeparatedHeroOrderProtect()
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto & state = sides.at(side).orderState;
		if(!state || state->command != HeroCommand::PROTECT || state->protectBroken)
			continue;
		const auto * protector = getStack(static_cast<int>(state->primaryTargetUnitId), false);
		const auto * ward = getStack(static_cast<int>(state->secondaryTargetUnitId), false);
		if(!protector || !ward || !protector->alive() || !ward->alive() || !orderUnitsAdjacent(protector, ward))
			state->protectBroken = true;
	}
}

bool BattleInfo::recordHeroOrderFlankSide(BattleSide side, uint32_t targetUnitId, uint8_t sideMask)
{
	auto & state = sides.at(side).orderState;
	if(!state || state->command != HeroCommand::FLANK || sideMask == 0 || sideMask > 0x3f)
		return false;
	auto * target = state->flankFor(targetUnitId);
	if(!target || (target->sideMask & sideMask) == sideMask)
		return false;
	target->sideMask |= sideMask;
	return true;
}

bool BattleInfo::setHeroOrderSecondWindActive(BattleSide side, bool active)
{
	auto & state = sides.at(side).orderState;
	if(!state || state->command != HeroCommand::SECOND_WIND)
		return false;
	state->secondWindActive = active;
	return true;
}

///BattleInfo
void BattleInfo::generateNewStack(uint32_t id, const CStackInstance & base, BattleSide side, const SlotID & slot, const BattleHex & position)
{
	PlayerColor owner = getSide(side).color;
	assert(!owner.isValidPlayer() || (base.getArmy() && base.getArmy()->tempOwner == owner));

	auto ret = std::make_unique<CStack>(&base, owner, id, side, slot);
	ret->initialPosition = getAvailableHex(base.getCreature(), side, position.toInt()); //TODO: what if no free tile on battlefield was found?
	stacks.push_back(std::move(ret));
}

void BattleInfo::generateNewStack(uint32_t id, const CStackBasicDescriptor & base, BattleSide side, const SlotID & slot, const BattleHex & position)
{
	PlayerColor owner = getSide(side).color;
	auto ret = std::make_unique<CStack>(&base, owner, id, side, slot);
	ret->initialPosition = position;
	stacks.push_back(std::move(ret));
}

void BattleInfo::localInit()
{
	for(BattleSide i : { BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		auto * armyObj = battleGetArmyObject(i);
		armyObj->battle = this;
		armyObj->attachTo(*this);
	}

	for(auto & s : stacks)
		s->localInit(this);

	exportBonuses();
}


//RNG that works like H3 one
struct RandGen
{
	ui32 seed;

	void srand(ui32 s)
	{
		seed = s;
	}
	void srand(const int3 & pos)
	{
		srand(110291 * static_cast<ui32>(pos.x) + 167801 * static_cast<ui32>(pos.y) + 81569);
	}
	int rand()
	{
		seed = 214013 * seed + 2531011;
		return (seed >> 16) & 0x7FFF;
	}
	int rand(int min, int max)
	{
		if(min == max)
			return min;
		if(min > max)
			return min;
		return min + rand() % (max - min + 1);
	}
};

struct RangeGenerator
{
	class ExhaustedPossibilities : public std::exception
	{
	};

	RangeGenerator(int _min, int _max, std::function<int()> _myRand):
		min(_min),
		remainingCount(_max - _min + 1),
		remaining(remainingCount, true),
		myRand(std::move(_myRand))
	{
	}

	int generateNumber() const
	{
		if(!remainingCount)
			throw ExhaustedPossibilities();
		if(remainingCount == 1)
			return 0;
		return myRand() % remainingCount;
	}

	//get number fulfilling predicate. Never gives the same number twice.
	int getSuchNumber(const std::function<bool(int)> & goodNumberPred = nullptr)
	{
		int ret = -1;
		do
		{
			int n = generateNumber();
			int i = 0;
			for(;;i++)
			{
				assert(i < (int)remaining.size());
				if(!remaining[i])
					continue;
				if(!n)
					break;
				n--;
			}

			remainingCount--;
			remaining[i] = false;
			ret = i + min;
		} while(goodNumberPred && !goodNumberPred(ret));
		return ret;
	}

	int min;
	int remainingCount;
	std::vector<bool> remaining;
	std::function<int()> myRand;
};

std::unique_ptr<BattleInfo> BattleInfo::setupBattle(IGameInfoCallback *cb, const int3 & tile, TerrainId terrain, const BattleField & battlefieldType, BattleSideArray<const CArmedInstance *> armies, BattleSideArray<const CGHeroInstance *> heroes, const BattleLayout & layout, const CGTownInstance * town)
{
	CMP_stack cmpst;
	auto currentBattle = std::make_unique<BattleInfo>(cb, layout);
	currentBattle->luckRollRules.goodChance = cb->getSettings().getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE);
	currentBattle->luckRollRules.badChance = cb->getSettings().getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE);
	currentBattle->luckRollRules.diceSize = cb->getSettings().getInteger(EGameSettings::COMBAT_LUCK_DICE_SIZE);
	currentBattle->luckRollRules.affectsAllTargets = cb->getSettings().getBoolean(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS);

	for(auto i : { BattleSide::LEFT_SIDE, BattleSide::RIGHT_SIDE})
	{
		currentBattle->sides[i].init(heroes[i], armies[i], i == BattleSide::RIGHT_SIDE ? town : nullptr);
		if(heroes[i])
		{
			auto & fortune = currentBattle->sides[i].sylvanLuck;
			fortune.serendipity = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.serendipity");
			fortune.naturesProvidence = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.natureSProvidence");
			fortune.fortunateAim = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.fortunateAim");
			fortune.perfectMoment = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.perfectMoment");
			fortune.forestsFavor = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.forestSFavor");
			fortune.luckyRecovery = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.luckyRecovery");
			fortune.sharedFortune = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.sharedFortune");
			fortune.cascadingFortune = heroes[i]->hasActivePerk("new-horizons:sylvanLuck", "new-horizons:sylvanLuck.cascadingFortune");
		}
		currentBattle->sides[i].bloodrageRank = newHorizonsBloodrage::rank(heroes[i]);
		currentBattle->sides[i].bloodrageDamagePercent = newHorizonsBloodrage::initialDamagePercent(heroes[i]);
	}

	currentBattle->tile = tile;
	currentBattle->terrainType = terrain;
	currentBattle->battlefieldType = battlefieldType;
	currentBattle->round = 0;
	currentBattle->activeStack = -1;
	currentBattle->replayAllowed = false;
	if (town)
		currentBattle->townID = town->id;

	//setting up siege obstacles
	if (town && town->fortificationsLevel().wallsHealth != 0)
	{
		auto fortification = town->fortificationsLevel();
		// Fortification HP belongs to the authoritative world snapshot, not to
		// whichever side happens to field a hero. This covers town garrisons and
		// prevents importing a v3 hero into a legacy world from changing durability.
		const auto canonicalSiege = cb->getHeroCapabilityRules()["rulesetVersion"].Integer() >= 3;
		currentBattle->si.canonicalStructuralHP = canonicalSiege;

		currentBattle->si.gateState = EGateState::CLOSED;

		currentBattle->si.wallState[EWallPart::GATE] = EWallState::INTACT;

		for(const auto wall : {EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL})
			currentBattle->si.wallState[wall] = static_cast<EWallState>(fortification.wallsHealth);

		if (fortification.citadelHealth != 0)
			currentBattle->si.wallState[EWallPart::KEEP] = static_cast<EWallState>(fortification.citadelHealth);

		if (fortification.upperTowerHealth != 0)
			currentBattle->si.wallState[EWallPart::UPPER_TOWER] = static_cast<EWallState>(fortification.upperTowerHealth);

		if (fortification.lowerTowerHealth != 0)
			currentBattle->si.wallState[EWallPart::BOTTOM_TOWER] = static_cast<EWallState>(fortification.lowerTowerHealth);

		if(canonicalSiege)
			for(const auto & [part, state] : currentBattle->si.wallState)
				if(state != EWallState::NONE)
					currentBattle->si.structuralHP[part] = SiegeInfo::maximumStructuralHP(part);
	}

	//randomize obstacles
	if (layout.obstaclesAllowed && (!town || !town->hasFort()))
 	{
		RandGen r{};
		auto ourRand = [&](){ return r.rand(); };
		r.srand(tile);
		r.rand(1,8); //battle sound ID to play... can't do anything with it here
		int tilesToBlock = r.rand(5,12);

		BattleHexArray blockedTiles;

		auto appropriateAbsoluteObstacle = [&](int id)
		{
			const auto * info = Obstacle(id).getInfo();
			return info && info->isAbsoluteObstacle && info->isAppropriate(currentBattle->terrainType, battlefieldType);
		};
		auto appropriateUsualObstacle = [&](int id)
		{
			const auto * info = Obstacle(id).getInfo();
			return info && !info->isAbsoluteObstacle && info->isAppropriate(currentBattle->terrainType, battlefieldType);
		};

		if(r.rand(1,100) <= 40) //put cliff-like obstacle
		{
			try
			{
				RangeGenerator obidgen(0, LIBRARY->obstacleHandler->size() - 1, ourRand);
				auto obstPtr = std::make_shared<CObstacleInstance>();
				obstPtr->obstacleType = CObstacleInstance::ABSOLUTE_OBSTACLE;
				obstPtr->ID = obidgen.getSuchNumber(appropriateAbsoluteObstacle);
				obstPtr->uniqueID = static_cast<si32>(currentBattle->obstacles.size());
				currentBattle->obstacles.push_back(obstPtr);

				for(const BattleHex & blocked : obstPtr->getBlockedTiles())
					blockedTiles.insert(blocked);
				tilesToBlock -= Obstacle(obstPtr->ID).getInfo()->blockedTiles.size() / 2;
			}
			catch(RangeGenerator::ExhaustedPossibilities &)
			{
				//silently ignore, if we can't place absolute obstacle, we'll go with the usual ones
				logGlobal->debug("RangeGenerator::ExhaustedPossibilities exception occurred - cannot place absolute obstacle");
			}
		}

		try
		{
			while(tilesToBlock > 0)
			{
				RangeGenerator obidgen(0, LIBRARY->obstacleHandler->size() - 1, ourRand);
				auto tileAccessibility = currentBattle->getAccessibility();
				const int obid = obidgen.getSuchNumber(appropriateUsualObstacle);
				const ObstacleInfo &obi = *Obstacle(obid).getInfo();

				auto validPosition = [&](const BattleHex & pos) -> bool
				{
					if(obi.height >= pos.getY())
						return false;
					if(pos.getX() == 0)
						return false;
					if(pos.getX() + obi.width > 15)
						return false;
					if(blockedTiles.contains(pos))
						return false;

					for(const BattleHex & blocked : obi.getBlocked(pos))
					{
						if(tileAccessibility[blocked.toInt()] == EAccessibility::UNAVAILABLE) //for ship-to-ship battlefield - exclude hardcoded unavailable tiles
							return false;
						if(blockedTiles.contains(blocked))
							return false;
						int x = blocked.getX();
						if(x <= 2 || x >= 14)
							return false;
					}

					return true;
				};

				RangeGenerator posgenerator(18, 168, ourRand);

				auto obstPtr = std::make_shared<CObstacleInstance>();
				obstPtr->ID = obid;
				obstPtr->pos = posgenerator.getSuchNumber(validPosition);
				obstPtr->uniqueID = static_cast<si32>(currentBattle->obstacles.size());
				currentBattle->obstacles.push_back(obstPtr);

				for(const BattleHex & blocked : obstPtr->getBlockedTiles())
					blockedTiles.insert(blocked);
				tilesToBlock -= static_cast<int>(obi.blockedTiles.size());
			}
		}
		catch(RangeGenerator::ExhaustedPossibilities &)
		{
			logGlobal->debug("RangeGenerator::ExhaustedPossibilities exception occurred - cannot place usual obstacle");
		}
	}

	//adding war machines
	//Checks if hero has artifact and create appropriate stack
	auto handleWarMachine = [&](BattleSide side, const ArtifactPosition & artslot, const BattleHex & hex)
	{
		const CArtifactInstance * warMachineArt = heroes[side]->getArt(artslot);

		if(nullptr != warMachineArt && hex.isValid())
		{
			CreatureID cre = warMachineArt->getType()->getWarMachine();

			if(cre != CreatureID::NONE)
				currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(cre, 1), side, SlotID::WAR_MACHINES_SLOT, hex);
		}
	};

	if(heroes[BattleSide::ATTACKER])
	{
		auto warMachineHexes = layout.warMachines.at(BattleSide::ATTACKER);

		handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH1, warMachineHexes.at(0));
		handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH2, warMachineHexes.at(1));
		handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH3, warMachineHexes.at(2));
		if(town && town->fortificationsLevel().wallsHealth > 0)
			handleWarMachine(BattleSide::ATTACKER, ArtifactPosition::MACH4, warMachineHexes.at(3));
	}

	if(heroes[BattleSide::DEFENDER])
	{
		auto warMachineHexes = layout.warMachines.at(BattleSide::DEFENDER);

		if(!town) //defending hero shouldn't receive ballista (bug #551)
			handleWarMachine(BattleSide::DEFENDER, ArtifactPosition::MACH1, warMachineHexes.at(0));
		handleWarMachine(BattleSide::DEFENDER, ArtifactPosition::MACH2, warMachineHexes.at(1));
		handleWarMachine(BattleSide::DEFENDER, ArtifactPosition::MACH3, warMachineHexes.at(2));
	}
	//war machines added

	//battleStartpos read
	for(BattleSide side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		int formationNo = armies[side]->stacksCount() - 1;
		vstd::abetween(formationNo, 0, GameConstants::ARMY_SIZE - 1);

		int k = 0; //stack serial
		for(auto i = armies[side]->Slots().begin(); i != armies[side]->Slots().end(); i++, k++)
		{
			const BattleHex & pos = layout.units.at(side).at(k);

			if (pos.isValid())
				currentBattle->generateNewStack(currentBattle->nextUnitId(), *i->second, side, i->first, pos);
			else
				logMod->warn("Invalid battlefield layout! Failed to find position for unit %d for %s", k, side == BattleSide::ATTACKER ? "attacker" : "defender");
		}
	}

	//adding commanders
	for(BattleSide i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if (heroes[i] && heroes[i]->getCommander() && heroes[i]->getCommander()->alive)
		{
			currentBattle->generateNewStack(currentBattle->nextUnitId(), *heroes[i]->getCommander(), i, SlotID::COMMANDER_SLOT_PLACEHOLDER, layout.commanders.at(i));
		}
	}

	if (currentBattle->townID.hasValue())
	{
		if (currentBattle->getDefendedTown()->fortificationsLevel().citadelHealth != 0)
			currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(CreatureID::ARROW_TOWERS, 1), BattleSide::DEFENDER, SlotID::ARROW_TOWERS_SLOT, BattleHex::CASTLE_CENTRAL_TOWER);

		if (currentBattle->getDefendedTown()->fortificationsLevel().upperTowerHealth != 0)
			currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(CreatureID::ARROW_TOWERS, 1), BattleSide::DEFENDER, SlotID::ARROW_TOWERS_SLOT, BattleHex::CASTLE_UPPER_TOWER);

		if (currentBattle->getDefendedTown()->fortificationsLevel().lowerTowerHealth != 0)
			currentBattle->generateNewStack(currentBattle->nextUnitId(), CStackBasicDescriptor(CreatureID::ARROW_TOWERS, 1), BattleSide::DEFENDER, SlotID::ARROW_TOWERS_SLOT, BattleHex::CASTLE_BOTTOM_TOWER);

		//Moat generating is done on server
	}

	std::stable_sort(currentBattle->stacks.begin(), currentBattle->stacks.end(), [cmpst](const auto & left, const auto & right){ return cmpst(left.get(), right.get());});

	auto neutral = std::make_shared<CreatureAlignmentLimiter>(EAlignment::NEUTRAL);
	auto good = std::make_shared<CreatureAlignmentLimiter>(EAlignment::GOOD);
	auto evil = std::make_shared<CreatureAlignmentLimiter>(EAlignment::EVIL);

	const auto * bgInfo = LIBRARY->battlefields()->getById(battlefieldType);

	for(const std::shared_ptr<Bonus> & bonus : bgInfo->bonuses)
	{
		currentBattle->addNewBonus(bonus);
	}

	//native terrain bonuses - some battlefields, such as Cursed Ground, block them
	bool blocksNativeTerrainBonus = std::any_of(bgInfo->bonuses.begin(), bgInfo->bonuses.end(), [](const std::shared_ptr<Bonus> & bonus)
	{
		return bonus->type == BonusType::BLOCK_NATIVE_TERRAIN_BONUS;
	});

	if(!blocksNativeTerrainBonus)
	{
		auto nativeTerrain = std::make_shared<AllOfLimiter>();
		nativeTerrain->add(std::make_shared<TerrainLimiter>());
		nativeTerrain->add(std::make_shared<CreatureLevelLimiter>()); // creature only limiter - exclude hero

		currentBattle->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::STACKS_SPEED, BonusSource::TERRAIN_NATIVE, 1,  BonusSourceID())->addLimiter(nativeTerrain));
		currentBattle->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::TERRAIN_NATIVE, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::ATTACK))->addLimiter(nativeTerrain));
		currentBattle->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::PRIMARY_SKILL, BonusSource::TERRAIN_NATIVE, 1, BonusSourceID(), BonusSubtypeID(PrimarySkill::DEFENSE))->addLimiter(nativeTerrain));
	}
	//////////////////////////////////////////////////////////////////////////

	//tactics
	BattleSideArray<int> battleRepositionHex = {};
	BattleSideArray<int> battleRepositionHexBlock = {};
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		if(heroes[i])
		{
			if(heroes[i]->tacticFormationEnabled)
				battleRepositionHex[i] += heroes[i]->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION);
			battleRepositionHexBlock[i] += heroes[i]->valOfBonuses(BonusType::BEFORE_BATTLE_REPOSITION_BLOCK);
		}
	}
	int tacticsSkillDiffAttacker = battleRepositionHex[BattleSide::ATTACKER] - battleRepositionHexBlock[BattleSide::DEFENDER];
	int tacticsSkillDiffDefender = battleRepositionHex[BattleSide::DEFENDER] - battleRepositionHexBlock[BattleSide::ATTACKER];

	/* for current tactics, we need to choose one side, so, we will choose side when first - second > 0, and ignore sides
	   when first - second <= 0. If there will be situations when both > 0, attacker will be chosen. Anyway, in OH3 this
	   will not happen because tactics block opposite tactics on same value.
	   TODO: For now, it is an error to use BEFORE_BATTLE_REPOSITION bonus without counterpart, but it can be changed if
	   double tactics will be implemented.
	*/

	if(layout.tacticsAllowed)
	{
		if(tacticsSkillDiffAttacker > 0 && tacticsSkillDiffDefender > 0)
			logGlobal->warn("Double tactics is not implemented, only attacker will have tactics!");
		if(tacticsSkillDiffAttacker > 0)
		{
			currentBattle->tacticsSide = BattleSide::ATTACKER;
			//bonus specifies distance you can move beyond base row; this allows 100% compatibility with HMM3 mechanics
			currentBattle->tacticDistance = 1 + tacticsSkillDiffAttacker;
		}
		else if(tacticsSkillDiffDefender > 0)
		{
			currentBattle->tacticsSide = BattleSide::DEFENDER;
			//bonus specifies distance you can move beyond base row; this allows 100% compatibility with HMM3 mechanics
			currentBattle->tacticDistance = 1 + tacticsSkillDiffDefender;
		}
		else
			currentBattle->tacticDistance = 0;
	}

	return currentBattle;
}

const CGHeroInstance * BattleInfo::getHero(const PlayerColor & player) const
{
	for(const auto & side : sides)
		if(side.color == player)
			return side.getHero();

	logGlobal->error("Player %s is not in battle!", player.toString());
	return nullptr;
}

BattleSide BattleInfo::whatSide(const PlayerColor & player) const
{
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(sides[i].color == player)
			return i;

	logGlobal->warn("BattleInfo::whatSide: Player %s is not in battle!", player.toString());
	return BattleSide::NONE;
}

CStack * BattleInfo::getStack(int stackID, bool onlyAlive)
{
	return const_cast<CStack *>(battleGetStackByID(stackID, onlyAlive));
}

BattleInfo::BattleInfo(IGameInfoCallback *cb, const BattleLayout & layout):
	BattleInfo(cb)
{
	*this->layout = layout;
}

BattleInfo::BattleInfo(IGameInfoCallback *cb)
	:CBonusSystemNode(BonusNodeType::BATTLE_WIDE),
	GameCallbackHolder(cb),
	sides({SideInBattle(cb), SideInBattle(cb)}),
	layout(std::make_unique<BattleLayout>()),
	round(-1),
	activeStack(-1),
	tile(-1,-1,-1),
	battlefieldType(BattleField::NONE),
	tacticsSide(BattleSide::NONE),
	tacticDistance(0)
{
	if(cb)
	{
		heroCommandRules = cb->getHeroCommandRules();
		magicRules = cb->getMagicRules();
		creatureCategoryRules = cb->getCreatureCategoryRules();
	}
}

BattleLayout BattleInfo::getLayout() const
{
	return *layout;
}

BattleID BattleInfo::getBattleID() const
{
	return battleID;
}

const IBattleInfo * BattleInfo::getBattle() const
{
	return this;
}

const scripting::Pool & BattleInfo::getScriptContextPool() const
{
	return cb->getScriptContextPool();
}

std::optional<PlayerColor> BattleInfo::getPlayerID() const
{
	return std::nullopt;
}

BattleInfo::~BattleInfo()
{
	stacks.clear();

	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(auto * army = battleGetArmyObject(i); army && army->battle == this)
			army->battle = nullptr;
}

int32_t BattleInfo::getActiveStackID() const
{
	return activeStack;
}

TStacks BattleInfo::getStacksIf(const TStackFilter & predicate) const
{
	TStacks ret;
	for (const auto & stack : stacks)
		if (predicate(stack.get()))
			ret.push_back(stack.get());
	return ret;
}

battle::Units BattleInfo::getUnitsIf(const battle::UnitFilter & predicate) const
{
	battle::Units ret;
	for (const auto & stack : stacks)
		if (predicate(stack.get()))
			ret.push_back(stack.get());
	return ret;
}


BattleField BattleInfo::getBattlefieldType() const
{
	return battlefieldType;
}

TerrainId BattleInfo::getTerrainType() const
{
	return terrainType;
}

IBattleInfo::ObstacleCList BattleInfo::getAllObstacles() const
{
	ObstacleCList ret;

	for(const auto & obstacle : obstacles)
		ret.push_back(obstacle);

	return ret;
}

PlayerColor BattleInfo::getSidePlayer(BattleSide side) const
{
	return getSide(side).color;
}

const CArmedInstance * BattleInfo::getSideArmy(BattleSide side) const
{
	return getSide(side).getArmy();
}

const CGHeroInstance * BattleInfo::getSideHero(BattleSide side) const
{
	return getSide(side).getHero();
}

uint8_t BattleInfo::getTacticDist() const
{
	return tacticDistance;
}

BattleSide BattleInfo::getTacticsSide() const
{
	return tacticsSide;
}

int32_t BattleInfo::getRound() const
{
	return round;
}

const CGTownInstance * BattleInfo::getDefendedTown() const
{
	if (townID.hasValue())
		return cb->getTown(townID);
	return nullptr;
}

EWallState BattleInfo::getWallState(EWallPart partOfWall) const
{
	if(si.canonicalStructuralHP)
		if(const auto it = si.structuralHP.find(partOfWall); it != si.structuralHP.end())
		{
			if(it->second == SiegeInfo::maximumStructuralHP(partOfWall))
				return si.wallState.at(partOfWall); // preserve full-strength REINFORCED visuals
			return SiegeInfo::stateFromStructuralHP(partOfWall, it->second);
		}
	return si.wallState.at(partOfWall);
}

int32_t BattleInfo::getWallStructuralHP(EWallPart partOfWall) const
{
	if(!si.canonicalStructuralHP)
		return 0;
	if(const auto it = si.structuralHP.find(partOfWall); it != si.structuralHP.end())
		return it->second;
	return 0;
}

EGateState BattleInfo::getGateState() const
{
	return si.gateState;
}

int32_t BattleInfo::getCastSpells(BattleSide side) const
{
	return getSide(side).castSpellsCount;
}

int32_t BattleInfo::getEnchanterCounter(BattleSide side) const
{
	return getSide(side).enchanterCounter;
}

bool BattleInfo::getTemporalFieldUsed(BattleSide side) const
{
	return getSide(side).temporalFieldUsed;
}

bool BattleInfo::getCounterspellArmed(BattleSide side) const
{
	return getSide(side).counterspellArmed;
}

int32_t BattleInfo::getMetamagicPendingCount(BattleSide side) const
{
	return getSide(side).metamagicPendingCount;
}

int32_t BattleInfo::getMetamagicUsesConsumed(BattleSide side) const
{
	return getSide(side).metamagicUsesConsumed;
}

bool BattleInfo::getMetamagicGrandUsed(BattleSide side) const
{
	return getSide(side).metamagicGrandUsed;
}

bool BattleInfo::getMetamagicFormulaReserveUsed(BattleSide side) const
{
	return getSide(side).metamagicFormulaReserveUsed;
}

bool BattleInfo::getMetamagicCountersequenceArmed(BattleSide side) const
{
	return getSide(side).metamagicCountersequenceArmed;
}

SpellID BattleInfo::getMetamagicFirstSpell(BattleSide side) const
{
	return getSide(side).metamagicFirstSpell;
}

uint32_t BattleInfo::getMetamagicFirstTargetUnitId(BattleSide side) const
{
	return getSide(side).metamagicFirstTargetUnitId;
}

const std::vector<SpellID> & BattleInfo::getMetamagicSequenceSpells(BattleSide side) const
{
	return getSide(side).metamagicSequenceSpells;
}

bool BattleInfo::getMetamagicFirstCounterspellNegated(BattleSide side) const
{
	return getSide(side).metamagicFirstCounterspellNegated;
}

const IBonusBearer * BattleInfo::getBonusBearer() const
{
	return this;
}

int64_t BattleInfo::getActualDamage(const DamageRange & damage, int32_t attackerCount, vstd::RNG & rng) const
{
	if(damage.min != damage.max)
	{
		int64_t sum = 0;

		auto howManyToAv = std::min<int32_t>(10, attackerCount);

		for(int32_t g = 0; g < howManyToAv; ++g)
			sum += rng.nextInt64(damage.min, damage.max);

		return sum / howManyToAv;
	}
	else
	{
		return damage.min;
	}
}

int3 BattleInfo::getLocation() const
{
	return tile;
}

std::vector<SpellID> BattleInfo::getUsedSpells(BattleSide side) const
{
	return getSide(side).usedSpellsHistory;
}

void BattleInfo::nextRound()
{
	for(auto i : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		sides.at(i).sylvanLuck.nextRound();
		sides.at(i).castSpellsCount = 0;
		sides.at(i).heroCommandUsed = false;
		sides.at(i).activeOrder = HeroCommand::NONE;
		sides.at(i).orderState.reset();
		sides.at(i).focusFire.reset();
		// A sequence is immediate: an unspent follow-up cannot survive into a
		// later round.  The per-combat Metamagic and Grand/Formula budgets remain.
		sides.at(i).clearMetamagicSequence();
		vstd::amax(--sides.at(i).enchanterCounter, 0);
	}
	// first round starts right after pre-battle effects (built-in enchants, OPENING_BATTLE_SPELL)
	// are applied, so skip the decrement here to grant them their full configured duration
	bool isFirstRound = round == 0;
	round += 1;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		sides.at(side).warcastingState = sides.at(side).warcastingState.clearedIfExpired(round);

	for(auto & s : stacks)
	{
		// new turn effects
		if(!isFirstRound && !s->isTimeStopped())
			s->reduceBonusDurations(Bonus::NTurns);

		s->afterNewRound(isFirstRound);
	}

	for(auto & obst : obstacles)
		obst->battleTurnPassed();
}

void BattleInfo::nextTurn(uint32_t unitId, BattleUnitTurnReason reason)
{
	activeStack = unitId;
	if(reason == BattleUnitTurnReason::ACTION_REJECTED)
		return;

	CStack * st = getStack(activeStack);
	if(battleBeginsActivation(st, reason))
	{
		const auto owner = playerToSide(battleGetOwner(st));
		for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
			sides.at(side).sylvanLuck.beginActivation(unitId, side == owner);
	}
	if(st->isTimeStopped() && reason == BattleUnitTurnReason::AUTOMATIC_ACTION)
	{
		// A stopped unit receives a synthetic queue activation solely so the
		// authoritative flow can reach a Hero Action window. Mark that queue
		// slot consumed without changing movement/action permission; the flag is
		// reset by afterNewRound and is used only by turn ordering.
		st->timeStopTurnConsumedFlag = true;
	}

	// A hero/creature spell that does not consume the active unit's turn is a
	// continuation of the same activation.  Keep the Fire Wall activation token
	// stable across those transitions, otherwise a repeated movement callback
	// could deal the same wall damage twice.  HERO_COMMAND is normally another
	// continuation, except for the explicit Second Wind activation whose state
	// is armed before the transition is published.
	bool newActivation = reason != BattleUnitTurnReason::HERO_SPELLCAST
		&& reason != BattleUnitTurnReason::UNIT_SPELLCAST;
	if(reason == BattleUnitTurnReason::HERO_COMMAND)
	{
		const auto & orderState = sides.at(st->unitSide()).orderState;
		newActivation = orderState
			&& orderState->command == HeroCommand::SECOND_WIND
			&& orderState->secondWindActive
			&& orderState->primaryTargetUnitId == unitId;
	}
	if(newActivation && activationSerial < std::numeric_limits<si32>::max())
		++activationSerial;

	if (reason != BattleUnitTurnReason::UNIT_SPELLCAST && reason != BattleUnitTurnReason::HERO_COMMAND)
	{
		//remove bonuses that last until when stack gets new turn
		if(!st->isTimeStopped())
			st->removeBonusesRecursive(Bonus::UntilGetsTurn);
	}

	st->afterGetsTurn(reason);
}

void BattleInfo::addUnit(uint32_t id, const JsonNode & data)
{
	if(heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE) && id != nextUnitId())
		throw std::runtime_error("Invalid New Horizons targeted unit allocation");
	battle::UnitInfo info;
	info.load(id, data);
	if(info.phantomIntegrity < 0 || info.phantomDuration < 0
		|| ((info.phantomIntegrity == 0) != (info.phantomDuration == 0)))
		throw std::runtime_error("Invalid Phantom Army spawn profile");
	if(info.phantomIntegrity > 0
		&& (info.count <= 0 || !info.summoned || info.natureSummoned
			|| info.phantomDuration != newHorizonsSorcery::PHANTOM_ARMY_DURATION_ROUNDS))
		throw std::runtime_error("Invalid Phantom Army spawn profile");

	CStackBasicDescriptor base(info.type, info.count);

	PlayerColor owner = getSidePlayer(info.side);

	auto ret = std::make_unique<CStack>(&base, owner, info.id, info.side, SlotID::SUMMONED_SLOT_PLACEHOLDER);
	ret->initialPosition = info.position;
	// Summon provenance affects inherited-bonus acceptance, so it must be set
	// before localInit attaches the stack to the bonus graph and fills caches.
	ret->summoned = info.summoned;
	ret->natureSummoned = info.natureSummoned;
	stacks.push_back(std::move(ret));
	stacks.back()->localInit(this);
	// CUnitState::localInit resets transient state, including summon provenance.
	// Restore the authoritative packet values before any subsequent bonus query.
	stacks.back()->summoned = info.summoned;
	stacks.back()->natureSummoned = info.natureSummoned;
	if(info.phantomIntegrity > 0)
		stacks.back()->initializePhantomProfile(info.phantomIntegrity, info.phantomDuration);
	stacks.back()->nodeHasChanged();
}

void BattleInfo::moveUnit(uint32_t id, const BattleHex & destination)
{
	auto * sta = getStack(id);
	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}
	if(sta->isTimeStopped())
	{
		logNetwork->warn("Ignoring movement of Time Stop unit %d", id);
		return;
	}
	if(sta->getPosition() != destination)
	{
		for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			auto & state = sides.at(side).orderState;
			if(!state || state->command != HeroCommand::HOLD_THE_LINE || state->containsHoldBroken(id))
				continue;
			if(const auto * anchor = state->anchorFor(id); anchor && anchor->position != destination.toInt())
			{
				state->holdBrokenUnitIds.insert(std::lower_bound(state->holdBrokenUnitIds.begin(), state->holdBrokenUnitIds.end(), id), id);
				break;
			}
		}
	}
	sta->position = destination;
	expireSeparatedHeroOrderProtect();
	//Bonuses can be limited by unit placement, so, change tree version
	//to force updating a bonus. TODO: update version only when such bonuses are present
	nodeHasChanged();
}

void BattleInfo::updateUnit(uint32_t id, const JsonNode & data, int64_t healthDelta)
{
	CStack * changedStack = getStack(id, false);
	if(!changedStack)
		throw std::runtime_error("Invalid unit id in BattleInfo update");
	if(changedStack->isTimeStopped() && healthDelta != 0)
	{
		logNetwork->warn("Ignoring health update of Time Stop unit %d", id);
		return;
	}
	if(changedStack->isTimeStopped())
	{
		// UnitChanges also carries a serialized state.  A stopped unit cannot
		// move, wait, defend, cast, or otherwise mutate activation state; the
		// authoritative flow advances it with a no-op instead.  Reject all
		// external state updates until the marker is released.
		logNetwork->warn("Ignoring state update of Time Stop unit %d", id);
		return;
	}

	if(!changedStack->alive() && healthDelta > 0)
	{
		//checking if we resurrect a stack that is under a living stack
		auto accessibility = getAccessibility();

		if(!accessibility.accessible(changedStack->getPosition(), changedStack))
		{
			logNetwork->error("Cannot resurrect %s because hex %d is occupied!", changedStack->nodeName(), changedStack->getPosition());
			return; //position is already occupied
		}
	}

	bool killed = (-healthDelta) >= changedStack->getAvailableHealth();//todo: check using alive state once rebirth will be handled separately

	bool resurrected = !changedStack->alive() && healthDelta > 0;

	//applying changes
	changedStack->load(data);
	expireSeparatedHeroOrderProtect();


	if(healthDelta < 0)
	{
		changedStack->removeBonusesRecursive(Bonus::UntilBeingAttacked);
	}

	if(healthDelta < 0)
	{
		changedStack->nodeHasChanged();	//bonuses with TIMES_STACK_SIZE updater may change
	}

	resurrected = resurrected || (killed && changedStack->alive());

	if(killed)
	{
		if(changedStack->cloneID >= 0)
		{
			//remove clone as well
			CStack * clone = getStack(changedStack->cloneID);
			if(clone)
				clone->makeGhost();

			changedStack->cloneID = -1;
		}
	}

	if(resurrected || killed)
	{
		//removing all spells effects
		auto selector = [](const Bonus * b)
		{
			//Special case: persistent effects, such as DISRUPTING_RAY, survive death
			return b->source == BonusSource::SPELL_EFFECT && !b->sid.as<SpellID>().toSpell()->isPersistent();
		};
		changedStack->removeBonusesRecursive(selector);
	}

	if(!changedStack->alive() && changedStack->isClone())
	{
		for(auto & s : stacks)
		{
			if(s->cloneID == changedStack->unitId())
				s->cloneID = -1;
		}
	}
}

void BattleInfo::removeUnit(uint32_t id)
{
	std::set<uint32_t> ids;
	ids.insert(id);

	while(!ids.empty())
	{
		auto toRemoveId = *ids.begin();
		auto * toRemove = getStack(toRemoveId, false);

		if(!toRemove)
		{
			logGlobal->error("Cannot find stack %d", toRemoveId);
			return;
		}

		if(!toRemove->ghost)
		{
			toRemove->onRemoved();
			toRemove->detachFromAll();

			//stack may be removed instantly (not being killed first)
			//handle clone remove also here
			if(toRemove->cloneID >= 0)
			{
				ids.insert(toRemove->cloneID);
				toRemove->cloneID = -1;
			}

			//cleanup remaining clone links if any
			for(const auto & s : stacks)
			{
				if(s->cloneID == toRemoveId)
					s->cloneID = -1;
			}
		}

		ids.erase(toRemoveId);
	}
	expireSeparatedHeroOrderProtect();
}

void BattleInfo::addUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}

	for(const Bonus & b : bonus)
		addOrUpdateUnitBonus(sta, b, true);
}

void BattleInfo::updateUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}

	for(const Bonus & b : bonus)
		addOrUpdateUnitBonus(sta, b, false);
}

void BattleInfo::removeUnitBonus(uint32_t id, const std::vector<Bonus> & bonus)
{
	CStack * sta = getStack(id, false);

	if(!sta)
	{
		logGlobal->error("Cannot find stack %d", id);
		return;
	}

	for(const Bonus & one : bonus)
	{
		if(sta->isTimeStopped() && !isTimeStopStateBonus(one))
		{
			logNetwork->warn("Ignoring effect removal from Time Stop unit %d", id);
			continue;
		}

		auto selector = [one](const Bonus * b)
		{
			//compare everything but turnsRemain, limiter and propagator
			return one.duration == b->duration
			&& one.type == b->type
			&& one.subtype == b->subtype
			&& one.source == b->source
			&& one.val == b->val
			&& one.sid == b->sid
			&& one.valType == b->valType
			&& one.effectRange == b->effectRange;
		};
		sta->removeBonusesRecursive(selector);
	}
}

void BattleInfo::expireTimeStops(BattleSide casterSide)
{
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return;

	for(auto & stack : stacks)
	{
		if(!stack)
			continue;

		stack->removeBonusesRecursive(CSelector([casterSide](const Bonus * bonus)
		{
			return bonus && isTimeStopStateBonus(*bonus)
				&& timeStopBelongsToSide(*bonus, casterSide);
		}));
	}

	clearPendingTimeStopHeroAction(casterSide);
}

namespace
{
ui8 timeStopSideBit(BattleSide side)
{
	if(side == BattleSide::ATTACKER)
		return 1u;
	if(side == BattleSide::DEFENDER)
		return 2u;
	return 0u;
}
}

ui8 BattleInfo::getPendingTimeStopHeroActionSides() const
{
	ui8 result = pendingTimeStopHeroActionSides;
	// Saves written with the first Time Stop format only have the scalar field.
	// Treat that field as a one-origin mask until the battle reaches a new
	// serialization boundary and can persist the complete set.
	if(result == 0)
		result = timeStopSideBit(pendingTimeStopHeroActionSide);
	return result;
}

BattleSide BattleInfo::getPendingTimeStopHeroActionSide() const
{
	const auto pending = getPendingTimeStopHeroActionSides();
	if(pending & timeStopSideBit(BattleSide::ATTACKER))
		return BattleSide::ATTACKER;
	if(pending & timeStopSideBit(BattleSide::DEFENDER))
		return BattleSide::DEFENDER;
	return BattleSide::NONE;
}

bool BattleInfo::hasPendingTimeStopHeroAction(BattleSide side) const
{
	return (getPendingTimeStopHeroActionSides() & timeStopSideBit(side)) != 0;
}

void BattleInfo::notePendingTimeStopHeroAction(BattleSide side)
{
	const auto bit = timeStopSideBit(side);
	if(!bit)
		return;

	pendingTimeStopHeroActionSides = getPendingTimeStopHeroActionSides() | bit;
	if(pendingTimeStopHeroActionSide == BattleSide::NONE)
		pendingTimeStopHeroActionSide = side;
}

void BattleInfo::clearPendingTimeStopHeroAction(BattleSide side)
{
	const auto bit = timeStopSideBit(side);
	if(!bit)
		return;

	pendingTimeStopHeroActionSides = getPendingTimeStopHeroActionSides() & static_cast<ui8>(~bit);
	if(pendingTimeStopHeroActionSides == 0)
		pendingTimeStopHeroActionSide = BattleSide::NONE;
	else if(pendingTimeStopHeroActionSide == side)
		pendingTimeStopHeroActionSide = getPendingTimeStopHeroActionSide();
}

uint32_t BattleInfo::nextUnitId() const
{
	if(heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE)
		&& stacks.size() > static_cast<size_t>(std::numeric_limits<int32_t>::max()))
		throw std::runtime_error("New Horizons targeted unit identities exhausted");
	return static_cast<uint32_t>(stacks.size());
}

void BattleInfo::addOrUpdateUnitBonus(CStack * sta, const Bonus & value, bool forceAdd)
{
	if(sta->isTimeStopped() && !isTimeStopStateBonus(value))
	{
		logNetwork->warn("Ignoring new effect on Time Stop unit %d", sta->unitId());
		return;
	}

	if(forceAdd || !sta->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, value.sid).And(Selector::typeSubtypeValueType(value.type, value.subtype, value.valType))))
	{
		//no such effect or cumulative - add new
		logBonus->trace("%s receives a new bonus: %s", sta->nodeName(), value.Description(nullptr));
		sta->addNewBonus(std::make_shared<Bonus>(value));
	}
	else
	{
		logBonus->trace("%s updated bonus: %s", sta->nodeName(), value.Description(nullptr));

		for(const auto & stackBonus : sta->getExportedBonusList()) //TODO: optimize
		{
			if(stackBonus->source == value.source && stackBonus->sid == value.sid && stackBonus->type == value.type && stackBonus->subtype == value.subtype && stackBonus->valType == value.valType)
			{
				stackBonus->turnsRemain = std::max(stackBonus->turnsRemain, value.turnsRemain);
			}
		}
		sta->nodeHasChanged();
	}
}

void BattleInfo::setWallState(EWallPart partOfWall, EWallState state)
{
	si.wallState[partOfWall] = state;
	if(si.canonicalStructuralHP && state == EWallState::DESTROYED)
		si.structuralHP[partOfWall] = 0;
}

void BattleInfo::setWallStructuralHP(EWallPart partOfWall, int32_t hp)
{
	if(!si.canonicalStructuralHP)
		return;
	si.structuralHP[partOfWall] = std::clamp(hp, 0, SiegeInfo::maximumStructuralHP(partOfWall));
	si.wallState[partOfWall] = SiegeInfo::stateFromStructuralHP(partOfWall, si.structuralHP[partOfWall]);
}

void BattleInfo::addObstacle(const ObstacleChanges & changes)
{
	auto obstacle = std::make_shared<SpellCreatedObstacle>();
	obstacle->fromInfo(changes);
	obstacles.push_back(obstacle);
}

void BattleInfo::updateObstacle(const ObstacleChanges& changes)
{
	auto changedObstacle = std::make_shared<SpellCreatedObstacle>();
	changedObstacle->fromInfo(changes);

	for(auto & obstacle : obstacles)
	{
		if(obstacle->uniqueID == changes.id) // update this obstacle
		{
			auto * spellObstacle = dynamic_cast<SpellCreatedObstacle *>(obstacle.get());
			assert(spellObstacle);

			// Most legacy obstacle updates only change visibility.  Canonical
			// New Horizons Fire Wall also publishes its per-activation trigger
			// token, which must be retained on the authoritative obstacle or a
			// repeated movement callback could deal damage twice.
			spellObstacle->revealed = changedObstacle->revealed;
			spellObstacle->lastTriggerUnit = changedObstacle->lastTriggerUnit;
			spellObstacle->lastTriggerActivation = changedObstacle->lastTriggerActivation;

			break;
		}
	}
}

void BattleInfo::removeObstacle(uint32_t id)
{
	for(int i=0; i < obstacles.size(); ++i)
	{
		if(obstacles[i]->uniqueID == id) //remove this obstacle
		{
			obstacles.erase(obstacles.begin() + i);
			break;
		}
	}
}

CArmedInstance * BattleInfo::battleGetArmyObject(BattleSide side) const
{
	return const_cast<CArmedInstance*>(CBattleInfoEssentials::battleGetArmyObject(side));
}

CGHeroInstance * BattleInfo::battleGetFightingHero(BattleSide side) const
{
	return const_cast<CGHeroInstance*>(CBattleInfoEssentials::battleGetFightingHero(side));
}

void BattleInfo::validateFocusFireStates() const
{
	std::set<uint32_t> unitIds;
	const bool targetedRules = heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE);
	for(const auto & unit : stacks)
	{
		if(!unit)
			throw std::runtime_error("Invalid null battle unit reference");
		// nextUnitId allocates by vector size. Unique IDs below size imply dense
		// coverage even after sorting; removed units remain as retained descriptors.
		if(targetedRules && (unit->unitId() > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
			|| unit->unitId() >= stacks.size() || !unitIds.insert(unit->unitId()).second))
			throw std::runtime_error("Invalid New Horizons targeted battle unit identity");
	}
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto & state = sides.at(side);
		if((state.activeDoctrine != HeroCommand::NONE
				&& (!heroCommands::isDoctrine(state.activeDoctrine)
					|| !heroCommands::supportedByRules(heroCommandRules, state.activeDoctrine)))
			|| (state.activeOrder != HeroCommand::NONE
				&& (heroCommands::isDoctrine(state.activeOrder)
					|| !heroCommands::supportedByRules(heroCommandRules, state.activeOrder)
					|| !state.heroCommandUsed || state.castSpellsCount != 0)))
			throw std::runtime_error("Invalid New Horizons saved command state");
		if(state.orderState)
		{
			state.orderState->validateShape();
			if(!heroCommands::isCanonicalRules(heroCommandRules)
				|| state.orderState->command != state.activeOrder
				|| state.orderState->issuedRound != round
				|| !state.heroCommandUsed || state.castSpellsCount != 0
				|| !heroCommands::supportedByRules(heroCommandRules, state.orderState->command))
				throw std::runtime_error("Invalid New Horizons canonical Order context");
			if(state.orderState->primaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID
				&& !battleGetUnitByID(state.orderState->primaryTargetUnitId))
				throw std::runtime_error("Invalid New Horizons canonical Order target");
			if(state.orderState->secondaryTargetUnitId != HeroOrderState::INVALID_UNIT_ID
				&& !battleGetUnitByID(state.orderState->secondaryTargetUnitId))
				throw std::runtime_error("Invalid New Horizons canonical Order secondary target");
		}
		else if(heroCommands::isCanonicalRules(heroCommandRules) && state.activeOrder != HeroCommand::NONE)
			throw std::runtime_error("Missing New Horizons canonical Order state");
		if(state.focusFire.has_value() != (state.activeOrder == HeroCommand::FOCUS_FIRE))
			throw std::runtime_error("Inconsistent New Horizons Focus Fire order state");
		if(!state.focusFire)
			continue;
		const auto & mark = *state.focusFire;
		mark.validateShape();
		if(!heroCommands::supportedByRules(heroCommandRules, HeroCommand::FOCUS_FIRE)
			|| !getSideHero(side) || !state.heroCommandUsed || state.castSpellsCount != 0 || mark.issuedRound != round
			|| !battleGetUnitByID(mark.targetUnitId))
			throw std::runtime_error("Invalid New Horizons Focus Fire battle context");
		for(auto id : mark.recipientUnitIds)
		{
			// Ghosts and changed controllers are valid retained, possibly inactive references.
			if(!battleGetUnitByID(id))
				throw std::runtime_error("Invalid New Horizons Focus Fire recipient reference");
		}
	}
}

void BattleInfo::normalizeLegacyHeroCommandState()
{
	for(auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		sides.at(side).activeDoctrine = HeroCommand::NONE;
		const bool invalidLegacyOrder = sides.at(side).activeOrder != HeroCommand::NONE
			&& !heroCommands::isActive(sides.at(side).activeOrder);
		if(invalidLegacyOrder)
			sides.at(side).activeOrder = HeroCommand::NONE;
		if(sides.at(side).orderState && (!heroCommands::isCanonicalRules(heroCommandRules)
			|| sides.at(side).orderState->command != sides.at(side).activeOrder))
			sides.at(side).orderState.reset();
		if(invalidLegacyOrder)
		{
			const auto legacyRoundOrder = Selector::sourceTypeSel(BonusSource::HERO_COMMAND)
				.And(CSelector(Bonus::NTurns));
			for(auto & unit : stacks)
				if(unit && unit->unitSide() == side)
					unit->removeBonusesRecursive(legacyRoundOrder);
		}
	}

	// Doctrine effects were the only HERO_COMMAND bonuses with ONE_BATTLE
	// duration. Remove those stale effects from decoded snapshots while leaving
	// all round-scoped Order bonuses untouched.
	const auto legacyDoctrine = Selector::sourceTypeSel(BonusSource::HERO_COMMAND)
		.And(CSelector(Bonus::OneBattle));
	for(auto & unit : stacks)
		if(unit)
			unit->removeBonusesRecursive(legacyDoctrine);
}

void BattleInfo::postDeserialize()
{
	for (const auto & unit : stacks)
		unit->postDeserialize(getSideArmy(unit->unitSide()));
}

bool CMP_stack::operator()(const battle::Unit * a, const battle::Unit * b) const
{
	switch(phase)
	{
	case 0: //catapult moves after turrets
		return a->isTurret() && !b->isTurret(); //turrets move before catapult
	case 1:
	case 2:
	case 3:
		{
			int as = a->getInitiative(turn);
			int bs = b->getInitiative(turn);

			if(as != bs)
				return as > bs;

			if(a->unitSide() == b->unitSide())
				return a->unitSlot() < b->unitSlot();

			return (a->unitSide() == side || b->unitSide() == side)
				? a->unitSide() != side
				: a->unitSide() < b->unitSide();
			}
	default:
		assert(false);
		return false;
	}

	assert(false);
	return false;
}

CMP_stack::CMP_stack(int Phase, int Turn, BattleSide Side):
	phase(Phase), 
	turn(Turn), 
	side(Side) 
{
}
