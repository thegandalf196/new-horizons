/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "CastleSpecialtyFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CObstacleInstance.h"
#include "../../../lib/battle/NewHorizonsRampartSpecialties.h"
#include "../../../lib/bonuses/BonusSelector.h"
#include "../../../lib/entities/hero/NewHorizonsPassiveSpecialtyBonuses.h"
#include "../../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"

namespace rampartCombatTest
{
constexpr auto ATTACKER = BattleSide::ATTACKER;
constexpr auto DEFENDER = BattleSide::DEFENDER;

/// Revised Rampart combat specialties on the shipped rules, cast and struck through
/// the authoritative battle processor. `stripped` reproduces a 0.93.1 capture.
class RampartCombatSpecialtyTest : public CastleSpecialtyTest
{
protected:
	double multiplier(const CStack * attacker, const CStack * defender, bool shooting = false) const
	{
		return newHorizonsRampart::attackDamageMultiplier(*battle(), BattleAttackInfo(attacker, defender, 0, shooting));
	}

	int64_t spellDamage(SpellID spell, const CStack * target) const
	{
		spells::BattleCast cast(battle(), attackerSideHero, spells::Mode::HERO, spell.toSpell());
		return cast.getSpell()->battleMechanics(&cast)->adjustEffectValue(target);
	}

	const Bonus * tagged(const battle::Unit & unit, const char * stacking) const
	{
		return unit.getBonus(CSelector([stacking](const Bonus * bonus) { return bonus->stacking == stacking; })).get();
	}

	void forceField(BattleHex hex)
	{
		SpellCreatedObstacle field;
		field.uniqueID = 900 + hex.toInt();
		field.ID = SpellID::FORCE_FIELD;
		field.trigger = SpellID::NONE;
		field.pos = hex;
		field.casterSide = BattleSide::NONE;
		field.turnsRemaining = 3;
		field.passable = false;
		field.customSize.insert(hex);
		BattleObstaclesChanged add;
		add.battleID = BattleID(0);
		add.change = ObstacleChanges(field.uniqueID, BattleChanges::EOperation::ADD);
		field.toInfo(add.change);
		gameHandler->sendAndApply(add);
	}

	/// The specialist's passive creature bundle, as initHero installs it for a hero of that type.
	void installPassives()
	{
		newHorizonsHeroes::refreshLevelScaledSpecialtyBonus(*attackerSideHero);
		newHorizonsHeroes::restoreMissingPassiveCreatureSpecialtyBonuses(*attackerSideHero);
	}
};
}
