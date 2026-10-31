/*
 * Limiters.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "Limiters.h"
#include "Updaters.h"

#include "../CBonusTypeHandler.h"
#include "../GameLibrary.h"
#include "../entities/faction/CTownHandler.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/army/CStackInstance.h"
#include "../mapObjects/army/CArmedInstance.h"
#include "../CStack.h"
#include <optional>
#include <set>
#include <set>
#include <stdexcept>
#include "../TerrainHandler.h"
#include "../constants/StringConstants.h"
#include "../battle/Unit.h"
#include "../json/JsonUtils.h"
#include "../modding/IdentifierStorage.h"

const std::map<std::string, TLimiterPtr> bonusLimiterMap =
{
	{"SHOOTER_ONLY", std::make_shared<HasAnotherBonusLimiter>(BonusType::SHOOTER)},
	{"DRAGON_NATURE", std::make_shared<HasAnotherBonusLimiter>(BonusType::DRAGON_NATURE)},
	{"IS_UNDEAD", std::make_shared<HasAnotherBonusLimiter>(BonusType::UNDEAD)},
	{"UNIT_DEFENDING", std::make_shared<HasAnotherBonusLimiter>(BonusType::UNIT_DEFENDING)},
	{"CREATURE_NATIVE_TERRAIN", std::make_shared<TerrainLimiter>()},
	{"CREATURES_ONLY", std::make_shared<CreatureLevelLimiter>()},
	{"OPPOSITE_SIDE", std::make_shared<OppositeSideLimiter>()},
};

ILimiter::EDecision ILimiter::limit(const BonusLimitationContext &context) const
{
	return ILimiter::EDecision::ACCEPT;
}

void ILimiter::validateAdjacencySerialization(bool) const {}

std::string ILimiter::toString() const
{
	return typeid(*this).name();
}

JsonNode ILimiter::toJsonNode() const
{
	JsonNode root;
	root["type"].String() = toString();
	return root;
}

ILimiter::EDecision CCreatureTypeLimiter::limit(const BonusLimitationContext &context) const
{
	const CCreature *c = context.evaluation.creature();
	if(!c)
		return ILimiter::EDecision::NOT_APPLICABLE;

	auto accept =  c->getId() == creatureID || (includeUpgrades && creatureID.toCreature()->isMyDirectOrIndirectUpgrade(c));
	return accept ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
	//drop bonus if it's not our creature and (we don`t check upgrades or its not our upgrade)
}

CCreatureTypeLimiter::CCreatureTypeLimiter(const CCreature & creature_, bool IncludeUpgrades)
	: creatureID(creature_.getId()), includeUpgrades(IncludeUpgrades)
{
}

void CCreatureTypeLimiter::setCreature(const CreatureID & id)
{
	creatureID = id;
}

std::string CCreatureTypeLimiter::toString() const
{
	boost::format fmt("CCreatureTypeLimiter(creature=%s, includeUpgrades=%s)");
	fmt % creatureID.toEntity(LIBRARY)->getJsonKey() % (includeUpgrades ? "true" : "false");
	return fmt.str();
}

JsonNode CCreatureTypeLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "CREATURE_TYPE_LIMITER";
	root["parameters"].Vector().emplace_back(creatureID.toEntity(LIBRARY)->getJsonKey());
	root["parameters"].Vector().emplace_back(includeUpgrades);

	return root;
}

void HasAnotherBonusLimiter::validateAdjacencySerialization(bool supported) const
{
	if(supported) return;
	if(type == BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION
		|| subtype.as<BonusTypeID>().toEnum() == BonusType::TARGET_MAGICAL_DAMAGE_REDUCTION_PENETRATION)
		throw std::runtime_error("Unsupported target MDR penetration limiter format");
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter( BonusType bonus )
	: type(bonus), isSubtypeRelevant(false), isSourceRelevant(false), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter( BonusType bonus, BonusSubtypeID _subtype )
	: type(bonus), subtype(_subtype), isSubtypeRelevant(true), isSourceRelevant(false), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter(BonusType bonus, BonusSource src)
	: type(bonus), source(src), isSubtypeRelevant(false), isSourceRelevant(true), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter(BonusType bonus, BonusSubtypeID _subtype, BonusSource src)
	: type(bonus), subtype(_subtype), isSubtypeRelevant(true), source(src), isSourceRelevant(true), isSourceIDRelevant(false)
{
}

ILimiter::EDecision HasAnotherBonusLimiter::limit(const BonusLimitationContext &context) const
{
	boost::container::static_vector<CSelector, 4> selectorSegments;

	if (type != BonusType::NONE)
		selectorSegments.push_back(Selector::type()(type));
	if(isSubtypeRelevant)
		selectorSegments.push_back(Selector::subtype()(subtype));
	if(isSourceRelevant && isSourceIDRelevant)
		selectorSegments.push_back(Selector::source(source, sid));
	else if (isSourceRelevant)
		selectorSegments.push_back(Selector::sourceTypeSel(source));

	auto mySelector = selectorSegments.empty() ? Selector::none : selectorSegments[0];

	for (size_t i = 1; i <selectorSegments.size(); ++i)
		mySelector = mySelector.And(selectorSegments[i]);

	//if we have a bonus of required type accepted, limiter should accept also this bonus
	if(context.alreadyAccepted.getFirst(mySelector))
	{
		if ( minValue == std::numeric_limits<int32_t>::min() &&
			 maxValue == std::numeric_limits<int32_t>::max())
			return ILimiter::EDecision::ACCEPT;

		// can't determine final bonus value yet
		if(context.stillUndecided.getFirst(mySelector))
			return ILimiter::EDecision::NOT_SURE;

		int bonusValue = context.alreadyAccepted.valOfBonuses(mySelector);

		if (bonusValue >= minValue && bonusValue <= maxValue)
			return ILimiter::EDecision::ACCEPT;
		else
			return ILimiter::EDecision::DISCARD;
	}

	//if there are no matching bonuses pending, we can (and must) reject right away
	if(!context.stillUndecided.getFirst(mySelector))
		return ILimiter::EDecision::DISCARD;

	//do not accept for now but it may change if more bonuses gets included
	return ILimiter::EDecision::NOT_SURE;
}

std::string HasAnotherBonusLimiter::toString() const
{
	std::string typeName = LIBRARY->bth->bonusToString(type);
	if(isSubtypeRelevant)
	{
		boost::format fmt("HasAnotherBonusLimiter(type=%s, subtype=%s)");
		fmt % typeName % subtype.toString();
		return fmt.str();
	}
	else
	{
		boost::format fmt("HasAnotherBonusLimiter(type=%s)");
		fmt % typeName;
		return fmt.str();
	}
}

JsonNode HasAnotherBonusLimiter::toJsonNode() const
{
	JsonNode root;
	std::string typeName = LIBRARY->bth->bonusToString(type);
	auto sourceTypeName = vstd::findKey(bonusSourceMap, source);

	root["type"].String() = "HAS_ANOTHER_BONUS_LIMITER";
	root["parameters"].Vector().emplace_back(typeName);
	if(isSubtypeRelevant)
		root["parameters"].Vector().emplace_back(subtype.toString());
	if(isSourceRelevant)
		root["parameters"].Vector().emplace_back(sourceTypeName);

	return root;
}

ILimiter::EDecision UnitOnHexLimiter::limit(const BonusLimitationContext &context) const
{
	const auto * stack = context.evaluation.battleUnit();
	if(!stack)
		return ILimiter::EDecision::NOT_APPLICABLE;

	auto accept = false;

	for (const auto & hex : stack->getHexes())
		accept |= applicableHexes.contains(hex);

	return accept ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
}

UnitOnHexLimiter::UnitOnHexLimiter(const BattleHexArray & applicableHexes):
	applicableHexes(applicableHexes)
{
}

JsonNode UnitOnHexLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "UNIT_ON_HEXES";
	for(const auto & hex : applicableHexes)
		root["parameters"].Vector().emplace_back(hex.toInt());

	return root;
}

ILimiter::EDecision UnitAdjacentLimiter::limit(const BonusLimitationContext &context) const
{
	const auto * stack = context.evaluation.battleUnit();
	if(!stack)
		return ILimiter::EDecision::NOT_APPLICABLE;

	if (!stack->getPosition().isValid())
		return ILimiter::EDecision::DISCARD;

	const auto adjacentUnits = context.evaluation.adjacentUnits();

	for (const auto & unit : adjacentUnits)
		if(context.evaluation.matchesAdjacentUnit(*unit, targetUnit, filter))
			return ILimiter::EDecision::ACCEPT;

	return ILimiter::EDecision::DISCARD;
}

void UnitAdjacentLimiter::validateAdjacencySerialization(bool supported) const
{
	if(!filter.isValid() || (!supported && !filter.isDefault())
		|| (filter.anyCreature && targetUnit.hasValue())
		|| (!filter.isDefault() && !filter.anyCreature && !targetUnit.hasValue()))
		throw std::runtime_error("Unsupported adjacency filter metadata");
}

JsonNode UnitAdjacentLimiter::toJsonNode() const
{
	validateAdjacencySerialization(true);
	if(!filter.anyCreature && !targetUnit.hasValue())
		throw std::runtime_error("Adjacency filter has no creature family");
	JsonNode root;
	root["type"].String() = "UNIT_ADJACENT";
	if(filter.anyCreature)
		root["anyCreature"].Bool() = true;
	else
		root["creature"].String() = targetUnit.toEntity(LIBRARY)->getJsonKey();
	if(filter.includeUpgrades)
		root["includeUpgrades"].Bool() = true;
	if(filter.relationship != AdjacentUnitFilter::Relationship::ANY)
		root["relationship"].String() = filter.relationship == AdjacentUnitFilter::Relationship::SAME_ARMY
			? "sameArmy" : "opposingArmy";
	if(filter.nativeTrait != AdjacentUnitFilter::NativeTrait::ANY)
		root["nativeTrait"].String() = filter.nativeTrait == AdjacentUnitFilter::NativeTrait::UNDEAD ? "undead" : "living";
	return root;
}
std::shared_ptr<const ILimiter> UnitAdjacentLimiter::fromJson(const JsonNode & node)
{
	if(!node.isStruct())
		throw std::runtime_error("Adjacency filter must be an object");
	if(!node["type"].isString() || node["type"].String() != "UNIT_ADJACENT")
		throw std::runtime_error("Adjacency filter requires its registered type");
	const std::set<std::string> fields{"type", "creature", "anyCreature", "includeUpgrades", "relationship", "nativeTrait"};
	for(const auto & [key, value] : node.Struct())
		if(!fields.contains(key))
			throw std::runtime_error("Unknown adjacency filter field");
	auto result = std::make_shared<UnitAdjacentLimiter>();
	for(const char * key : {"includeUpgrades", "anyCreature"})
		if(node.Struct().contains(key) && !node[key].isBool())
			throw std::runtime_error("Adjacency filter flag must be boolean");
	result->filter.includeUpgrades = node["includeUpgrades"].isBool() && node["includeUpgrades"].Bool();
	result->filter.anyCreature = node["anyCreature"].isBool() && node["anyCreature"].Bool();
	if(!result->filter.anyCreature)
	{
		if(!node["creature"].isString())
			throw std::runtime_error("Adjacency filter requires a creature family or anyCreature");
		LIBRARY->identifiers()->requestIdentifier("creature", node["creature"], [result](si32 id) { result->targetUnit = id; });
	}
	else if(node.Struct().contains("creature"))
		throw std::runtime_error("Adjacency filter cannot combine creature with anyCreature");
	if(node.Struct().contains("relationship"))
	{
		if(!node["relationship"].isString()) throw std::runtime_error("Invalid adjacency relationship");
		const auto & value = node["relationship"].String();
		if(value == "sameArmy") result->filter.relationship = AdjacentUnitFilter::Relationship::SAME_ARMY;
		else if(value == "opposingArmy") result->filter.relationship = AdjacentUnitFilter::Relationship::OPPOSING_ARMY;
		else if(value != "any") throw std::runtime_error("Invalid adjacency relationship");
	}
	if(node.Struct().contains("nativeTrait"))
	{
		if(!node["nativeTrait"].isString()) throw std::runtime_error("Invalid adjacency native trait");
		const auto & value = node["nativeTrait"].String();
		if(value == "undead") result->filter.nativeTrait = AdjacentUnitFilter::NativeTrait::UNDEAD;
		else if(value == "living") result->filter.nativeTrait = AdjacentUnitFilter::NativeTrait::LIVING;
		else if(value != "any") throw std::runtime_error("Invalid adjacency native trait");
	}
	return result;
}

FactionLimiter::FactionLimiter(FactionID creatureFaction)
	: faction(creatureFaction)
{
}

ILimiter::EDecision FactionLimiter::limit(const BonusLimitationContext &context) const
{
	const auto bearer = context.evaluation.faction();

	if(bearer)
	{
		if(faction != FactionID::DEFAULT)
			return *bearer == faction ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;

		switch(context.b.source)
		{
			case BonusSource::CREATURE_ABILITY:
				return *bearer == context.b.sid.as<CreatureID>().toCreature()->getFactionID() ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;

			case BonusSource::TOWN_STRUCTURE:
				return *bearer == context.b.sid.as<BuildingTypeUniqueID>().getFaction() ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;

			//TODO: other sources of bonuses
		}
	}
	return ILimiter::EDecision::NOT_APPLICABLE; //Discard by default
}

std::string FactionLimiter::toString() const
{
	boost::format fmt("FactionLimiter(faction=%s)");
	fmt % LIBRARY->factions()->getById(faction)->getJsonKey();
	return fmt.str();
}

JsonNode FactionLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "FACTION_LIMITER";
	root["parameters"].Vector().emplace_back(LIBRARY->factions()->getById(faction)->getJsonKey());

	return root;
}

CreatureLevelLimiter::CreatureLevelLimiter(uint32_t minLevel, uint32_t maxLevel) :
	minLevel(minLevel),
	maxLevel(maxLevel)
{
}

ILimiter::EDecision CreatureLevelLimiter::limit(const BonusLimitationContext &context) const
{
	const auto *c = context.evaluation.creature();
	if (!c)
		return ILimiter::EDecision::NOT_APPLICABLE;

	auto accept = c->getLevel() < maxLevel && c->getLevel() >= minLevel;
	return accept ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD; //drop bonus for non-creatures or non-native residents
}

std::string CreatureLevelLimiter::toString() const
{
	boost::format fmt("CreatureLevelLimiter(minLevel=%d,maxLevel=%d)");
	fmt % minLevel % maxLevel;
	return fmt.str();
}

JsonNode CreatureLevelLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "CREATURE_LEVEL_LIMITER";
	root["parameters"].Vector().emplace_back(minLevel);
	root["parameters"].Vector().emplace_back(maxLevel);

	return root;
}

CreatureAlignmentLimiter::CreatureAlignmentLimiter(EAlignment Alignment)
	: alignment(Alignment)
{
}

ILimiter::EDecision CreatureAlignmentLimiter::limit(const BonusLimitationContext &context) const
{
	const auto * c = context.evaluation.creature();
	if(c) {
		if(alignment == c->getAlignment())
			return ILimiter::EDecision::ACCEPT;
		return ILimiter::EDecision::DISCARD;
	}

	return ILimiter::EDecision::NOT_APPLICABLE;
}

std::string CreatureAlignmentLimiter::toString() const
{
	boost::format fmt("CreatureAlignmentLimiter(alignment=%s)");
	fmt % GameConstants::ALIGNMENT_NAMES[vstd::to_underlying(alignment)];
	return fmt.str();
}

JsonNode CreatureAlignmentLimiter::toJsonNode() const
{
	JsonNode root;

	root["type"].String() = "CREATURE_ALIGNMENT_LIMITER";
	root["parameters"].Vector().emplace_back(GameConstants::ALIGNMENT_NAMES[vstd::to_underlying(alignment)]);

	return root;
}

RankRangeLimiter::RankRangeLimiter(ui8 Min, ui8 Max)
	:minRank(Min), maxRank(Max)
{
}

RankRangeLimiter::RankRangeLimiter()
{
	minRank = maxRank = -1;
}

ILimiter::EDecision RankRangeLimiter::limit(const BonusLimitationContext &context) const
{
	const CStackInstance * csi = context.evaluation.stackInstance();
	if(csi)
	{
		if (csi->getNodeType() == BonusNodeType::COMMANDER) //no stack exp bonuses for commander creatures
			return ILimiter::EDecision::DISCARD;
		if (csi->getExpRank() > minRank && csi->getExpRank() < maxRank)
			return ILimiter::EDecision::ACCEPT;

		return ILimiter::EDecision::DISCARD;
	}
	return ILimiter::EDecision::NOT_APPLICABLE;
}

OppositeSideLimiter::OppositeSideLimiter()
{
}

ILimiter::EDecision OppositeSideLimiter::limit(const BonusLimitationContext & context) const
{
	PlayerColor contextOwner = context.evaluation.getOwner();
	PlayerColor bonusOwner = context.b.bonusOwner;
	if (contextOwner == PlayerColor::UNFLAGGABLE)
		contextOwner = PlayerColor::NEUTRAL;
	auto decision = (bonusOwner == contextOwner || bonusOwner == PlayerColor::CANNOT_DETERMINE) ? ILimiter::EDecision::DISCARD : ILimiter::EDecision::ACCEPT;
	return decision;
}

// Aggregate/Boolean Limiters

AggregateLimiter::AggregateLimiter(std::vector<TLimiterPtr> limiters):
	limiters(std::move(limiters))
{
}

void AggregateLimiter::validateAdjacencySerialization(bool supported) const
{
	for(const auto & limiter : limiters)
		if(limiter)
			limiter->validateAdjacencySerialization(supported);
}

void AggregateLimiter::add(const TLimiterPtr & limiter)
{
	if(limiter)
		limiters.push_back(limiter);
}

JsonNode AggregateLimiter::toJsonNode() const
{
	JsonNode result;
	result.Vector().emplace_back(getAggregator());
	for(const auto & l : limiters)
		result.Vector().push_back(l->toJsonNode());
	return result;
}

const std::string AllOfLimiter::aggregator = "allOf";
const std::string & AllOfLimiter::getAggregator() const
{
	return aggregator;
}

AllOfLimiter::AllOfLimiter(std::vector<TLimiterPtr> limiters):
	AggregateLimiter(limiters)
{
}

ILimiter::EDecision AllOfLimiter::limit(const BonusLimitationContext & context) const
{
	bool wasntSure = false;

	for(const auto & limiter : limiters)
	{
		auto result = limiter->limit(context);
		if(result == ILimiter::EDecision::DISCARD || result == ILimiter::EDecision::NOT_APPLICABLE)
			return result;
		if(result == ILimiter::EDecision::NOT_SURE)
			wasntSure = true;
	}

	return wasntSure ? ILimiter::EDecision::NOT_SURE : ILimiter::EDecision::ACCEPT;
}

const std::string AnyOfLimiter::aggregator = "anyOf";
const std::string & AnyOfLimiter::getAggregator() const
{
	return aggregator;
}

AnyOfLimiter::AnyOfLimiter(std::vector<TLimiterPtr> limiters):
	AggregateLimiter(limiters)
{
}

ILimiter::EDecision AnyOfLimiter::limit(const BonusLimitationContext & context) const
{
	bool wasntSure = false;

	for(const auto & limiter : limiters)
	{
		auto result = limiter->limit(context);
		if(result == ILimiter::EDecision::ACCEPT)
			return result;
		if(result == ILimiter::EDecision::NOT_SURE)
			wasntSure = true;
	}

	return wasntSure ? ILimiter::EDecision::NOT_SURE : ILimiter::EDecision::DISCARD;
}

const std::string NoneOfLimiter::aggregator = "noneOf";
const std::string & NoneOfLimiter::getAggregator() const
{
	return aggregator;
}

NoneOfLimiter::NoneOfLimiter(std::vector<TLimiterPtr> limiters):
	AggregateLimiter(limiters)
{
}

ILimiter::EDecision NoneOfLimiter::limit(const BonusLimitationContext & context) const
{
	bool wasntSure = false;

	for(const auto & limiter : limiters)
	{
		auto result = limiter->limit(context);
		if(result == ILimiter::EDecision::NOT_APPLICABLE)
			return ILimiter::EDecision::NOT_APPLICABLE;
		if(result == ILimiter::EDecision::ACCEPT)
			return ILimiter::EDecision::DISCARD;
		if(result == ILimiter::EDecision::NOT_SURE)
			wasntSure = true;
	}

	return wasntSure ? ILimiter::EDecision::NOT_SURE : ILimiter::EDecision::ACCEPT;
}

HasChargesLimiter::HasChargesLimiter(const uint16_t cost)
	: chargeCost(cost)
{
}

ILimiter::EDecision HasChargesLimiter::limit(const BonusLimitationContext & context) const
{
	for(const auto & bonus : context.stillUndecided)
	{
		if(bonus->type == BonusType::ARTIFACT_CHARGE && bonus->sid == context.b.sid)
			return ILimiter::EDecision::NOT_SURE;
	}

	for(const auto & bonus : context.alreadyAccepted)
	{
		if(bonus->type == BonusType::ARTIFACT_CHARGE && bonus->sid == context.b.sid)
			return bonus->val >= chargeCost ? ILimiter::EDecision::ACCEPT : ILimiter::EDecision::DISCARD;
	}
	return ILimiter::EDecision::DISCARD;
}
