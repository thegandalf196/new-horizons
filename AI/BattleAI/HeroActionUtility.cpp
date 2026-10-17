/*
 * HeroActionUtility.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroActionUtility.h"
#include "AttackPossibility.h"
#include "BattleExchangeVariant.h"
#include "HypotheticBattle.h"
#include "PossibleSpellcast.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../lib/spells/NewHorizonsPandemonium.h"

#include <cstdlib>

namespace heroActionUtility
{
std::optional<HealthDeltaUtility> accumulateHealthDelta(const HealthDeltaSubject & subject,
	const PossibleSpellcast & ps, PlayerColor playerID, DamageCache & innerCache,
	const std::shared_ptr<HypotheticBattle> & state, const BattleExchangeEvaluator & scoreEvaluator,
	ImmediateSignedUtility & immediate)
{
	const auto * unit = subject.projectedVictim;
	const auto * original = subject.beforeVictim;
	const auto oldHealth = subject.beforeHealth;
	const auto newHealth = subject.projectedHealth;
	const auto phantomArmyStack = subject.phantomArmyStack;
	auto & damageToHostilesScore = immediate.toHostiles;
	auto & damageToFriendliesScore = immediate.toFriendlies;
	if(state->isElementalRebirthSpawn(unit->unitId()) && oldHealth == 0
		&& newHealth > 0 && unit->unitType())
	{
		// Elemental Rebirth is a newly-created temporary stack, but unlike
		// generic magical summons it replaces destroyed army strength. Keep
		// its score in the existing creature-value / exact-HP scale.
		const auto maxHealth = std::max<int64_t>(1, unit->getMaxHealth());
		const float spawnValue = static_cast<float>(newHealth)
			* static_cast<float>(unit->unitType()->getAIValue())
			/ static_cast<float>(maxHealth);
		const bool ourUnit = state->battleGetOwner(unit) == playerID;
		damageToHostilesScore += ourUnit ? spawnValue : -spawnValue;
		return std::nullopt;
	}

	auto damage = std::abs(oldHealth - newHealth);
	auto originalDefender = original;

	auto dpsReduce = AttackPossibility::calculateDamageReduce(
		subject.measurementActor,
		originalDefender && originalDefender->alive() ? originalDefender : unit,
		damage,
		innerCache,
		state);
	const bool ourUnit = state->battleGetOwner(unit) == playerID;
	const bool goodEffect = newHealth > oldHealth;
	const bool naturesWrath = ps.spell
		&& ps.spell->getJsonKey() == newHorizonsNaturesWrath::SPELL_KEY;
	const bool pandemonium = ps.spell
		&& ps.spell->getJsonKey() == newHorizonsPandemonium::SPELL_KEY;
	if(original && !goodEffect && (pandemonium
		|| ((ps.spellStormOfDaggers || naturesWrath) && !ourUnit)))
	{
		// Pandemonium also permits friendly resistance. castEval uses
		// shared damage per recipient but leaves
		// that random roll out, so value its expected hit probability here.
		const int resistance = std::clamp(original->magicResistance(), 0, 100);
		dpsReduce *= 1.0f - static_cast<float>(resistance) / 100.0f;
	}

	if(ourUnit == goodEffect)
	{
		auto isMagical = state->getForUpdate(unit->unitId())->summoned
			|| unit->isClone()
			|| unit->isGhost()
			|| phantomArmyStack;

		if(ourUnit && goodEffect && isMagical && !naturesWrath)
			return std::nullopt;

		damageToHostilesScore += dpsReduce * scoreEvaluator.getPositiveEffectMultiplier();
	}
	else
		// discourage AI making collateral damage with spells
		damageToFriendliesScore -= 4 * dpsReduce * scoreEvaluator.getNegativeEffectMultiplier();
	return HealthDeltaUtility{dpsReduce};
}

AbsoluteHeroActionUtility composeAbsoluteUtility(AbsoluteContinuationUtility continuation,
	const ImmediateSignedUtility & immediate)
{
	if(vstd::isAlmostEqual(continuation.value, static_cast<float>(EvaluationResult::INEFFECTIVE_SCORE)))
		return {immediate.toFriendlies + immediate.toHostiles + immediate.initiative};
	return {continuation.value + immediate.toFriendlies + immediate.toHostiles + immediate.initiative};
}
}
