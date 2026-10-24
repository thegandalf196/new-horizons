/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSpecialtySerialization.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "NewHorizonsSwampSeer.h"
#include "NewHorizonsReclaimer.h"

namespace newHorizonsHeroes
{
void validateCapturedSpecialtiesSerialization(const JsonNode & development,
	const SpecialtySerializationContext & context)
{
	validateIndividualSpecialtiesSerialization(development, context.fortuneAndArcane,
		context.individualRiders, context.schoolSpellcraft, context.additional, context.recovery);
	validateLearningExchangeSpecialtiesSerialization(development, context.remainingRiders);
	validateSwampSeerSerialization(development, context.remainingRiders);
	validateReclamationSpecialtySerialization(development, context.remainingRiders);
}

void validateCurrentSpecialtyRules(const JsonNode & development)
{
	SpecialtySerializationContext current;
	current.fortuneAndArcane = true;
	current.individualRiders = true;
	current.schoolSpellcraft = true;
	current.additional = true;
	current.recovery = true;
	current.remainingRiders = true;
	validateCapturedSpecialtiesSerialization(development, current);
}

}
