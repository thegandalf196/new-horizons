/*
 * NewHorizonsLegendaryReputation.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsLegendaryReputation.h"
#include "NewHorizonsDiplomacy.h"
#include "../../callback/Calendar.h"
#include "../../CCreatureHandler.h"
#include "../../GameLibrary.h"
#include "../creature/NewHorizonsRecruitmentTraining.h"
#include "../../gameState/CGameState.h"
#include "../../mapObjects/CGCreature.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../networkPacks/PacksForClient.h"

#include <array>
#include <limits>
#include <map>

namespace newHorizonsDiplomacy
{
namespace
{
CGHeroInstance & validateOffer(CGameState & state, const LegendaryAdmission & receipt)
{
	receipt.validate();
	auto * hero = state.getHero(receipt.hero);
	const auto * source = dynamic_cast<const CGCreature *>(state.getObjInstance(receipt.source));
	const auto * stack = source ? source->getStackPtr(receipt.sourceSlot) : nullptr;
	const auto * creature = stack ? stack->getCreature() : nullptr;
	if(!hero || !source || !creature || !hero->tempOwner.isValidPlayer()
		|| source->refusedJoining || !source->diplomacyEligible
		|| (source->tempOwner != PlayerColor::NEUTRAL && source->tempOwner != PlayerColor::UNFLAGGABLE)
		|| source->initialCharacter == CGCreature::Character::SAVAGE
		|| (source->initialCharacter == CGCreature::Character::COMPLIANT && !source->joinOnlyForMoney)
		|| receipt.month != state.getCalendar().getMonth()
		|| hero->getNewHorizonsLegendaryReputationLastMonth() != receipt.previousMonth
		|| !hero->canUseNewHorizonsLegendaryReputation(receipt.month)
		|| (receipt.recruitmentPactApplied && !hero->hasActivePerk(SKILL_ID, RECRUITMENT_PACT_ID))
		|| source->getStackCount(receipt.sourceSlot) != receipt.sourceQuantity
		|| stack->getCreatureID() != receipt.creature)
		throw std::runtime_error("Stale or ineligible Legendary Reputation admission");

	// Recheck the ORIGINAL accepted cohort, not unrelated troops returned to
	// another neutral slot while its ordinary garrison dialog remains open.
	ForecastInput input;
	input.usesNewHorizonsRules = usesNewHorizonsRules(hero->getPerkState().rules);
	input.encounterEligible = true;
	input.skillRank = hero->getPerkSkillRank(SKILL_ID);
	input.negotiator = hero->hasActivePerk(SKILL_ID, NEGOTIATOR_ID);
	input.grandDiplomat = hero->hasActivePerk(SKILL_ID, GRAND_DIPLOMAT_ID);
	input.commonCause = hero->hasActivePerk(SKILL_ID, COMMON_CAUSE_ID)
		&& hero->getFactionID() == creature->getFactionID();
	input.recruitmentPact = receipt.recruitmentPactApplied;
	input.legendaryReputation = true;
	input.heroArmyValue = hero->getArmyStrength();
	input.joiningAmount = receipt.originalQuantity;
	input.goldCostPerCreature = creature->getRecruitCost(EGameResID::GOLD);
	input.creatureArmyValue = receipt.originalArmyValue;
	const auto forecast = resolveForecast(input);
	if(!forecast.legendaryReputation || forecast.normalGoldCost != receipt.normalGoldCost)
		throw std::runtime_error("Legendary Reputation original offer no longer qualifies");
	return *hero;
}

struct ProjectedSlot
{
	CreatureID creature = CreatureID::NONE;
	int64_t count = 0;
	newHorizonsTraining::Receipt training;
};

class Projection
{
	CGameState & state;
	std::map<ObjectInstanceID, std::array<ProjectedSlot, GameConstants::ARMY_SIZE>> armies;

public:
	explicit Projection(CGameState & state) : state(state) {}

	auto & army(ObjectInstanceID id)
	{
		if(!armies.count(id))
		{
			const auto * source = state.getArmyInstance(id);
			if(!source)
				throw std::runtime_error("Invalid Legendary Reputation transaction army");
			auto & rows = armies[id];
			for(const auto & [slot, stack] : source->Slots())
			{
				if(!slot.validSlot()) throw std::runtime_error("Invalid army slot");
				rows[slot.getNum()] = {stack->getCreatureID(), stack->getCount(), stack->getTrainingReceipt()};
				rows[slot.getNum()].training.validate();
			}
		}
		return armies.at(id);
	}

	ProjectedSlot & slot(ObjectInstanceID id, SlotID position)
	{
		if(!position.validSlot())
			throw std::runtime_error("Invalid Legendary Reputation transaction slot");
		return army(id)[position.getNum()];
	}

	void validateArmy(ObjectInstanceID id)
	{
		const auto * owner = state.getArmyInstance(id);
		const auto * hero = dynamic_cast<const CGHeroInstance *>(owner);
		bool occupied = false;
		for(const auto & row : army(id))
		{
			if(row.count == 0) continue;
			occupied = true;
			if(row.creature.getNum() < 0 || static_cast<size_t>(row.creature.getNum()) >= LIBRARY->creh->objects.size()
				|| !LIBRARY->creh->objects[row.creature.getNum()])
				throw std::runtime_error("Invalid Diplomacy transaction creature");
			row.training.validate();
			if(row.count < 0 || row.count > std::numeric_limits<TQuantity>::max())
				throw std::runtime_error("Legendary Reputation transaction count overflow");
			if(hero)
				if(const auto capacity = hero->getLeadershipSlotCapacity(row.creature);
					capacity && row.count > capacity->maximum)
					throw std::runtime_error("Legendary Reputation transaction exceeds Leadership");
		}
		if(owner->needsLastStack() && !occupied)
			throw std::runtime_error("Legendary Reputation transaction removes the last creature");
	}

	void validateOrigin(ObjectInstanceID sourceID, ObjectInstanceID destinationID,
		SlotID destinationSlot, int64_t count, ObjectInstanceID origin)
	{
		if(origin.getNum() < -1)
			throw std::runtime_error("Invalid accepted Diplomacy admission");
		if(!origin.hasValue()) return;
		const auto * source = dynamic_cast<const CGCreature *>(state.getObjInstance(sourceID));
		const auto * destination = state.getHero(destinationID);
		if(!source || !destination || destination->id != origin || sourceID == destinationID
			|| !destinationSlot.validSlot() || count <= 0
			|| (source->tempOwner != PlayerColor::NEUTRAL && source->tempOwner != PlayerColor::UNFLAGGABLE)
			|| !usesNewHorizonsRules(destination->getPerkState().rules))
			throw std::runtime_error("Invalid accepted Diplomacy admission");
	}

	void move(const RebalanceStacks & move)
	{
		auto & source = slot(move.srcArmy, move.srcSlot);
		auto & destination = slot(move.dstArmy, move.dstSlot);
		if(&source == &destination || move.count <= 0 || source.count < move.count
			|| (destination.count > 0 && destination.creature != source.creature)
			|| destination.count > std::numeric_limits<TQuantity>::max() - move.count)
			throw std::runtime_error("Invalid Legendary Reputation transfer");
		validateOrigin(move.srcArmy, move.dstArmy, move.dstSlot, move.count, move.diplomacyRecruiter);
		auto movedTraining = source.training;
		if(move.srcArmy != move.dstArmy) movedTraining.crossedArmyBoundary();
		if(move.diplomacyRecruiter.hasValue()) movedTraining.admittedThroughDiplomacy(move.diplomacyRecruiter);
		if(destination.count > 0) destination.training.merge(movedTraining);
		else destination.training = std::move(movedTraining);
		destination.creature = source.creature;
		destination.count += move.count;
		source.count -= move.count;
		if(source.count == 0) source = {};
		validateArmy(move.srcArmy);
		validateArmy(move.dstArmy);
	}

	void swap(const SwapStacks & swap)
	{
		auto & source = slot(swap.srcArmy, swap.srcSlot);
		auto & destination = slot(swap.dstArmy, swap.dstSlot);
		if(&source == &destination || (source.count <= 0 && destination.count <= 0))
			throw std::runtime_error("Invalid Legendary Reputation swap");
		if(swap.diplomacyRecruiter.getNum() < -1
			|| (swap.diplomacyRecruiter.hasValue() && swap.diplomacyRecruiter != swap.srcArmy && swap.diplomacyRecruiter != swap.dstArmy))
			throw std::runtime_error("Invalid accepted Diplomacy swap origin");
		if(swap.diplomacyRecruiter.hasValue())
		{
			const bool intoDestination = swap.diplomacyRecruiter == swap.dstArmy;
			validateOrigin(intoDestination ? swap.srcArmy : swap.dstArmy, swap.diplomacyRecruiter,
				intoDestination ? swap.dstSlot : swap.srcSlot,
				intoDestination ? source.count : destination.count, swap.diplomacyRecruiter);
		}
		if(swap.srcArmy != swap.dstArmy)
		{
			source.training.crossedArmyBoundary();
			destination.training.crossedArmyBoundary();
			if(swap.diplomacyRecruiter == swap.dstArmy) source.training.admittedThroughDiplomacy(swap.diplomacyRecruiter);
			if(swap.diplomacyRecruiter == swap.srcArmy) destination.training.admittedThroughDiplomacy(swap.diplomacyRecruiter);
		}
		std::swap(source, destination);
		validateArmy(swap.srcArmy);
		validateArmy(swap.dstArmy);
	}
};
}

PreparedGarrisonAdmission prepareGarrisonAdmission(CGameState & state, const RebalanceStacks & pack)
{
	if(!pack.legendaryAdmission)
	{
		if(pack.diplomacyRecruiter != ObjectInstanceID::NONE)
		{
			Projection projection(state);
			projection.move(pack);
		}
		return {};
	}
	const auto & receipt = *pack.legendaryAdmission;
	auto & hero = validateOffer(state, receipt);
	if(pack.srcArmy != receipt.source || pack.srcSlot != receipt.sourceSlot
		|| pack.dstArmy != receipt.hero || pack.count <= 0)
		throw std::runtime_error("Legendary Reputation receipt does not describe this admission");
	Projection projection(state);
	projection.move(pack);
	return {&hero, receipt.month};
}

PreparedGarrisonAdmission prepareGarrisonAdmission(CGameState & state, const BulkRebalanceStacks & pack)
{
	for(const auto & move : pack.moves)
		if(move.legendaryAdmission)
			throw std::runtime_error("Nested Legendary Reputation admission receipt");
	if(!pack.legendaryAdmission)
	{
		if(std::ranges::any_of(pack.moves, [](const auto & move) { return move.diplomacyRecruiter != ObjectInstanceID::NONE; }))
		{
			Projection projection(state);
			for(const auto & move : pack.moves) projection.move(move);
		}
		return {};
	}
	const auto & receipt = *pack.legendaryAdmission;
	auto & hero = validateOffer(state, receipt);
	Projection projection(state);
	SlotID offeredSlot = receipt.sourceSlot;
	bool admitted = false;
	for(const auto & move : pack.moves)
	{
		if(move.srcArmy == receipt.source && move.srcSlot == offeredSlot)
		{
			if(move.dstArmy == receipt.hero && move.count > 0)
				admitted = true;
			if(move.dstArmy == receipt.source)
			{
				if(move.count != projection.slot(move.srcArmy, move.srcSlot).count)
					throw std::runtime_error("Cannot split the accepted cohort internally");
				offeredSlot = move.dstSlot;
			}
		}
		projection.move(move);
	}
	if(!admitted)
		throw std::runtime_error("Legendary Reputation bulk receipt has no positive original intake");
	return {&hero, receipt.month};
}

PreparedGarrisonAdmission prepareGarrisonAdmission(CGameState & state, const SwapStacks & pack)
{
	if(!pack.legendaryAdmission)
	{
		if(pack.diplomacyRecruiter != ObjectInstanceID::NONE)
		{
			Projection projection(state);
			projection.swap(pack);
		}
		return {};
	}
	const auto & receipt = *pack.legendaryAdmission;
	auto & hero = validateOffer(state, receipt);
	if(!((pack.srcArmy == receipt.source && pack.srcSlot == receipt.sourceSlot && pack.dstArmy == receipt.hero)
		|| (pack.dstArmy == receipt.source && pack.dstSlot == receipt.sourceSlot && pack.srcArmy == receipt.hero)))
		throw std::runtime_error("Legendary Reputation receipt does not describe this swap");
	Projection projection(state);
	projection.swap(pack);
	return {&hero, receipt.month};
}
void PreparedGarrisonAdmission::commit() const
{
	if(hero) hero->markNewHorizonsLegendaryReputationUsed(month);
}
}
