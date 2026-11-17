/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/battle/AccessibilityInfo.h"
namespace battleSearch { class SearchBudget; }
class CBattleInfoCallback;
namespace spellTargetBudgetFallback
{
bool mineHexIsEmpty(const CBattleInfoCallback &, const AccessibilityInfo &, const BattleHex &);
std::vector<spells::Target> firstPlacementAim(const spells::Mechanics *, bool, battleSearch::SearchBudget *);
std::vector<spells::Target> firstUnitAim(spells::Mechanics *, bool, battleSearch::SearchBudget *);
}
