/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "PreparedBonusRule.h"
#include <string_view>

class CCreature;

namespace nativeRules
{
DLL_LINKAGE std::shared_ptr<Bonus> prepareSkillRankBonus(std::shared_ptr<Bonus> bonus);
DLL_LINKAGE std::shared_ptr<Bonus> prepareFamilySpecialtyBonus(const Bonus & bonus);
DLL_LINKAGE std::shared_ptr<Bonus> prepareAdjacentSpecialtyBonus(const Bonus & bonus);
enum class AuraPolicy : uint8_t { FriendlyDefense, NativeUndeadDefense, EnemyMeleeReduction, FriendlyMeleeDamage, DirectMagicProtection };
DLL_LINKAGE void configureAdjacentSpecialtyBonus(Bonus & bonus, CreatureID family, AuraPolicy policy);
/// Typed constant templates are prepared once; cast IDs and duration bind without JSON parsing.
DLL_LINKAGE PreparedBonusRule sanctuaryKeeperTemplate();
DLL_LINKAGE PreparedBonusRule creatureSuppressionTemplate(bool intrinsicPassives);
DLL_LINKAGE Bonus sanctuaryKeeperMorale(SpellID sanctuary);
DLL_LINKAGE Bonus creatureSuppression(SpellID spell, std::string_view identity, int32_t rounds, bool intrinsicPassives);
}
