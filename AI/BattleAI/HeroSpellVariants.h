/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../lib/battle/BattleSide.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <string>
#include <memory>
#include <utility>
#include <vector>
class CBattleInfoCallback;
class CGHeroInstance;
class CSpell;
class PossibleSpellcast;
namespace spells { class BattleCast; class Mechanics; }
namespace heroSpellVariants
{
struct Prepared
{
	int maxOvercharge;
	bool canUseSelectiveDispel;
	bool canUseTemporalField;
	std::vector<std::pair<SpellID, std::string>> cureAfflictionChoices;
	std::vector<int32_t> shadowGiftChoices;
};
/// Existing variant discovery only; preserves traversal and stable choice order.
/// Does not generate spell targets or evaluate a cast.
/// Exact target-aware new-policy mana/legality after selected Purify choices.
/// Legacy candidates retain their original earlier admission path.
bool targetAdmitted(const PossibleSpellcast &, spells::BattleCast &,
	std::unique_ptr<spells::Mechanics> &);
Prepared prepare(const CBattleInfoCallback &, const CGHeroInstance *, const CSpell *, BattleSide);
}
