/* NewHorizonsLightDamage.cpp, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "StdInc.h"
#include "NewHorizonsLightDamage.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
namespace newHorizonsMagic
{
int32_t holyWrathRecipientMultiplier(const JsonNode & rules, SpellID spell, bool undeadOrDemonic)
{
	return undeadOrDemonic && spell.toSpell()->getJsonKey() == "new-horizons:holyWrath" && rulesActive(rules)
		&& rules["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spellAllowedBySavedRoster(rules, spell) ? 150 : 100;
}
}
