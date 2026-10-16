/*
 * HeroQueueBonusSnapshot.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroQueueBonusSnapshot.h"
#include "../../lib/battle/BattleHexArray.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/bonuses/Limiters.h"
#include "../../lib/bonuses/Propagators.h"
#include "../../lib/bonuses/Updaters.h"
#include "../../lib/serializer/CMemorySerializer.h"

#include <stdexcept>
#include <cstdint>
#include <typeinfo>
#include <utility>
#include <variant>

namespace heroQueueTest
{
namespace
{
struct Graph
{
	unsigned remaining = 256;
	std::vector<PlayerColor> owners;
	std::vector<std::vector<uint8_t>> hexMembership;

	void visit(unsigned depth)
	{
		if(depth >= 16 || remaining == 0)
			throw std::runtime_error("Unsupported snapshot graph depth or size");
		--remaining;
	}
};

void requireUpdater(const TUpdaterPtr & updater, Graph & graph, unsigned depth)
{
	graph.visit(depth);
	if(!updater)
		return;
	const auto & kind = typeid(*updater);
	if(kind == typeid(CompositeUpdater))
	{
		for(const auto & nested : static_cast<const CompositeUpdater &>(*updater).updaters)
			requireUpdater(nested, graph, depth + 1);
		return;
	}
	if(kind == typeid(IUpdater) || kind == typeid(GrowsWithLevelUpdater)
		|| kind == typeid(TimesHeroLevelUpdater) || kind == typeid(TimesArmySizeUpdater)
		|| kind == typeid(TimesStackLevelUpdater) || kind == typeid(DivideStackLevelUpdater)
		|| kind == typeid(TimesHeroLevelDivideStackLevelUpdater) || kind == typeid(OwnerUpdater))
		return;
	// TimesStackSizeUpdater's private stepValue is absent from its runtime serializer.
	throw std::runtime_error("Unsupported snapshot updater graph");
}

void requireLimiter(const TLimiterPtr & limiter, Graph & graph, unsigned depth)
{
	graph.visit(depth);
	if(!limiter)
		return;
	const auto & kind = typeid(*limiter);
	if(kind == typeid(AllOfLimiter) || kind == typeid(AnyOfLimiter) || kind == typeid(NoneOfLimiter))
	{
		for(const auto & nested : static_cast<const AggregateLimiter &>(*limiter).limiters)
			requireLimiter(nested, graph, depth + 1);
		return;
	}
	if(kind == typeid(UnitOnHexLimiter))
	{
		const auto & hexes = static_cast<const UnitOnHexLimiter &>(*limiter).applicableHexes;
		std::vector<uint8_t> membership;
		for(int cell = 0; cell < GameConstants::BFIELD_SIZE; ++cell)
			membership.push_back(hexes.contains(BattleHex(static_cast<si16>(cell))) ? 1 : 0);
		graph.hexMembership.push_back(std::move(membership));
		return;
	}
	if(kind == typeid(ILimiter) || kind == typeid(CCreatureTypeLimiter)
		|| kind == typeid(HasAnotherBonusLimiter) || kind == typeid(TerrainLimiter)
		|| kind == typeid(CreatureLevelLimiter) || kind == typeid(FactionLimiter)
		|| kind == typeid(CreatureAlignmentLimiter) || kind == typeid(OppositeSideLimiter)
		|| kind == typeid(RankRangeLimiter)
		|| kind == typeid(UnitAdjacentLimiter) || kind == typeid(HasChargesLimiter))
		return;
	throw std::runtime_error("Unsupported snapshot limiter graph");
}

void requirePropagator(const TPropagatorPtr & propagator, Graph & graph, unsigned depth)
{
	graph.visit(depth);
	if(!propagator)
		return;
	if(typeid(*propagator) != typeid(IPropagator) && typeid(*propagator) != typeid(CPropagatorNodeType))
		throw std::runtime_error("Unsupported snapshot propagator graph");
}

void requireJson(const JsonNode & value, Graph & graph, unsigned depth)
{
	graph.visit(depth);
	if(value.isVector())
		for(const auto & nested : value.Vector())
			requireJson(nested, graph, depth + 1);
	if(value.isStruct())
		for(const auto & entry : value.Struct())
			requireJson(entry.second, graph, depth + 1);
}

const BonusParametersOnCombatEvent * combatEffects(const BonusParameters & parameters, Graph & graph, unsigned depth)
{
	try
	{
		return &parameters.toCustom<BonusParametersOnCombatEvent>();
	}
	catch(const std::runtime_error & error)
	{
		// BonusParameters is final; these are all five other current variant alternatives.
		const std::string message = error.what();
		for(const int index : {0, 1, 2, 3, 5})
			if(message == "Invalid addInfo type access! Stored type: " + std::to_string(index))
			{
				if(index == 5)
					requireJson(parameters.toCustom<JsonNode>(), graph, depth);
				if(index == 3)
					for(const auto ignored : parameters.toVector())
					{
						(void)ignored;
						graph.visit(depth);
					}
				return nullptr;
			}
		throw;
	}
}

void requireBonus(const Bonus & bonus, Graph & graph, unsigned depth)
{
	graph.visit(depth);
	if(typeid(bonus) != typeid(Bonus))
		throw std::runtime_error("Unsupported snapshot bonus type");
	graph.owners.push_back(bonus.bonusOwner);
	requireUpdater(bonus.updater, graph, depth + 1);
	requireUpdater(bonus.propagationUpdater, graph, depth + 1);
	requireLimiter(bonus.limiter, graph, depth + 1);
	requirePropagator(bonus.propagator, graph, depth + 1);
	if(!bonus.parameters)
		return;
	graph.visit(depth + 1);
	if(const auto * event = combatEffects(*bonus.parameters, graph, depth + 2))
		for(const auto & effect : event->effects)
		{
			graph.visit(depth + 2);
			if(const auto * nested = std::get_if<BonusParametersOnCombatEvent::CombatEffectBonus>(&effect))
				if(nested->bonus)
					requireBonus(*nested->bonus, graph, depth + 3);
		}
}
}

std::vector<std::byte> bonusSnapshotBytes(const Bonus & bonus)
{
	Graph graph;
	requireBonus(bonus, graph, 0);
	// Complete graph validation precedes the first write, including nested owners/updaters.
	CMemorySerializer memory;
	memory.oser & bonus;
	memory.oser & graph.owners;
	memory.oser & graph.hexMembership;
	return memory.extractBuffer();
}

std::vector<std::string> bonusSnapshots(const BonusList & bonuses)
{
	std::vector<std::string> result;
	constexpr char digits[] = "0123456789abcdef";
	for(const auto & bonus : bonuses)
	{
		if(!bonus)
			throw std::runtime_error("Null snapshot bonus entry");
		std::string encoded;
		for(const auto byte : bonusSnapshotBytes(*bonus))
		{
			const auto value = std::to_integer<unsigned int>(byte);
			encoded.push_back(digits[value >> 4]);
			encoded.push_back(digits[value & 15]);
		}
		result.push_back(std::move(encoded));
	}
	return result;
}
}
