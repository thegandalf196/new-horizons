/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPetrifyingBlood.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "../entities/hero/NewHorizonsPetrifyingBlood.h"
#include "../spells/CSpell.h"
#include "../spells/CSpellHandler.h"
#include "../spells/ISpellMechanics.h"
#include "../spells/AbilityCaster.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusList.h"
namespace newHorizonsPetrifyingBlood
{
bool eligiblePair(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::Unit & basilisk)
{
	const auto creature = basilisk.creatureId();
	if(creature != CreatureID(CreatureID::decode("core:basilisk"))
		&& creature != CreatureID(CreatureID::decode("core:greaterBasilisk")))
		return false;
	return attacker
		&& attacker->unitId() != basilisk.unitId()
		&& battle.battleMatchOwner(attacker, &basilisk, false)
		&& newHorizonsHeroes::hasPetrifyingBlood(battle.battleGetOwnerHero(&basilisk));
}
bool qualifies(const CBattleInfoCallback & battle, const battle::Unit * attacker,
	const battle::CUnitState & basilisk, int64_t actualDamage, bool ranged, bool physical)
{
	return !ranged && physical && actualDamage > 0 && battle.battleGetRound() >= 0
		&& basilisk.petrifyingBloodRound != battle.battleGetRound()
		&& eligiblePair(battle, attacker, basilisk);
}
bool isPetrified(const battle::Unit & unit)
{
	return unit.hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::STONE_GAZE)))
		.And(Selector::type()(BonusType::NOT_ACTIVE)));
}
int chanceBasisPoints(const CBattleInfoCallback & battle,
	const battle::Unit & basilisk, const battle::Unit & attacker)
{
	if(!attacker.alive() || !basilisk.alive() || isPetrified(attacker))
		return 0;
	const auto id = SpellID(SpellID::STONE_GAZE);
	const auto selector = Selector::typeSubtype(BonusType::SPELL_AFTER_ATTACK, BonusSubtypeID(id));
	const auto bonuses = basilisk.getBonuses(selector);
	int level = 0;
	bool melee = false;
	for(const auto & bonus : *bonuses)
	{
		const auto values = bonus->parameters ? bonus->parameters->toVector() : std::vector<int32_t>{};
		if(!values.empty())
			level = std::max(level, values[0]);
		const int mode = values.size() > 1 ? values[1] : -1;
		melee = melee || mode == -1 || mode == 0 || mode == 2;
	}
	if(!melee)
		return 0;
	spells::AbilityCaster caster(&basilisk, level);
	spells::BattleCast cast(&battle, &caster, spells::Mode::PASSIVE, id.toSpell());
	spells::Target target;
	target.emplace_back(&attacker);
	if(!id.toSpell()->battleMechanics(&cast)->canBeCastAt(target))
		return 0;
	return basilisk.favorableCreatureAbilityChanceBasisPoints(std::clamp(basilisk.valOfBonuses(selector), 0, 100));
}
}
