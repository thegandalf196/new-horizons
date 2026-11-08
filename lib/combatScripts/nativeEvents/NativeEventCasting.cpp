/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/AbilityCaster.h"
#include "../../spells/BattleSpellMechanics.h"
#include "../../spells/CSpell.h"
#include "../../spells/ISpellMechanics.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../spells/NewHorizonsMetamagicEffects.h"
#include <vstd/RNG.h>
#include <cmath>

namespace nativeEvents
{
int hostileBinomial(const Context & context, const battle::Unit & actor, const battle::Unit & recipient,
	int trials, double probability, int cap)
{
	if(trials < 0 || cap < 0 || !std::isfinite(probability) || probability < 0 || probability > 1)
		throw std::invalid_argument("Hostile binomial ability requires nonnegative counts and a chance in [0, 1].");
	const auto drawCapped = [&]() { return std::min(context.server.getRNG()->nextBinomialInt(trials, probability), cap); };
	const auto valid = [](BattleSide side) { return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER; };
	const auto actorSide = context.battle.playerToSide(context.battle.battleGetOwner(&actor));
	const auto harmedSide = context.battle.playerToSide(context.battle.battleGetOwner(&recipient));
	const bool hostile = valid(actorSide) && valid(harmedSide) && actorSide != harmedSide;
	const bool stochastic = trials > 0 && cap > 0 && probability > 0 && probability < 1;
	if(!actor.alive() || !recipient.alive() || !recipient.isValidTarget(false)
		|| recipient.isInvincible() || !hostile || !stochastic)
		return drawCapped();
	int result = 0;
	context.server.resolveAdverseCombatRoll(context.battle.getBattle()->getBattleID(), harmedSide, true, true,
		[&]() { result = drawCapped(); return result > 0; });
	return result;
}

bool receptive(const Context & context, const battle::Unit & caster, const CSpell & spell, const battle::Unit & recipient)
{
	spells::AbilityCaster ability(&caster, 0);
	spells::BattleCast cast(&context.battle, &ability, spells::Mode::PASSIVE, &spell);
	auto mechanics = spell.battleMechanics(&cast);
	return recipient.isValidTarget(false) && !recipient.isInvincible() && mechanics && mechanics->isReceptive(&recipient);
}

void cast(const Context & context, const battle::Unit & caster, const CSpell & spell,
	const std::vector<const battle::Unit *> & targets, int64_t effectValue)
{
	spells::AbilityCaster ability(&caster, 0);
	spells::BattleCast cast(&context.battle, &ability, spells::Mode::PASSIVE, &spell);
	cast.setEffectValue(effectValue);
	spells::Target destinations;
	for(const auto * unit : targets) if(unit) destinations.emplace_back(unit);
	cast.cast(&context.server, destinations);
}

void applyEffects(const Context & context, const battle::Unit & caster, const CSpell & spell,
	const std::vector<const battle::Unit *> & targets, int level, int duration, bool ignoreImmunity)
{
	spells::BattleCast cast(&context.battle, &caster, spells::Mode::PASSIVE, &spell);
	cast.setSpellLevel(level);
	cast.setEffectDuration(duration);
	spells::Target destinations;
	for(const auto * unit : targets) if(unit) destinations.emplace_back(unit);
	cast.applyEffects(&context.server, destinations, false, ignoreImmunity);
}

int64_t adjustDamage(const Context & context, const CSpell & spell, const battle::Unit & source,
	const battle::Unit & target, int64_t rawDamage)
{
	const spells::Caster * caster = context.battle.battleGetFightingHero(source.unitSide());
	if(!caster) caster = &source;
	const auto * state = context.battle.getBattle();
	const bool fractional = state && newHorizonsMagic::rulesActive(state->getMagicRules())
		&& state->getMagicRules()["rulesetVersion"].Integer() == newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
	const int perkReduction = spell.isMagical() ? context.battle.battleGetPerkMagicalReductionBasisPoints(&target) : 0;
	const auto ordinary = spell.adjustRawDamage(caster, &target, rawDamage, 0,
		context.battle.battleGetHoldTheLineMagicalReductionBasisPoints(&target), 100,
		context.battle.battleUsesNewHorizonsMultiplicativeMDR(), fractional, true, perkReduction);
	return newHorizonsMetamagic::deferredSpellDamage(ordinary, &context.parameters["mdrPenetration"],
		context.battle, &target, caster->getCasterOwner());
}
}
