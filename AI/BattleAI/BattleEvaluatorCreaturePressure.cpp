/*
 * BattleEvaluatorCreaturePressure.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "CreatureSpellTiming.h"
#include <bit>
#include "../../lib/CStack.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../lib/callback/CBattleCallback.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/ISpellMechanics.h"
#include "SpellTargetsEvaluator.h"


namespace
{
void recordCreatureSelection(creatureSpellTiming::Sample & timing, const PossibleSpellcast & selected)
{
	timing.selected = true;
	timing.representativeSpell = selected.spell->getId().getNum();
	timing.selectedValueBits = std::bit_cast<uint32_t>(selected.value);
	if(selected.dest.size() == 1 && selected.dest.front().unitValue)
		timing.recipient = selected.dest.front().unitValue->unitId();
}
}

float BattleEvaluator::beneficialCreaturePressure(const std::shared_ptr<HypotheticBattle> & state,
	uint32_t spentCasterId, DamageCache & cache, creatureSpellTiming::Pressure * timing) const
{
	creatureSpellTiming::Scope total(timing ? &timing->total : nullptr);
	float value = 0;
	creatureSpellTiming::measure(timing ? &timing->cache : nullptr, [&]()
	{
		cache.buildCreaturePressureCache(state, side);
	});
	for(const auto * unit : state->getUnitsIf([&](const battle::Unit * unit)
	{
		return unit->alive() && unit->unitId() != spentCasterId && !unit->isTimeStopped();
	}))
	{
		// Reuse ordinary spell evaluation's physical-attack valuation, without
		// advancing turns or running a battle queue for each random outcome.
		if(timing)
			++timing->eligibleUnits;
		creatureSpellTiming::Scope targetTiming(timing ? &timing->targets : nullptr);
		PotentialTargets targets(unit, cache, state);
		if(targets.possibleAttacks.empty())
			continue;
		const float pressure = static_cast<float>(targets.bestActionValue());
		value += state->battleGetActionController(unit) == playerID ? pressure : -pressure;
	}
	return value;
}

std::optional<PossibleSpellcast> BattleEvaluator::findBestRandomCreatureSpell(const CStack * stack,
	int abilityLevel, creatureSpellTiming::Sample * timing)
{
	const auto battle = cb->getBattle(battleID);
	auto baseline = creatureSpellTiming::measure(timing ? &timing->baselineSetup : nullptr, [&]()
	{
		return std::make_shared<HypotheticBattle>(env.get(), battle);
	});
	DamageCache baselineCache;
	const float baselinePressure = beneficialCreaturePressure(baseline, stack->unitId(), baselineCache, timing ? &timing->baseline : nullptr);
	std::optional<PossibleSpellcast> best;
	for(const auto * recipient : battle->battleAliveUnits())
	{
		if(timing)
			++timing->recipients;
		const auto pool = battle->getAvailableRandomCreatureSpells(stack, recipient);
		if(pool.empty())
			continue;
		if(timing)
		{
			++timing->nonemptyPools;
			timing->outcomes += pool.size();
		}
		double total = 0;
		for(const auto spell : pool)
			total += beneficialCreatureOutcomeValue(stack, recipient, spell.toSpell(),
				creatureAbilitySpellLevel(stack, spell.toSpell(), abilityLevel), baselinePressure, timing);
		const float value = static_cast<float>(total / pool.size());
		if(std::isfinite(value) && value > 0 && (!best || value > best->value))
		{
			PossibleSpellcast candidate;
			// The server replaces this representative ID with its own random
			// outcome from the same recipient-specific pool upon acceptance.
			candidate.spell = pool.front().toSpell();
			candidate.dest = {spells::Destination(recipient)};
			candidate.value = value;
			best = std::move(candidate);
		}
	}
	return best;
}

std::optional<PossibleSpellcast> BattleEvaluator::findBestCreatureSpell(const CStack * stack, creatureSpellTiming::Sample * details)
{
	if(!stack->canCast())
		return std::nullopt;
	const auto randomAbility = stack->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	creatureSpellTiming::Call measurement(details);
	auto * timing = measurement.sink();
	if(timing)
	{
		timing->battle = battleID.getNum();
		timing->player = playerID.getNum();
		timing->side = static_cast<int>(side);
		timing->round = cb->getBattle(battleID)->getBattle()->getRound();
		timing->unit = stack->unitId();
		timing->creature = stack->creatureId().getNum();
		timing->randomPool = static_cast<bool>(randomAbility);
	}
	if(randomAbility)
	{
		auto best = findBestRandomCreatureSpell(stack, randomAbility->val, timing);
		if(timing && best)
			recordCreatureSelection(*timing, *best);
		return best;
	}

	std::vector<SpellID> spellsToCast;
	TConstBonusListPtr bl = stack->getBonusesOfType(BonusType::SPELLCASTER);

	//TODO: faerie dragon type spell should be selected by server
	SpellID creatureSpellToCast = cb->getBattle(battleID)->getRandomCastedSpell(CRandomGenerator::getDefault(), stack);

	for(const auto & bonus : *bl)
		if(!bonus->parameters && bonus->subtype.as<SpellID>().hasValue())
			spellsToCast.push_back(bonus->subtype.as<SpellID>());

	if(creatureSpellToCast.hasValue())
		spellsToCast.push_back(creatureSpellToCast);

	std::vector<PossibleSpellcast> possibleCasts;
	std::optional<float> beneficialBaseline;

	for(const auto spellID : spellsToCast)
	{
		if(timing)
			++timing->ordinarySpells;
		const CSpell * spell = spellID.toSpell();

		if(!spell->canBeCast(cb->getBattle(battleID).get(), spells::Mode::CREATURE_ACTIVE, stack))
			continue;

		spells::BattleCast temp(cb->getBattle(battleID).get(), stack, spells::Mode::CREATURE_ACTIVE, spell);
		for(const auto & target : SpellTargetEvaluator::getViableTargets(spell->battleMechanics(&temp).get()))
		{
			if(timing)
				++timing->ordinaryTargets;
			PossibleSpellcast ps;
			ps.dest = target;
			ps.spell = spell;
			const auto completion = creatureSpellTiming::measure(timing ? &timing->ordinaryCast : nullptr, [&]()
			{
				return evaluateCreatureCastBeforeCompletion(stack, ps);
			});
			const auto ability = stack->getBonus(Selector::typeSubtype(BonusType::SPELLCASTER,
				BonusSubtypeID(spellID)));
			// Only fill the existing zero-HP blind spot for an ordinary friendly
			// single-target buff. Damage, healing, summons and weighted abilities
			// retain their existing scoring and selection policies.
			bool beneficialFallback = false;
			if(ps.value == 0 && spell->isPositive() && ability && !ability->parameters
				&& target.size() == 1 && target.front().unitValue
				&& cb->getBattle(battleID)->battleMatchActionController(stack, target.front().unitValue, true))
			{
				beneficialFallback = true;
				if(!beneficialBaseline)
				{
					auto baseline = creatureSpellTiming::measure(timing ? &timing->baselineSetup : nullptr, [&]()
					{
						return std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
					});
					DamageCache cache;
					beneficialBaseline = beneficialCreaturePressure(baseline, stack->unitId(), cache, timing ? &timing->baseline : nullptr);
				}
				ps.value = beneficialCreatureOutcomeValue(stack, target.front().unitValue, spell,
					creatureAbilitySpellLevel(stack, spell, ability->val), *beneficialBaseline, timing);
			}
			// The zero-HP fallback is future-DPS-valued; do not mix raw HP into it.
			// Admission and the cast-only zero survive until this lane is chosen.
			ps.value += beneficialFallback ? completion.second : completion.first;
			possibleCasts.push_back(ps);
		}
	}

	std::sort(
		possibleCasts.begin(), possibleCasts.end(),
		[&](const PossibleSpellcast & lhs, const PossibleSpellcast & rhs)
		{
			return lhs.value > rhs.value;
		}
	);

	if(!possibleCasts.empty() && possibleCasts.front().value > 0)
	{
		if(timing)
			recordCreatureSelection(*timing, possibleCasts.front());
		return possibleCasts.front();
	}

	return std::nullopt;
}

int BattleEvaluator::creatureAbilitySpellLevel(const CStack * caster, const CSpell * spell, int abilityLevel)
{
	int spellLevel = std::max(0, abilityLevel);
	if(const auto spellAbility = caster->getBonus(Selector::typeSubtype(
		BonusType::SPELLCASTER, BonusSubtypeID(spell->getId()))))
		vstd::amax(spellLevel, spellAbility->val);
	// Match BattleActionProcessor: only the ANY-school battlefield raise is
	// applied to creature casts, not school-specific hero proficiency.
	if(spell->getLevel() > 0)
		vstd::amax(spellLevel, caster->valOfBonuses(BonusType::MAGIC_SCHOOL_SKILL, BonusSubtypeID(SpellSchool::ANY)));
	return spellLevel;
}

float BattleEvaluator::beneficialCreatureOutcomeValue(const CStack * caster,
	const battle::Unit * recipient, const CSpell * spell, int spellLevel, float baselinePressure, creatureSpellTiming::Sample * timing) const
{
	const auto battle = cb->getBattle(battleID);
	if(!recipient || !spell)
		return 0;
	auto state = creatureSpellTiming::measure(timing ? &timing->outcomeSetup : nullptr, [&]()
	{
		return std::make_shared<HypotheticBattle>(env.get(), battle);
	});
	const auto * projectedCaster = state->battleGetUnitByID(caster->unitId());
	const auto * projectedRecipient = state->battleGetUnitByID(recipient->unitId());
	newHorizonsPuppetMaster::ActionControllerCaster actionCaster(projectedCaster,
		state->battleGetActionController(projectedCaster));
	spells::BattleCast cast(state.get(), &actionCaster, spells::Mode::CREATURE_ACTIVE, spell);
	{
		creatureSpellTiming::Scope castTiming(timing ? &timing->outcomeCast : nullptr);
		cast.setSpellLevel(spellLevel);
		cast.castEval(state->getServerCallback(), {spells::Destination(projectedRecipient)});
	}
	DamageCache cache;
	const float pressure = beneficialCreaturePressure(state, caster->unitId(), cache, timing ? &timing->outcome : nullptr) - baselinePressure;
	creatureSpellTiming::Scope healingTiming(timing ? &timing->outcomeHealing : nullptr);
	const auto * restored = state->battleGetUnitByID(recipient->unitId());
	const int64_t healed = restored->getAvailableHealth() - recipient->getAvailableHealth();
	const float healingValue = healed > 0
		? AttackPossibility::calculateDamageReduce(nullptr, recipient, healed, cache, state) : 0;
	return (pressure + healingValue) * scoreEvaluator.getPositiveEffectMultiplier();
}

float BattleEvaluator::expectedBeneficialCreatureSpellValue(const CStack * caster,
	const battle::Unit * recipient) const
{
	const auto battle = cb->getBattle(battleID);
	if(!caster || !caster->canCast() || !recipient)
		return 0;
	const auto pool = battle->getAvailableRandomCreatureSpells(caster, recipient);
	const auto ability = caster->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	if(pool.empty() || !ability)
		return 0;
	auto baseline = std::make_shared<HypotheticBattle>(env.get(), battle);
	DamageCache cache;
	const float baselinePressure = beneficialCreaturePressure(baseline, caster->unitId(), cache);
	double total = 0;
	for(const auto spell : pool)
		total += beneficialCreatureOutcomeValue(caster, recipient, spell.toSpell(),
			creatureAbilitySpellLevel(caster, spell.toSpell(), ability->val), baselinePressure);
	return static_cast<float>(total / pool.size());
}

float BattleEvaluator::beneficialCreatureSpellOutcomeValue(const CStack * caster,
	const battle::Unit * recipient, const CSpell * spell) const
{
	if(!caster || !recipient || !spell)
		return 0;
	const auto randomAbility = caster->getBonus(Selector::type()(BonusType::RANDOM_SPELLCASTER));
	const auto ability = randomAbility ? randomAbility : caster->getBonus(Selector::typeSubtype(
		BonusType::SPELLCASTER, BonusSubtypeID(spell->getId())));
	if(!ability)
		return 0;
	auto baseline = std::make_shared<HypotheticBattle>(env.get(), cb->getBattle(battleID));
	DamageCache cache;
	return beneficialCreatureOutcomeValue(caster, recipient, spell,
		creatureAbilitySpellLevel(caster, spell, ability->val),
		beneficialCreaturePressure(baseline, caster->unitId(), cache));
}
