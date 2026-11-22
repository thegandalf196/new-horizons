/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "DispelValue.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/spells/CSpell.h"

namespace dispelAI
{
namespace
{
/// One activation's best strike by `attackerId` on `board`. Damage comes from
/// `board`; its value uses the weights of `weights` (the board before the
/// removal), so an enemy whose buffs were stripped does not also become a less
/// valuable target, and a debuffed enemy freed of its debuff does not look
/// better to kill. Same nominal proxy as expectedTargetActivationValue.
float bestStrike(uint32_t attackerId, const std::shared_ptr<HypotheticBattle> & board, DamageCache & cache,
	const std::shared_ptr<HypotheticBattle> & weights, DamageCache & weightCache)
{
	const auto * attacker = board->battleGetUnitByID(attackerId);
	float best = 0;
	for(const auto * target : board->battleGetAllUnits(false))
	{
		if(!target || !target->alive() || target->isGhost() || target->isTurret() || !target->isValidTarget(true)
			|| board->battleGetOwner(target) == board->battleGetOwner(attacker)
			|| (!board->battleCanAttackUnitAction(attacker, target)
				&& !board->battleCanShootAction(attacker, target->getPosition()))) continue;
		const bool shooting = board->battleCanShoot(attacker, target->getPosition());
		const int64_t count = AttackPossibility::getAttackCount(*attacker, shooting, *board);
		const auto health = std::max<int64_t>(0, target->getAvailableHealth());
		const auto damage = std::min(health, std::min(health, cache.getDamage(attacker, target, board)) * count);
		const auto * weighted = weights->battleGetUnitByID(target->unitId());
		if(damage > 0 && weighted)
			best = std::max(best, AttackPossibility::calculateDamageReduce(nullptr, weighted,
				static_cast<uint64_t>(damage), weightCache, weights));
	}
	return best;
}

/// Removing effects from one stack changes only its own strikes and those of
/// its enemies, so only they contribute.
float strikeChange(uint32_t id, PlayerColor reference, const std::shared_ptr<HypotheticBattle> & after,
	DamageCache & afterCache, const std::shared_ptr<HypotheticBattle> & restored, DamageCache & restoredCache)
{
	const auto * unit = after->battleGetUnitByID(id);
	const auto owner = after->battleGetOwner(unit);
	float result = 0;
	for(const auto * other : after->battleGetAllUnits(false))
	{
		if(!other || !other->alive() || other->isGhost() || other->isTurret()
			|| (other != unit && after->battleGetOwner(other) == owner)
			|| !restored->battleGetUnitByID(other->unitId())) continue;
		const auto change = bestStrike(other->unitId(), after, afterCache, restored, restoredCache)
			- bestStrike(other->unitId(), restored, restoredCache, restored, restoredCache);
		result += after->battleGetOwner(other) == reference ? change : -change;
	}
	return result;
}

std::vector<Bonus> removedEffects(const battle::Unit & before, const battle::Unit & after)
{
	std::vector<Bonus> result;
	const auto remaining = after.getBonusesFrom(BonusSource::SPELL_EFFECT);
	const auto effects = before.getUnstackedBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT));
	for(const auto & bonus : *effects)
		if(bonus && !remaining->getFirst(Selector::source(BonusSource::SPELL_EFFECT, bonus->sid)))
			result.push_back(*bonus);
	return result;
}

int longestRounds(const std::vector<Bonus> & removed)
{
	int rounds = 1;
	for(const auto & bonus : removed)
		if(Bonus::NTurns(&bonus)) rounds = std::max(rounds, static_cast<int>(bonus.turnsRemain));
	return rounds;
}
}

bool removesSpellEffects(const CSpell * spell)
{
	return spell && spell->getId() == SpellID::DISPEL;
}

float removedEffectValue(const CSpell * spell, const CBattleInfoCallback & before,
	const std::shared_ptr<HypotheticBattle> & after, PlayerColor reference, float positiveMultiplier, float negativeMultiplier)
{
	if(!after || !removesSpellEffects(spell)) return 0;
	DamageCache afterCache;
	float result = 0;
	for(const auto * unit : after->battleGetAllUnits(false))
	{
		const auto * original = unit && unit->alive() ? before.battleGetUnitByID(unit->unitId()) : nullptr;
		if(!original || !original->alive()) continue;
		const auto removed = removedEffects(*original, *unit);
		if(removed.empty()) continue;
		battleSearch::require(after->searchBudget().get(), battleSearch::WorkKind::Node);
		// Restore only the removed effects on a child of the projected board, so
		// other consequences of the cast stay on both sides of the comparison.
		auto restored = std::make_shared<HypotheticBattle>(after->env, after);
		restored->addUnitBonus(unit->unitId(), removed);
		DamageCache restoredCache; // Changed bonuses invalidate inherited pair certificates.
		const auto change = strikeChange(unit->unitId(), reference, after, afterCache, restored, restoredCache);
		result += change * 0.5f * static_cast<float>(longestRounds(removed));
	}
	return result * (result >= 0 ? positiveMultiplier : negativeMultiplier);
}
}
