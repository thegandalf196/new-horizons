/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "PossibleSpellcast.h"
#include "../../lib/battle/BattleSide.h"
#include <memory>
#include <vector>

class CBattleInfoCallback;
class DamageCache;

/// Battle AI riders for the revised Rampart specialties that a spell candidate's
/// own forecast cannot see, because the authority applies them after the cast:
/// Uland's Living Remedy Regeneration on his Cure and Alagar's Brittle on his ice
/// spells. Heuristic exposure terms in HP-damage units, like the Castle riders;
/// no search. Damage-side specialties (Giant-Slayer, Use the Ground, Brittle,
/// Perfect Mount, Treantmoot, Forest Ambush) and Gem's free first spell reach the
/// AI through the shared damage and spell-cost rules instead.
namespace rampartSpecialtyAI
{
/// Expected Regeneration Uland's Cure leaves on a friendly stack (0 when inactive).
DLL_LINKAGE float livingRemedyValue(const CBattleInfoCallback & battle, BattleSide side, uint32_t unit);
/// Extra damage the Brittle mark lets this side's ready stacks deal to the enemy
/// stack, plus the share of it a cancelled Defend would have absorbed (0 when inactive).
DLL_LINKAGE float brittleValue(const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & board,
	BattleSide side, uint32_t unit, DamageCache & damage);
/// Unscaled rider value of one hero spell candidate (0 when it has none).
DLL_LINKAGE float riderValue(const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & board,
	BattleSide side, const PossibleSpellcast & candidate, DamageCache & damage);
DLL_LINKAGE void addSpecialtyValue(std::vector<PossibleSpellcast> & candidates, const CBattleInfoCallback & battle,
	const std::shared_ptr<CBattleInfoCallback> & board, BattleSide side, DamageCache & damage, float positive);
}
