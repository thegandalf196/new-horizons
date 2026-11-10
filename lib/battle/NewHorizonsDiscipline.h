/*
 * NewHorizonsDiscipline.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
#include <utility>
#include <vector>

class CGHeroInstance;

namespace newHorizonsDiscipline
{
inline constexpr const char * SKILL = "new-horizons:discipline";
inline constexpr const char * STEADFAST = "new-horizons:discipline.steadfast";
inline constexpr const char * ESPRIT_DE_CORPS = "new-horizons:discipline.espritDeCorps";
inline constexpr const char * HOLD_FAST = "new-horizons:discipline.holdFast";
inline constexpr const char * FEARLESS = "new-horizons:discipline.fearless";
inline constexpr const char * VETERAN_COHESION = "new-horizons:discipline.veteranCohesion";

DLL_LINKAGE bool hasSteadfast(const CGHeroInstance * hero);
DLL_LINKAGE bool hasEspritDeCorps(const CGHeroInstance * hero);
/// Clone negative army-composition contributions before ordinary stacking.
/// The unstacked form permits the battle's separate Steadfast adjustment.
DLL_LINKAGE TConstBonusListPtr espritDeCorpsMoraleBonuses(const CGHeroInstance * hero,
	const IBonusBearer & bearer, bool stackBonuses = true);
/// Pure source adjustment, using limiter-applied/updater-processed raw Morale records.
DLL_LINKAGE TConstBonusListPtr espritDeCorpsMoraleBonuses(TConstBonusListPtr unstacked,
	PlayerColor owner, bool stackBonuses = true);
DLL_LINKAGE int32_t espritDeCorpsMoraleAdjustment(const CGHeroInstance * hero, const IBonusBearer & bearer);
/// Derived hero-only inputs absent from the raw bonus graph. Capture once at
/// authoritative export; descendants retain these values, never a live hero query.
struct HeroMoraleContext
{
	bool hasHero = false;
	bool espritDeCorps = false;
	PlayerColor owner = PlayerColor::CANNOT_DETERMINE;
	std::pair<int32_t, int32_t> limits{0, 0};
	std::vector<Bonus> glyphs;
};
DLL_LINKAGE HeroMoraleContext captureHeroMoraleContext(const CGHeroInstance * hero);
/// Same source adjustment, stacking, derived contributions and flags as actual
/// hero moraleVal(). Records must be this hero's current unstacked graph view,
/// not creature-recipient records or an already adjusted effective Morale list.
DLL_LINKAGE int heroMoraleValue(const HeroMoraleContext & context, TConstBonusListPtr unstacked);
DLL_LINKAGE bool hasHoldFast(const CGHeroInstance * hero);
DLL_LINKAGE bool hasFearless(const CGHeroInstance * hero);
DLL_LINKAGE bool hasVeteranCohesion(const CGHeroInstance * hero);
/// Expose one earned personal modifier, replacing inherited synthetic copies.
DLL_LINKAGE TConstBonusListPtr veteranCohesionBonuses(TConstBonusListPtr original,
	const CSelector & selector, bool earned);
DLL_LINKAGE Bonus holdFastMoraleFloorBonus();
DLL_LINKAGE bool isHoldFastMoraleFloorBonus(const Bonus * bonus);
DLL_LINKAGE CSelector holdFastMoraleFloorBonusSelector();
}
