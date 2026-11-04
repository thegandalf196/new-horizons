/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "FixedActionValue.h"
#include "PotentialTargets.h"
#include "PhysicalPoisonValue.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include <cmath>
#include <set>
namespace fixedActionValue
{
namespace
{
float moraleExposure(const CBattleInfoCallback & state, const battle::Unit & unit)
{
	if(unit.unaffectedByMorale()) return 1;
	const int morale = state.battleGetMorale(&unit);
	if(morale == 0) return 1;
	const auto * settings = LIBRARY->engineSettings();
	const auto & chances = settings->getVector(morale > 0
		? EGameSettings::COMBAT_GOOD_MORALE_CHANCE : EGameSettings::COMBAT_BAD_MORALE_CHANCE);
	const int dice = settings->getInteger(EGameSettings::COMBAT_MORALE_DICE_SIZE);
	if(chances.empty() || dice <= 0) return 1;
	const auto index = std::min<size_t>(std::abs(morale), chances.size()) - 1;
	return 1 + (morale > 0 ? 1.0f : -1.0f) * std::max<int64_t>(0, chances[index]) / dice;
}
}
Reference::Reference(std::shared_ptr<HypotheticBattle> state, PlayerColor player,
	const DamageCache & damage, float positiveEffectMultiplier, float negativeEffectMultiplier)
	: parent(std::move(state)), player(player), originalDamage(&damage),
	positive(positiveEffectMultiplier), negative(negativeEffectMultiplier)
{
	DamageCache cache(originalDamage);
	for(const auto * unit : parent->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || !unit->willMove() || unit->isTimeStopped()) continue;
		PotentialTargets targets(unit, cache, parent);
		if(targets.possibleAttacks.empty()) continue;
		const auto & best = targets.bestAction();
		auto fixed = best.attack;
		// A generated forced-mode candidate may borrow a temporary child world.
		// Retain value-only attack facts and rebind both endpoints to this reference's
		// immutable owning parent before PotentialTargets releases its query lease.
		fixed.attacker = parent->battleGetUnitByID(best.attack.attacker->unitId());
		fixed.defender = parent->battleGetUnitByID(best.attack.defender->unitId());
		if(!fixed.attacker || !fixed.defender)
			throw std::logic_error("Fixed attack endpoint is outside its owning parent");
		const auto repriced = AttackPossibility::evaluate(fixed, best.from, cache, parent, best.perfectMoment);
		const auto index = actions.size();
		actions.push_back({fixed, best.from, best.perfectMoment,
			repriced.attackValue() * moraleExposure(*parent, *unit)});
		std::set<uint32_t> touched{best.attack.attacker->unitId(), best.attack.defender->unitId()};
		for(const auto & affected : best.affectedUnits) if(affected) touched.insert(affected->unitId());
		for(const auto id : touched) touchedActions[id].push_back(index);
	}
}
std::optional<float> Reference::marginal(const std::shared_ptr<HypotheticBattle> & outcome,
	std::span<const uint32_t> changedRecipients) const
{
	if(!outcome) return std::nullopt;
	DamageCache cache(originalDamage);
	double value = 0;
	for(const auto id : changedRecipients)
	{
		const auto * before = parent->battleGetUnitByID(id);
		if(!before) return std::nullopt;
		const auto * after = outcome->battleGetUnitByID(before->unitId());
		const auto delta = (after ? after->getAvailableHealth() : 0) - before->getAvailableHealth();
		const bool ours = parent->battleGetActionController(before) == player;
		const auto * poisonedBefore = dynamic_cast<const battle::CUnitState *>(before);
		const auto * poisonedAfter = dynamic_cast<const battle::CUnitState *>(after);
		const auto poisonDelta = physicalPoisonValue::projectedDamage(poisonedBefore)
			- physicalPoisonValue::projectedDamage(poisonedAfter);
		if(poisonDelta != 0)
		{
			const float sign = (poisonDelta > 0) == ours ? 1.0f : -1.0f;
			value += sign * AttackPossibility::calculateDamageReduce(nullptr, before,
				static_cast<uint64_t>(poisonDelta > 0 ? poisonDelta : -poisonDelta), cache, outcome)
				* (sign > 0 ? positive : negative);
		}
		if(!delta) continue;
		if(ours && delta > 0 && after && (after->isSummoned() || after->isClone() || after->isGhost()))
			continue; // Retain the ordinary magical-healing utility exclusion.
		const auto magnitude = AttackPossibility::calculateDamageReduce(nullptr, before,
			static_cast<uint64_t>(delta > 0 ? delta : -delta), cache, outcome);
		const float sign = (delta > 0) == ours ? 1.0f : -1.0f;
		value += sign * magnitude * (sign > 0 ? positive : negative);
	}
	std::set<size_t> changedActions;
	for(const auto id : changedRecipients)
		if(const auto found = touchedActions.find(id); found != touchedActions.end())
			changedActions.insert(found->second.begin(), found->second.end());
	for(const auto index : changedActions)
	{
		const auto & fixed = actions[index];
		const auto * attacker = outcome->battleGetUnitByID(fixed.attack.attacker->unitId());
		const auto * defender = outcome->battleGetUnitByID(fixed.attack.defender->unitId());
		float after = 0;
const bool forced = attacker && attacker->hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE)
			&& !newHorizonsPuppetMaster::hasValidControlMarker(*outcome, attacker);
		if(attacker && defender && attacker->alive() && defender->alive()
			&& attacker->willMove() && !attacker->isTimeStopped()
			&& (forced || !outcome->battleMatchActionController(attacker, defender, true)))
		{
			auto attack = fixed.attack;
			attack.attacker = attacker;
			attack.defender = defender;
			const auto projected = AttackPossibility::evaluate(attack, fixed.from, cache, outcome, fixed.perfectMoment);
			after = projected.attackValue() * moraleExposure(*outcome, *attacker);
		}
		const bool oursBefore = parent->battleGetActionController(fixed.attack.attacker) == player;
		const bool oursAfter = attacker && outcome->battleGetActionController(attacker) == player;
		const float delta = (oursAfter ? 1 : -1) * after - (oursBefore ? 1 : -1) * fixed.before;
		value += delta * (delta >= 0 ? positive : negative);
	}
	const auto result = static_cast<float>(value);
	return std::isfinite(result) ? std::optional<float>(result) : std::nullopt;
}
}
