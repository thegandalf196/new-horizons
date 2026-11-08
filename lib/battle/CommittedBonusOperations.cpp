/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CommittedBonusOperations.h"
#include "BattleInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsPassiveSpecialties.h"
#include "Unit.h"
#include "../CStack.h"
#include "../bonuses/BonusEvaluationContext.h"
#include "../bonuses/BonusGraphQuery.h"
#include "../bonuses/BonusLimitation.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/NativeBonusGraphAccess.h"
#include "../bonuses/OwnedBonusPayloads.h"
#include "../bonuses/ProjectedBonusRoster.h"
#include "../bonuses/Updaters.h"
#include "../bonuses/Limiters.h"
#include "../bonuses/Propagators.h"
#include <typeinfo>
#include <map>
#include <set>
#include <stdexcept>
namespace battle
{
namespace
{
void validateTreantmootOperation(const Bonus & bonus)
{
	const auto reject = []() { throw std::invalid_argument("Unsupported current Treantmoot operation shape"); };
	if(bonus.val != 1 || bonus.updater || bonus.propagator || bonus.propagationUpdater
		|| bonus.effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus.sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:ryland")))
		|| bonus.stacking != "new-horizons:ordinary-passive:core:ryland:targetProtection"
		|| typeid(*bonus.limiter) != typeid(AllOfLimiter)) reject();
	const auto & all = static_cast<const AllOfLimiter &>(*bonus.limiter);
	if(all.limiters.size() != 2 || !all.limiters[0] || !all.limiters[1]
		|| typeid(*all.limiters[0]) != typeid(NoneOfLimiter)
		|| typeid(*all.limiters[1]) != typeid(UnitAdjacentLimiter)) reject();
	const auto & excluded = static_cast<const NoneOfLimiter &>(*all.limiters[0]);
	if(excluded.limiters.size() != 1 || !excluded.limiters[0]
		|| typeid(*excluded.limiters[0]) != typeid(CCreatureTypeLimiter)) reject();
	const auto & family = static_cast<const CCreatureTypeLimiter &>(*excluded.limiters[0]);
	const auto & spatial = static_cast<const UnitAdjacentLimiter &>(*all.limiters[1]);
	const CreatureID tree(CreatureID::decode("core:dendroidGuard"));
	if(family.creatureID != tree || !family.includeUpgrades || spatial.targetUnit != tree
		|| !spatial.filter.includeUpgrades || spatial.filter.anyCreature
		|| spatial.filter.nativeTrait != AdjacentUnitFilter::NativeTrait::ANY
		|| spatial.filter.relationship != AdjacentUnitFilter::Relationship::SAME_ARMY) reject();
}
void validateOperation(const Bonus & bonus)
{
	const auto reject = []() { throw std::invalid_argument("Unsupported current passive operation shape"); };
	if(bonus.duration != BonusDuration::PERMANENT || bonus.valType != BonusValueType::ADDITIVE_VALUE
		|| !bonus.limiter || bonus.parameters)
		reject();
	if(bonus.type == BonusType::CREATURE_ATTACK_TARGET_PROTECTION)
	{
		validateTreantmootOperation(bonus);
		return;
	}
	if(bonus.type == BonusType::PHYSICAL_DEFENSE)
	{
		if(typeid(*bonus.limiter) != typeid(CCreatureTypeLimiter) || bonus.propagator || bonus.propagationUpdater
			|| bonus.effectRange != BonusLimitEffect::NO_LIMIT)
			reject();
		const auto & family = static_cast<const CCreatureTypeLimiter &>(*bonus.limiter);
		if(family.creatureID != CreatureID(CreatureID::decode("core:skeleton")) || !family.includeUpgrades)
			reject();
		if(bonus.updater ? typeid(*bonus.updater) != typeid(TimesStackSizeUpdater) : bonus.val != -11)
			reject();
		return;
	}
	if(bonus.updater || typeid(*bonus.limiter) != typeid(AllOfLimiter)) reject();
	const auto & all = static_cast<const AllOfLimiter &>(*bonus.limiter);
	if(all.limiters.size() != 2) reject();
	bool adjacent = false, opposing = false;
	for(const auto & limiter : all.limiters)
	{
		if(!limiter) reject();
		if(typeid(*limiter) == typeid(UnitAdjacentLimiter))
		{
			const auto & spatial = static_cast<const UnitAdjacentLimiter &>(*limiter);
			if(adjacent || spatial.targetUnit != CreatureID(CreatureID::decode("core:wight"))
				|| !spatial.filter.includeUpgrades || spatial.filter.anyCreature
				|| spatial.filter.nativeTrait != AdjacentUnitFilter::NativeTrait::ANY
				|| spatial.filter.relationship != AdjacentUnitFilter::Relationship::OPPOSING_ARMY) reject();
			adjacent = true;
		}
		else if(typeid(*limiter) == typeid(OppositeSideLimiter) && !opposing) opposing = true;
		else reject();
	}
	if(!adjacent || !opposing || (bonus.propagator && (typeid(*bonus.propagator) != typeid(CPropagatorNodeType)
		|| bonus.propagator->getPropagatorType() != BonusNodeType::BATTLE_WIDE))
		|| (bonus.propagationUpdater && typeid(*bonus.propagationUpdater) != typeid(OwnerUpdater))) reject();
	if(bonus.type == BonusType::GENERAL_ATTACK_REDUCTION)
	{
		if(bonus.val != 10 || bonus.effectRange != BonusLimitEffect::ONLY_MELEE_FIGHT) reject();
	}
	else if((bonus.type != BonusType::MORALE && bonus.type != BonusType::LUCK)
		|| bonus.val != -1 || bonus.effectRange != BonusLimitEffect::NO_LIMIT) reject();
}
void collectOperations(const CBonusSystemNode & node,
	std::vector<std::shared_ptr<const Bonus>> & out, std::set<const CBonusSystemNode *> & path)
{
	if(!path.insert(&node).second) throw std::invalid_argument("Cyclic current bonus source graph");
	for(const auto * parent : liveBonusGraph().parents(node)) collectOperations(*parent, out, path);
	for(const auto & bonus : liveBonusGraph().local(node))
		if(bonus && newHorizonsPassiveSpecialties::isDynamicPassiveBonus(*bonus))
		{
			validateOperation(*bonus);
			out.push_back(bonus);
		}
	path.erase(&node);
}
}
struct CommittedBonusOperations::Data
{
	struct Source
	{
		const CStack * identity; // Identity only; caller retains the native source interval.
		const IBonusBearer * provider;
		std::unique_ptr<ownedBonusPayloads::Captured> operations;
	};
	std::map<uint32_t, Source> sources;
	std::map<std::string, std::string> anonymousGroups;
};
CommittedBonusOperations::CommittedBonusOperations(const CBattleInfoCallback & observer) : data(std::make_unique<Data>())
{
	const auto * native = dynamic_cast<const BattleInfo *>(observer.getBattle());
	if(!native) throw std::invalid_argument("Current bonus operations require the native source interval");
	NativeBonusGraphAccess access;
	for(const auto * unit : observer.battleGetUnitsIf([](const Unit *) { return true; }))
	{
		const auto * stack = dynamic_cast<const CStack *>(unit);
		if(!stack || stack->getBattle() != native || native->battleGetStackByID(unit->unitId(), false) != stack)
			throw std::invalid_argument("Current bonus operations require exact registered native occurrences");
		std::vector<std::shared_ptr<const Bonus>> operations;
		std::set<const CBonusSystemNode *> path;
		collectOperations(*stack, operations, path);
		for(const auto & bonus : operations)
			if(!bonus->stacking.empty() && bonus->stacking != "ALWAYS" && !data->anonymousGroups.contains(bonus->stacking))
				data->anonymousGroups.emplace(bonus->stacking,
					"new-horizons:current-bonus-group:" + std::to_string(data->anonymousGroups.size()));
		Data::Source source{stack, stack->getBonusBearer(), ownedBonusPayloads::copyOwnedValues(operations)};
		if(!data->sources.emplace(stack->unitId(), std::move(source)).second)
			throw std::invalid_argument("Ambiguous current bonus operation occurrence");
	}
}
CommittedBonusOperations::~CommittedBonusOperations() = default;
bool CommittedBonusOperations::empty() const
{
	return std::ranges::all_of(data->sources, [](const auto & entry)
		{ return entry.second.operations->roots().empty(); });
}
bool CommittedBonusOperations::containsSource(const CStack & native) const
{
	const auto found = data->sources.find(native.unitId());
	return found != data->sources.end() && found->second.identity == &native
		&& found->second.provider == native.getBonusBearer();
}
TConstBonusListPtr CommittedBonusOperations::replaceCurrent(const CStack & native, const Unit & projected,
	const ProjectedBonusRoster & roster, const BonusList & baseline) const
{
	const auto found = data->sources.find(projected.unitId());
	if(found == data->sources.end() || found->second.identity != &native
		|| found->second.provider != native.getBonusBearer() || native.unitId() != projected.unitId()
		|| projected.unitSide() != native.unitSide() || projected.unitOwner() != native.unitOwner()
		|| roster.usesOrderedOccurrences() || !roster.contains(projected) || !native.getBattle())
		throw std::invalid_argument("Current bonus query is outside its admitted native occurrence/roster");
	const BonusEvaluationContext evaluation(native, projected, roster, {nullptr, native.getBattle()});
	BonusList pending, accepted;
	for(const auto & operation : found->second.operations->roots())
	{
		// Admitted count updaters REPLACE val from the recipient count, so this
		// equals canonical collectBonusGraph's final recipient update, even when
		// the native graph encountered the same updater at earlier ancestors.
		if(operation->updater && !dynamic_cast<const TimesStackSizeUpdater *>(operation->updater.get()))
			throw std::invalid_argument("Unsupported current passive updater");
		pending.push_back(operation->updater ? operation->updater->createUpdatedBonus(operation, evaluation) : operation);
	}
	limitBonuses(pending, accepted, evaluation);
	auto result = std::make_shared<BonusList>();
	for(const auto & bonus : baseline)
		if(!newHorizonsPassiveSpecialties::isDynamicPassiveBonus(*bonus)) result->push_back(bonus);
	for(const auto & bonus : accepted)
	{
		auto numeric = std::make_shared<Bonus>(BonusDuration::PERMANENT, bonus->type,
			bonus->source, bonus->val, BonusSourceID(), bonus->subtype, bonus->valType);
		numeric->targetSourceType = bonus->targetSourceType;
		numeric->bonusOwner = bonus->bonusOwner;
		numeric->effectRange = bonus->effectRange;
		if(bonus->stacking == "ALWAYS") numeric->stacking = "ALWAYS";
		else if(!bonus->stacking.empty()) numeric->stacking = data->anonymousGroups.at(bonus->stacking);
		result->push_back(numeric);
	}
	return result; // Caller preserves normal stacked/unstacked selector order.
}
}
