/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CurrentFireBasis.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/NewHorizonsMagic.h"
namespace newHorizonsMagicalAbilityDamage
{
namespace
{
constexpr auto HOLY_ARMOR_SAVED_ROSTER_KEY = "new-horizons:holyArmor";
constexpr auto WARCASTING_SKILL_ID = "new-horizons:warcasting";
constexpr auto SPELLWARD_PERK_ID = "new-horizons:warcasting.spellward";
constexpr int SPELLWARD_REDUCTION_BASIS_POINTS = 1000;
}
bool usesMultiplicativeMDR(const JsonNode & magicRules)
{
	return newHorizonsMagic::rulesActive(magicRules)
		&& magicRules["spells"].isStruct()
		&& magicRules["spells"].Struct().contains(HOLY_ARMOR_SAVED_ROSTER_KEY);
}
int spellwardReductionBasisPoints(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk(WARCASTING_SKILL_ID, SPELLWARD_PERK_ID)
		? SPELLWARD_REDUCTION_BASIS_POINTS
			+ hero->getMasterSpellwardExtraBasisPoints(SPELLWARD_REDUCTION_BASIS_POINTS) : 0;
}
}
