/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleInfo.h"
#include "UnitAdditionRules.h"
#include "UnitTransitionSidecars.h"
#include "NewHorizonsElementalRebirth.h"
#include "NewHorizonsOffense.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "../CStack.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "NewHorizonsPrismaticVanguard.h"

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

void BattleInfo::addUnit(uint32_t id, const JsonNode & data)
{
	const auto info = battle::decodeUnitAddition(id, data, heroCommandRules, stacks.size());
	if(info.rebirthOriginalAggregateHP > 0)
	{
		battle::validateRebirthAdditionSide(info);
		const auto effectiveMaxHP = newHorizonsElementalRebirth::effectiveSummonMaxHP(
			getSideArmy(info.side), info.type, getSidePlayer(info.side), info.side);
		battle::validateUnitAdditionCapacity(info, effectiveMaxHP);
	}

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
	stacks.back()->summonRoundsRemaining = info.summonRoundsRemaining;
	stacks.back()->initializeRebirthOriginalAggregateHP(info.rebirthOriginalAggregateHP);
	if(const auto entry = newHorizonsPrismaticVanguard::entryBonus(getSideHero(info.side), info.type))
		stacks.back()->addNewBonus(std::make_shared<Bonus>(*entry));
	if(info.phantomIntegrity > 0)
		stacks.back()->initializePhantomProfile(info.phantomIntegrity, info.phantomDuration);
	const auto * orderState = sides.at(info.side).findOrder(HeroCommand::RIPOSTE);
	const auto * hero = battleGetFightingHero(info.side);
	if(battle::unitAdditionNeedsVengeance(*stacks.back(), orderState, round, hero))
		stacks.back()->addNewBonus(std::make_shared<Bonus>(newHorizonsOffense::vengeanceRetaliationBonus()));
	// Existing recipients may have cached limiters that inspect the roster.
	nodeHasChanged();
}

void BattleInfo::removeUnit(uint32_t id)
{
	bool rosterChanged = false;
	std::set<uint32_t> ids;
	ids.insert(id);

	while(!ids.empty())
	{
		auto toRemoveId = *ids.begin();
		auto * toRemove = getStack(toRemoveId, false);

		if(!toRemove)
		{
			logGlobal->error("Cannot find stack %d", toRemoveId);
			if(rosterChanged)
				nodeHasChanged();
			return;
		}

		newHorizonsSerpentsEmbrace::releaseDeathBindings(*this, *toRemove);
		if(!toRemove->ghost)
		{
			toRemove->onRemoved();
			toRemove->detachFromAll();
			rosterChanged = true;

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
	if(rosterChanged)
		nodeHasChanged();
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		battle::clearReducedActivationForUnit(sides.at(side).reducedExtraActivation, id);
	}
	expireSeparatedHeroOrderProtect();
}
