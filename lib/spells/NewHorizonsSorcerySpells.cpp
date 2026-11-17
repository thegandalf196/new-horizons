/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsSorcerySpells.h"
#include "ISpellMechanics.h"
#include "../CStack.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/AccessibilityInfo.h"

namespace newHorizonsSorcerySpells
{
std::optional<BattleHex> forcedRecallDestination(const CBattleInfoCallback & battle,
	const battle::Unit & unit, BattleHex anchor)
{
	if(!unit.alive() || !unit.isValidTarget(false) || unit.isGhost()
		|| !anchor.isAvailable() || unit.getPosition() == anchor
		|| unit.hasBonusOfType(BonusType::SIEGE_WEAPON)) return std::nullopt;
	const auto destination = battle.getAccessibility(&unit).nearestLegalPosition(anchor,
		unit.doubleWide(), unit.unitSide());
	return destination && *destination != unit.getPosition() ? destination : std::nullopt;
}

std::optional<BattleHex> forcedRecallDestination(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	const auto * original = battle.battleGetStackByID(unit.unitId());
	return original ? forcedRecallDestination(battle, unit, original->initialPosition) : std::nullopt;
}

int64_t forceLanceDamage(const spells::Mechanics & mechanics)
{
	return mechanics.scaleHeroLuckDamage(std::max<int64_t>(0,
		mechanics.applySpellBonus(mechanics.getEffectValue(), nullptr)));
}

Bonus forceLancePin(SpellID spell)
{
	Bonus pin(BonusDuration::N_TURNS, BonusType::BIND_EFFECT, BonusSource::SPELL_EFFECT, 1, BonusSourceID(spell));
	pin.turnsRemain = 1;
	pin.stacking = FORCE_LANCE;
	return pin;
}
}
