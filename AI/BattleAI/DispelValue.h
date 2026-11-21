/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/constants/EntityIdentifiers.h"
#include <memory>
class CBattleInfoCallback;
class CSpell;
class HypotheticBattle;
namespace dispelAI
{
/// Dispel removes every removable spell effect from its targets, whoever cast
/// it, so a Dispel on a hostile stack also strips this side's own debuffs.
bool removesSpellEffects(const CSpell * spell);
/// Zero unless `spell` removes spell effects; otherwise the signed value, for
/// `reference`, of the spell effects present on `before` but absent from
/// `after`. Each affected stack's change in best single-activation strike (its
/// own and that of every stack hostile to it, valued with the pre-removal
/// weights) is credited for half the longest removed duration, mirroring the
/// cast-time Curse and Sorrow estimates. Removing a hostile stack's debuffs or a friendly stack's
/// buffs is a loss; removing the opposite is a gain. Positive and negative
/// results are scaled by the evaluator's matching effect multiplier.
float removedEffectValue(const CSpell * spell, const CBattleInfoCallback & before,
	const std::shared_ptr<HypotheticBattle> & after, PlayerColor reference, float positiveMultiplier, float negativeMultiplier);
}
