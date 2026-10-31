/*
 * HeroSpellCastContext, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "../../Global.h"
#include <cstdint>
#include <optional>
class CGHeroInstance;
class CSpell;
class CBattleInfoCallback;
namespace newHorizonsMagic
{
/// Caster-side deterministic scalar inputs; no recipient lookup or cast execution.
struct DLL_LINKAGE HeroSpellCastContext
{
	int32_t warcasting = 0;
	int32_t arcaneFocus = 0;
	int32_t consecrated = 0;
	int32_t counterpressure = 0;
	int32_t crossSchool = 0;
	int32_t grandFormula = 100;
	int32_t concentration = 0;
	int32_t componentBonusPercent() const;
};
DLL_LINKAGE HeroSpellCastContext heroSpellCastContext(const CGHeroInstance & hero,
	const CSpell & spell, const CBattleInfoCallback & callback, bool followup, bool concentrated = false);
DLL_LINKAGE bool heroCastExtendAvailable(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback & callback, int32_t effectLevel);
DLL_LINKAGE int32_t brimstoneSpellPowerBonus(const CGHeroInstance & hero, const CBattleInfoCallback & callback);
/// Missing selected target omits Split Focus, never guesses a recipient.
DLL_LINKAGE int32_t metamagicPowerBonus(const CGHeroInstance & hero, const CSpell & spell,
	const CBattleInfoCallback & callback, std::optional<uint32_t> selectedTarget = std::nullopt);
}
