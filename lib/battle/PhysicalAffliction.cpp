/*
 * PhysicalAffliction.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "PhysicalAffliction.h"

#include "CUnitState.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"

#include <algorithm>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace
{
struct GroupKey
{
	BonusSource source = BonusSource::OTHER;
	BonusSourceID sourceID;

	bool operator<(const GroupKey & other) const
	{
		if(source != other.source)
			return source < other.source;
		return sourceID < other.sourceID;
	}

	bool operator==(const GroupKey & other) const
	{
		return source == other.source && sourceID == other.sourceID;
	}
};

GroupKey keyFor(const Bonus & bonus)
{
	return {bonus.source, bonus.sid};
}

std::vector<Bonus> unitBonuses(const battle::Unit & unit)
{
	std::vector<Bonus> result;
	const auto bonuses = unit.getBonuses(Selector::all);
	if(!bonuses)
		return result;

	result.reserve(bonuses->size());
	for(const auto & bonus : *bonuses)
		if(bonus)
			result.emplace_back(*bonus);
	return result;
}

void setApplicationOrder(Bonus & bonus, const int64_t applicationOrder)
{
	if(applicationOrder < 0)
		throw std::invalid_argument("Physical-affliction application order cannot be negative");

	JsonNode parameters;
	try
	{
		parameters = bonus.parameters->toCustom<JsonNode>();
	}
	catch(const std::exception &)
	{
		throw std::invalid_argument("PHYSICAL_AFFLICTION marker parameters must be a JSON object");
	}
	parameters["applicationOrder"] = JsonNode(applicationOrder);
	bonus.parameters = std::make_shared<BonusParameters>(parameters);
}

void validateMarkerGroup(const GroupKey & key, const std::vector<Bonus> & bonuses,
	physicalAfflictions::Affliction & result)
{
	std::optional<physicalAfflictions::MarkerMetadata> firstMarker;
	std::optional<BonusDuration::Type> markerDuration;

	for(const auto & bonus : bonuses)
	{
		const auto metadata = physicalAfflictions::markerMetadata(bonus);
		if(!metadata)
		{
			result.effects.push_back(bonus);
			continue;
		}

		if(!firstMarker)
		{
			firstMarker = metadata;
			markerDuration = bonus.duration;
			continue;
		}

		if(metadata->kind != firstMarker->kind)
			throw std::invalid_argument("Physical-affliction group has conflicting kinds");
		if(metadata->applicationOrder != firstMarker->applicationOrder)
			throw std::invalid_argument("Physical-affliction group has conflicting application orders");
		if(bonus.duration != *markerDuration)
			throw std::invalid_argument("Physical-affliction group has markers with conflicting durations");
	}

	if(firstMarker)
	{
		result.kind = firstMarker->kind;
		result.source = key.source;
		result.sourceID = key.sourceID;
		result.applicationOrder = firstMarker->applicationOrder;
		// Frozen is represented by its marker alone. Creature-source IDs can
		// coincide with the bearer's intrinsic native bonuses; those are not
		// part of this physical affliction and must never be cleansed with it.
		if(result.kind == "frozen")
			result.effects.clear();
		return;
	}

	if(key.source != BonusSource::SPELL_EFFECT)
		return;

	const SpellID spell = key.sourceID.as<SpellID>();
	if(spell == SpellID::POISON)
		result.kind = "poison";
	else if(spell == SpellID::DISEASE)
		result.kind = "disease";
	else
		return;

	result.source = key.source;
	result.sourceID = key.sourceID;
	result.applicationOrder = 0;
}

int priorityFor(const std::string & kind)
{
	if(kind == "poison")
		return 0;
	if(kind == "disease")
		return 1;
	if(kind == "bleeding")
		return 2;
	return 3;
}

bool afflictionLess(const physicalAfflictions::Affliction & lhs, const physicalAfflictions::Affliction & rhs)
{
	const int lhsPriority = priorityFor(lhs.kind);
	const int rhsPriority = priorityFor(rhs.kind);
	if(lhsPriority != rhsPriority)
		return lhsPriority < rhsPriority;
	if(lhs.applicationOrder != rhs.applicationOrder)
		return lhs.applicationOrder < rhs.applicationOrder;
	if(lhs.source != rhs.source)
		return lhs.source < rhs.source;
	if(lhs.sourceID != rhs.sourceID)
		return lhs.sourceID < rhs.sourceID;
	if(lhs.kind != rhs.kind)
		return lhs.kind < rhs.kind;
	return lhs.storedPoison < rhs.storedPoison;
}

bool sameRemovalIdentity(const Bonus & lhs, const Bonus & rhs)
{
	// Keep this aligned with BattleInfo::removeUnitBonus and the detached
	// StackWithBonuses removal selector. Timers and metadata are not identity.
	return lhs.duration == rhs.duration && lhs.type == rhs.type && lhs.subtype == rhs.subtype
		&& lhs.source == rhs.source && lhs.val == rhs.val && lhs.sid == rhs.sid
		&& lhs.valType == rhs.valType && lhs.effectRange == rhs.effectRange;
}

bool isScheduledForRemoval(const Bonus & bonus, const std::vector<Bonus> & removed)
{
	return std::ranges::any_of(removed, [&](const Bonus & candidate)
	{
		return candidate.type != BonusType::PHYSICAL_AFFLICTION && sameRemovalIdentity(bonus, candidate);
	});
}

bool isMarkerScheduledForRemoval(const Bonus & marker, const std::vector<Bonus> & removed)
{
	return std::ranges::any_of(removed, [&](const Bonus & candidate)
	{
		return candidate.type == BonusType::PHYSICAL_AFFLICTION && sameRemovalIdentity(marker, candidate);
	});
}

std::optional<std::string> legacyFallbackKind(const GroupKey & key)
{
	if(key.source != BonusSource::SPELL_EFFECT)
		return std::nullopt;
	const SpellID spell = key.sourceID.as<SpellID>();
	if(spell == SpellID::POISON)
		return "poison";
	if(spell == SpellID::DISEASE)
		return "disease";
	return std::nullopt;
}

struct IncomingGroup
{
	GroupKey key;
	size_t markerIndex = 0;
	std::string kind;
	std::optional<int64_t> explicitOrder;
};

struct PreparedVector
{
	std::vector<Bonus> bonuses;
	std::vector<IncomingGroup> groups;
};

PreparedVector prepareIncoming(std::vector<Bonus> bonuses)
{
	PreparedVector result;
	result.bonuses = std::move(bonuses);
	std::map<GroupKey, size_t> groupIndices;
	std::vector<bool> duplicateMarkers(result.bonuses.size(), false);

	for(size_t index = 0; index < result.bonuses.size(); ++index)
	{
		Bonus & bonus = result.bonuses[index];
		const auto metadata = physicalAfflictions::markerMetadata(bonus);
		if(!metadata)
			continue;

		const GroupKey key = keyFor(bonus);
		auto existing = groupIndices.find(key);
		if(existing == groupIndices.end())
		{
			IncomingGroup group;
			group.key = key;
			group.markerIndex = index;
			group.kind = metadata->kind;
			if(metadata->applicationOrder > 0)
				group.explicitOrder = metadata->applicationOrder;
			groupIndices.emplace(key, result.groups.size());
			result.groups.push_back(std::move(group));
			continue;
		}

		IncomingGroup & group = result.groups.at(existing->second);
		if(group.kind != metadata->kind)
			throw std::invalid_argument("Incoming physical-affliction group has conflicting kinds");
		if(metadata->applicationOrder > 0)
		{
			if(group.explicitOrder && *group.explicitOrder != metadata->applicationOrder)
				throw std::invalid_argument("Incoming physical-affliction group has conflicting application orders");
			group.explicitOrder = metadata->applicationOrder;
		}

		Bonus & retainedMarker = result.bonuses.at(group.markerIndex);
		if(retainedMarker.duration != bonus.duration)
			throw std::invalid_argument("Incoming physical-affliction group has markers with conflicting durations");
		if(Bonus::NTurns(&retainedMarker) && Bonus::NTurns(&bonus))
			retainedMarker.turnsRemain = std::max(retainedMarker.turnsRemain, bonus.turnsRemain);
		duplicateMarkers[index] = true;
	}

	for(const auto & group : result.groups)
	{
		const Bonus & marker = result.bonuses.at(group.markerIndex);
		const auto metadata = physicalAfflictions::markerMetadata(marker);
		if(!metadata)
			throw std::logic_error("Prepared physical-affliction marker disappeared");
		if(group.explicitOrder)
			setApplicationOrder(result.bonuses.at(group.markerIndex), *group.explicitOrder);
	}

	std::vector<Bonus> deduplicated;
	deduplicated.reserve(result.bonuses.size());
	for(size_t index = 0; index < result.bonuses.size(); ++index)
		if(!duplicateMarkers[index])
			deduplicated.emplace_back(std::move(result.bonuses[index]));
	result.bonuses = std::move(deduplicated);
	for(size_t index = 0; index < result.bonuses.size(); ++index)
	{
		if(result.bonuses[index].type != BonusType::PHYSICAL_AFFLICTION)
			continue;
		const auto groupIndex = groupIndices.find(keyFor(result.bonuses[index]));
		if(groupIndex != groupIndices.end())
			result.groups.at(groupIndex->second).markerIndex = index;
	}
	return result;
}

void validateCurrentMarkers(const std::vector<Bonus> & bonuses)
{
	std::map<GroupKey, std::vector<Bonus>> groups;
	for(const auto & bonus : bonuses)
		if(bonus.type == BonusType::PHYSICAL_AFFLICTION)
			groups[keyFor(bonus)].push_back(bonus);

	for(const auto & [key, groupBonuses] : groups)
	{
		physicalAfflictions::Affliction ignored;
		validateMarkerGroup(key, groupBonuses, ignored);
	}
}
}

namespace physicalAfflictions
{
std::optional<MarkerMetadata> markerMetadata(const Bonus & bonus)
{
	if(bonus.type != BonusType::PHYSICAL_AFFLICTION)
		return std::nullopt;
	if(bonus.val != 0)
		throw std::invalid_argument("PHYSICAL_AFFLICTION marker value must be zero");
	if(!bonus.parameters)
		throw std::invalid_argument("PHYSICAL_AFFLICTION marker requires JSON parameters");

	const JsonNode * parameters = nullptr;
	try
	{
		parameters = &bonus.parameters->toCustom<JsonNode>();
	}
	catch(const std::exception &)
	{
		throw std::invalid_argument("PHYSICAL_AFFLICTION marker parameters must be a JSON object");
	}
	if(!parameters->isStruct())
		throw std::invalid_argument("PHYSICAL_AFFLICTION marker parameters must be a JSON object");

	const auto & values = parameters->Struct();
	for(const auto & [key, value] : values)
	{
		(void)value;
		if(key != "kind" && key != "applicationOrder" && key != "applicationRound")
			throw std::invalid_argument("PHYSICAL_AFFLICTION marker contains an unknown parameter");
	}
	const auto kind = values.find("kind");
	if(kind == values.end() || !kind->second.isString())
		throw std::invalid_argument("PHYSICAL_AFFLICTION marker requires a string kind");
	const std::string & kindValue = kind->second.String();
	if(kindValue.empty())
		throw std::invalid_argument("PHYSICAL_AFFLICTION marker kind cannot be empty");
	if(const auto round = values.find("applicationRound"); round != values.end())
	{
		if(kindValue != "frozen" || round->second.getType() != JsonNode::JsonType::DATA_INTEGER
			|| round->second.Integer() < 0 || round->second.Integer() > std::numeric_limits<int32_t>::max())
			throw std::invalid_argument("Invalid Frozen physical-affliction application round");
	}

	int64_t applicationOrder = 0;
	const auto order = values.find("applicationOrder");
	if(order != values.end())
	{
		if(order->second.getType() != JsonNode::JsonType::DATA_INTEGER)
			throw std::invalid_argument("PHYSICAL_AFFLICTION applicationOrder must be an integer");
		applicationOrder = order->second.Integer();
		if(applicationOrder < 0)
			throw std::invalid_argument("PHYSICAL_AFFLICTION applicationOrder cannot be negative");
	}

	return MarkerMetadata{kindValue, applicationOrder};
}

std::vector<Affliction> enumerate(const battle::Unit & unit)
{
	std::map<GroupKey, std::vector<Bonus>> groups;
	for(const auto & bonus : unitBonuses(unit))
		groups[keyFor(bonus)].push_back(bonus);

	std::vector<Affliction> result;
	for(const auto & [key, groupBonuses] : groups)
	{
		Affliction affliction;
		affliction.source = key.source;
		affliction.sourceID = key.sourceID;
		validateMarkerGroup(key, groupBonuses, affliction);
		if(!affliction.kind.empty())
			result.push_back(std::move(affliction));
	}

	const auto state = unit.acquireState();
	if(state && state->physicalPoisonBaseDamage > 0 && state->physicalPoisonActivationsRemaining > 0)
	{
		Affliction poison;
		poison.kind = "poison";
		poison.applicationOrder = 0;
		poison.storedPoison = true;
		result.push_back(std::move(poison));
	}
	return result;
}

std::optional<Affliction> first(const battle::Unit & unit)
{
	auto afflictions = enumerate(unit);
	if(afflictions.empty())
		return std::nullopt;

	const auto selected = std::min_element(afflictions.begin(), afflictions.end(), afflictionLess);
	return *selected;
}

std::vector<Bonus> removalPlan(const battle::Unit & unit, const Affliction & affliction)
{
	if(affliction.storedPoison)
		return {};

	const auto current = enumerate(unit);
	const auto exists = std::ranges::any_of(current, [&](const Affliction & candidate)
	{
		return !candidate.storedPoison && candidate.source == affliction.source
			&& candidate.sourceID == affliction.sourceID && candidate.kind == affliction.kind
			&& candidate.applicationOrder == affliction.applicationOrder;
	});
	if(!exists)
		return {};

	std::vector<Bonus> result;
	for(const auto & bonus : unitBonuses(unit))
		if(bonus.source == affliction.source && bonus.sid == affliction.sourceID)
		{
			if(affliction.kind == "frozen")
			{
				const auto metadata = markerMetadata(bonus);
				if(!metadata || metadata->kind != "frozen")
					continue;
			}
			result.push_back(bonus);
		}
	return result;
}

void stampEffectChanges(const battle::Unit & unit, const std::vector<Bonus> & removed,
	const std::vector<std::vector<Bonus> *> & incomingInApplyOrder)
{
	std::set<const std::vector<Bonus> *> distinctVectors;
	bool hasMarkerWork = false;
	for(const auto & bonus : removed)
	{
		if(bonus.type == BonusType::PHYSICAL_AFFLICTION)
		{
			markerMetadata(bonus);
			hasMarkerWork = true;
		}
	}
	for(const auto * incoming : incomingInApplyOrder)
	{
		if(!incoming)
			throw std::invalid_argument("Physical-affliction batch contains a null bonus vector");
		if(!distinctVectors.insert(incoming).second)
			throw std::invalid_argument("Physical-affliction batch repeats an incoming bonus vector");
		for(const auto & bonus : *incoming)
			if(bonus.type == BonusType::PHYSICAL_AFFLICTION)
				hasMarkerWork = true;
	}
	if(!hasMarkerWork)
		return;

	const auto currentBonuses = unitBonuses(unit);
	const auto currentAfflictions = enumerate(unit);
	validateCurrentMarkers(currentBonuses);

	std::map<GroupKey, Affliction> active;
	std::set<GroupKey> explicitlyMarked;
	for(const auto & bonus : currentBonuses)
		if(bonus.type == BonusType::PHYSICAL_AFFLICTION)
			explicitlyMarked.insert(keyFor(bonus));
	for(const auto & affliction : currentAfflictions)
		if(!affliction.storedPoison)
			active.emplace(GroupKey{affliction.source, affliction.sourceID}, affliction);

	for(const auto & bonus : removed)
	{
		if(bonus.type != BonusType::PHYSICAL_AFFLICTION)
			continue;
		const auto metadata = markerMetadata(bonus);
		const GroupKey key = keyFor(bonus);
		const auto existing = active.find(key);
		if(existing != active.end() && metadata && metadata->kind != existing->second.kind)
			throw std::invalid_argument("Removed physical-affliction marker conflicts with the active group kind");
	}
	std::set<GroupKey> remainingMarkerGroups;
	for(const auto & bonus : currentBonuses)
		if(bonus.type == BonusType::PHYSICAL_AFFLICTION && !isMarkerScheduledForRemoval(bonus, removed))
			remainingMarkerGroups.insert(keyFor(bonus));

	for(auto iterator = active.begin(); iterator != active.end(); )
	{
		const GroupKey key = iterator->first;
		Affliction & affliction = iterator->second;
		const bool hasMarker = explicitlyMarked.contains(key);
		const bool markerRemoved = hasMarker && !remainingMarkerGroups.contains(key);
		if(hasMarker && !markerRemoved)
		{
			std::erase_if(affliction.effects, [&](const Bonus & effect)
			{
				return isScheduledForRemoval(effect, removed);
			});
			++iterator;
			continue;
		}

		std::erase_if(affliction.effects, [&](const Bonus & effect)
		{
			return isScheduledForRemoval(effect, removed);
		});
		if(hasMarker && markerRemoved)
		{
			const auto fallbackKind = legacyFallbackKind(key);
			if(fallbackKind && !affliction.effects.empty())
			{
				affliction.kind = *fallbackKind;
				affliction.applicationOrder = 0;
				++iterator;
				continue;
			}
			iterator = active.erase(iterator);
			continue;
		}
		if(affliction.effects.empty())
			iterator = active.erase(iterator);
		else
			++iterator;
	}

	// Order zero is the serialized/default sentinel. New applications start at one.
	int64_t maximumOrder = 0;
	for(const auto & [key, affliction] : active)
		maximumOrder = std::max(maximumOrder, affliction.applicationOrder);
	for(const auto & affliction : currentAfflictions)
		if(affliction.storedPoison)
			maximumOrder = std::max(maximumOrder, affliction.applicationOrder);

	std::vector<PreparedVector> planned;
	planned.reserve(incomingInApplyOrder.size());
	for(const auto * incoming : incomingInApplyOrder)
		planned.push_back(prepareIncoming(*incoming));

	for(auto & incoming : planned)
	{
		for(auto & group : incoming.groups)
		{
			auto existing = active.find(group.key);
			int64_t assignedOrder = 0;
			if(existing != active.end())
			{
				if(existing->second.kind != group.kind)
					throw std::invalid_argument("Physical-affliction source/sid group cannot change kind");
				assignedOrder = existing->second.applicationOrder;
			}
			else if(group.explicitOrder)
			{
				assignedOrder = *group.explicitOrder;
			}
			else
			{
				if(maximumOrder == std::numeric_limits<int64_t>::max())
					throw std::overflow_error("Physical-affliction application order is exhausted");
				assignedOrder = maximumOrder + 1;
			}

			setApplicationOrder(incoming.bonuses.at(group.markerIndex), assignedOrder);
			Affliction activeGroup;
			activeGroup.kind = group.kind;
			activeGroup.source = group.key.source;
			activeGroup.sourceID = group.key.sourceID;
			activeGroup.applicationOrder = assignedOrder;
			active[group.key] = std::move(activeGroup);
			maximumOrder = std::max(maximumOrder, assignedOrder);
		}
	}

	// Marker metadata is validated before any caller-owned packet vector is changed.
	for(const auto & incoming : planned)
		for(const auto & bonus : incoming.bonuses)
			markerMetadata(bonus);

	for(size_t index = 0; index < planned.size(); ++index)
		*incomingInApplyOrder[index] = std::move(planned[index].bonuses);
}

std::vector<Bonus> stampApplicationOrder(const battle::Unit & unit,
	const std::vector<Bonus> & incomingBonuses)
{
	std::vector<Bonus> result = incomingBonuses;
	stampEffectChanges(unit, {}, {&result});
	return result;
}
}
