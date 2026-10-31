/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
struct BattleAttackInfo;
namespace newHorizonsOrdinaryRangedSpecialtyRules
{
/// Read-only casualty predicate; original-species counts survive battle forms.
DLL_LINKAGE bool orcCasualtyShot(const BattleAttackInfo & attack);
/// Extra percentage for an Orc-family ordinary physical shot against a victim
/// currently below its combat-entry species count. Caller supplies entitlement
/// and a captured coefficient in [0,100]; zero disables the premium.
/// Ranged collateral/reactions qualify too; this rule is not primary-only.
DLL_LINKAGE int orcCasualtyDamagePercent(const BattleAttackInfo & attack, int capturedPercent);
}
