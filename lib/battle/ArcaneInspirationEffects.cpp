/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneInspiration.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include <limits>
#include <stdexcept>

namespace arcaneInspiration
{
namespace
{
constexpr auto ACTIVE = "new-horizons:warcasting.arcaneInspiration";
constexpr auto RETAINED = "new-horizons:warcasting.lingeringInspiration";
constexpr auto VALOR_KEY = "new-horizons:warcasting.arcaneValor";
constexpr auto SHOCK = "new-horizons:warcasting.spellshock";
Bonus marker(const char * identity, BonusType type, int amount, PlayerColor owner, int32_t round)
{
	if(round < 0 || round == std::numeric_limits<int32_t>::max() || !owner.isValidPlayer())
		throw std::invalid_argument("Invalid Arcane Inspiration activation receipt");
	Bonus result(BonusDuration::ONE_BATTLE, type, BonusSource::HERO_SPECIAL, amount, BonusSourceID());
	result.stacking = identity;
	result.statusIdentity = identity;
	result.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	result.spellCasterOwner = owner;
	result.turnsRemain = round + 1;
	return result;
}
}
Bonus activeMarker(Profile policy, PlayerColor owner, int32_t round)
{
	if(policy.corePercent < 5 || policy.corePercent > 15 || policy.corePercent % 5 != 0 || policy.riders > 127)
		throw std::invalid_argument("Invalid Arcane Inspiration policy");
	auto result = marker(ACTIVE, BonusType::NONE, policy.corePercent, owner, round);
	result.subtype = BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(policy.riders)));
	result.description.appendRawString("Arcane Inspiration: creature attack and retaliation damage increased until the next combat round.");
	return result;
}
Bonus valorMarker(PlayerColor owner, int32_t round)
{
	auto result = marker(VALOR_KEY, BonusType::MORALE, 1, owner, round);
	result.description.appendRawString("Arcane Valor: +1 Morale until the next combat round.");
	return result;
}
Bonus spellshockMarker(PlayerColor owner, int32_t round)
{
	auto result = marker(SHOCK, BonusType::NONE, 10, owner, round);
	result.description.appendRawString("Spellshock: -10% attack and retaliation damage until the next combat round.");
	return result;
}
bool isEffect(const Bonus * bonus)
{
	if(!bonus || (bonus->stacking != ACTIVE && bonus->stacking != RETAINED
		&& bonus->stacking != VALOR_KEY && bonus->stacking != SHOCK))
		return false;
	const bool retained = bonus->stacking == RETAINED;
	if(bonus->source != BonusSource::HERO_SPECIAL || bonus->statusIdentity != bonus->stacking
		|| bonus->statusTags != std::vector<BonusStatusTag>{BonusStatusTag::NON_TRANSFERABLE}
		|| !bonus->spellCasterOwner.isValidPlayer() || bonus->valType != BonusValueType::BASE_NUMBER
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->parameters || bonus->limiter
		|| bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| bonus->duration != (retained ? BonusDuration::UNTIL_ATTACK : BonusDuration::ONE_BATTLE)
		|| (retained ? bonus->turnsRemain != 0 : bonus->turnsRemain < 1)
		|| bonus->type != (bonus->stacking == VALOR_KEY ? BonusType::MORALE : BonusType::NONE))
		throw std::runtime_error("Invalid Arcane Inspiration effect provenance");
	if(bonus->stacking == ACTIVE || retained)
	{
		if(bonus->val < 5 || bonus->val > 15 || bonus->val % 5 != 0
			|| bonus->subtype.getNum() < 0 || bonus->subtype.getNum() > (retained ? 0 : 127))
			throw std::runtime_error("Invalid Arcane Inspiration magnitude or riders");
	}
	else if(bonus->val != (bonus->stacking == SHOCK ? 10 : 1) || bonus->subtype != BonusSubtypeID())
		throw std::runtime_error("Invalid Arcane Inspiration rider");
	return true;
}
bool isActive(const Bonus * bonus) { return isEffect(bonus) && bonus->stacking == ACTIVE; }
bool isRetained(const Bonus * bonus) { return isEffect(bonus) && bonus->stacking == RETAINED; }
bool isSpellshock(const Bonus * bonus) { return isEffect(bonus) && bonus->stacking == SHOCK; }
Bonus retainedMarker(const Bonus & active)
{
	if(!isActive(&active))
		throw std::invalid_argument("Lingering Inspiration requires an active marker");
	auto result = active;
	result.stacking = RETAINED;
	result.statusIdentity = RETAINED;
	result.subtype = BonusSubtypeID(BonusCustomSubtype(0));
	result.duration = BonusDuration::UNTIL_ATTACK;
	result.turnsRemain = 0;
	result.description = MetaString();
	result.description.appendRawString("Lingering Inspiration: core bonus for the next attack or retaliation.");
	return result;
}
void advanceRound(IBattleState & battle, int32_t nextRound)
{
	for(const auto * unit : battle.getUnitsIf([](const battle::Unit *) { return true; }))
	{
		std::vector<Bonus> expired;
		for(const auto & effect : *unit->getBonuses(CSelector(isEffect)))
			if(!isRetained(effect.get()) && effect->turnsRemain <= nextRound)
				expired.push_back(*effect);
		if(!expired.empty())
			battle.removeUnitBonus(unit->unitId(), expired);
	}
}
}
