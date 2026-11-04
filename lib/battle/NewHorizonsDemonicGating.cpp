/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsDemonicGating.h"
#include "BattleAction.h"
#include "BattleInfo.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../json/JsonNode.h"
#include "../mapObjects/CGHeroInstance.h"

namespace newHorizonsDemonicGating
{
namespace
{
constexpr const char * SKILL = "new-horizons:demonicGating";

bool perk(const CGHeroInstance & hero, const char * name)
{
	return hero.hasActivePerk(SKILL, std::string(SKILL) + "." + name);
}

const JsonNode * definition(const JsonNode & rules)
{
	if(!rules.isStruct())
		return nullptr;
	const auto it = rules.Struct().find(POLICY_KEY);
	return it == rules.Struct().end() ? nullptr : &it->second;
}
}

void validatePolicy(const JsonNode & rules)
{
	const auto * value = definition(rules);
	if(!value)
		return;
	if(!value->isStruct() || value->Struct().size() != 1
		|| !value->Struct().contains("version")
		|| (*value)["version"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| (*value)["version"].Integer() != POLICY_VERSION)
		throw std::runtime_error("Invalid captured temporary Demonic Gating policy");
}

bool usesTemporaryGating(const JsonNode & rules)
{
	validatePolicy(rules);
	return definition(rules) != nullptr;
}

bool usesTemporaryGating(const CGHeroInstance * hero)
{
	return hero && usesTemporaryGating(hero->getPerkState().rules);
}

bool usesMobileGate(const CGHeroInstance * hero)
{
	return hero && !usesTemporaryGating(hero) && perk(*hero, "mobileGate");
}

void validatePolicySerialization(const JsonNode & rules, bool supported)
{
	if(usesTemporaryGating(rules) && !supported)
		throw std::runtime_error("Cannot serialize temporary Demonic Gating to an older format");
}

bool eligibleOriginalStack(const CBattleInfoCallback & battle, const battle::Unit & source, BattleSide side)
{
	const auto * hero = battle.battleGetFightingHero(side);
	if(!usesTemporaryGating(hero) || !source.alive() || source.isGhost() || source.isClone()
		|| source.isSummoned() || !source.unitSlot().validSlot() || source.hasOpenedDemonicGate()
		|| source.unitSide() != side || battle.battleGetOwner(&source) != battle.sideToPlayer(side)
		|| source.isTimeStopped() || source.isFrozen() || source.hasBonusOfType(BonusType::NOT_ACTIVE)
		|| source.creatureId().toCreature()->getFactionID() != FactionID::INFERNO)
		return false;
	const int rank = hero->getPerkSkillRank(SKILL);
	const auto category = battle.battleGetCreatureCategory(source.creatureId());
	return rank > 0 && category && static_cast<int>(category->category) < rank;
}

std::vector<CreatureID> availableTypes(const CBattleInfoCallback & battle, const battle::Unit & source, BattleSide side)
{
	const auto * hero = battle.battleGetFightingHero(side);
	if(!hero || source.unitSide() != side || source.creatureId().toCreature()->getFactionID() != FactionID::INFERNO)
		return {};
	if(usesTemporaryGating(hero))
		return eligibleOriginalStack(battle, source, side) ? std::vector<CreatureID>{source.creatureId()} : std::vector<CreatureID>{};
	const int rank = hero->getPerkSkillRank(SKILL);
	std::vector<CreatureID> result;
	for(const auto & [creature, count] : battle.getBattle()->getDemonicReserve(side))
	{
		const auto category = battle.battleGetCreatureCategory(creature);
		if(rank > 0 && count > 0 && category && static_cast<int>(category->category) < rank)
			result.push_back(creature);
	}
	return result;
}

TQuantity summonedCount(const CGHeroInstance & hero, TQuantity count)
{
	if(count <= 0)
		return 0;
	const int percent = perk(hero, "endlessLegion") ? 50 : 30;
	return static_cast<TQuantity>(std::max<int64_t>(1, static_cast<int64_t>(count) * percent / 100));
}

int placementRange(const CGHeroInstance & hero)
{
	return perk(hero, "wideGate") ? 5 : 3;
}

bool footprintHasObstacle(const CBattleInfoCallback & battle, BattleHex position, bool doubleWide, BattleSide side)
{
	for(const auto & hex : battle::Unit::getHexes(position, doubleWide, side))
		if(hex.isAvailable() && !battle.battleGetAllObstaclesOnPos(hex, false).empty())
			return true;
	return false;
}

std::optional<GatePlan> plan(const CBattleInfoCallback & battle, const BattleAction & action)
{
	if(action.actionType != EActionType::DEMONIC_GATING || !action.gatingCreature.hasValue()
		|| (action.side != BattleSide::ATTACKER && action.side != BattleSide::DEFENDER)
		|| (action.target.size() != 1 && action.target.size() != 2)
		|| std::ranges::any_of(action.target, [](const auto & target)
		{ return target.unitValue != -1000 || !target.hexValue.isValid(); }))
		return std::nullopt;
	const auto * source = battle.battleGetUnitByID(action.stackNumber);
	const auto * hero = battle.battleGetFightingHero(action.side);
	const auto * creature = action.gatingCreature.toCreature();
	if(!source || !source->alive() || !hero || !creature || source->unitSide() != action.side
		|| source->creatureId().toCreature()->getFactionID() != FactionID::INFERNO
		|| creature->getFactionID() != FactionID::INFERNO)
		return std::nullopt;
	const bool temporary = usesTemporaryGating(hero);
	TQuantity count = 0;
	if(temporary)
	{
		if(action.gatingCreature != source->creatureId() || !eligibleOriginalStack(battle, *source, action.side))
			return std::nullopt;
		count = summonedCount(*hero, source->getCount());
	}
	else
	{
		const int rank = hero->getPerkSkillRank(SKILL);
		const auto category = battle.battleGetCreatureCategory(action.gatingCreature);
		if(rank <= 0 || !category || static_cast<int>(category->category) >= rank)
			return std::nullopt;
		const auto & reserve = battle.getBattle()->getDemonicReserve(action.side);
		const auto found = reserve.find(action.gatingCreature);
		if(found == reserve.end() || found->second <= 0)
			return std::nullopt;
		count = found->second;
	}
	auto sourcePosition = source->getPosition();
	if(action.target.size() == 2)
	{
		if(!usesMobileGate(hero) || source != battle.battleActiveUnit())
			return std::nullopt;
		const auto destination = action.target.front().hexValue;
		const auto [path, distance] = battle.getPath(sourcePosition, destination, source);
		if(path.empty() || distance < 0 || distance > static_cast<int>(source->getMovementRange(0) / 2))
			return std::nullopt;
		sourcePosition = destination;
	}
	const auto target = action.target.back().hexValue;
	const auto tail = source->doubleWide() ? source->occupiedHex(sourcePosition) : BattleHex::INVALID;
	const auto gatedTail = battle::Unit::occupiedHex(target, creature->isDoubleWide(), action.side);
	if(!target.isAvailable() || target == sourcePosition || target == tail
		|| (gatedTail.isValid() && (gatedTail == sourcePosition || gatedTail == tail))
		|| BattleHex::getDistance(sourcePosition, target) > placementRange(*hero)
		|| battle.battleGetUnitByPos(target, true)
		|| footprintHasObstacle(battle, target, creature->isDoubleWide(), action.side)
		|| !battle.getAccessibility().accessible(target, creature->isDoubleWide(), action.side))
		return std::nullopt;
	GatePlan result;
	result.creature = action.gatingCreature;
	result.count = count;
	result.position = target;
	result.sourceUnitId = action.stackNumber;
	result.arrivalRound = battle.getBattle()->getRound() + 1;
	result.temporary = temporary;
	result.atEndOfRound = perk(*hero, "swiftGate");
	result.chainGateAccelerated = perk(*hero, "chainGate") && battle.getBattle()->getChainGateArmed(action.side)
		&& !result.atEndOfRound;
	result.atEndOfRound = result.atEndOfRound || result.chainGateAccelerated;
	if(result.chainGateAccelerated)
		result.arrivalRound = battle.getBattle()->getRound();
	result.freeActivation = perk(*hero, "masterGate") && !battle.getBattle()->getMasterGateUsed(action.side);
	return result;
}
}
