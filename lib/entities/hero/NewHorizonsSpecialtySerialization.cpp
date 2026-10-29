/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsSpecialtySerialization.h"
#include "NewHorizonsForestAmbush.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsChosenSpecialties.h"
#include "NewHorizonsLearningExchangeSpecialties.h"
#include "NewHorizonsSwampSeer.h"
#include "NewHorizonsReclaimer.h"
#include "NewHorizonsManaLeeches.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "NewHorizonsGhostSpecialty.h"
#include "NewHorizonsParalyzingGaze.h"
#include "NewHorizonsPetrifyingBlood.h"
#include "NewHorizonsLivingInferno.h"
#include "NewHorizonsAerialSupremacy.h"
#include "NewHorizonsDreadCavalry.h"

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
	validateChosenSpecialtiesSerialization(development, context.chosenSpecialties);
	validateManaLeechesSerialization(development, context.manaLeeches);
	validateSerpentsEmbraceSerialization(development, context.serpentsEmbrace);
	validateGhostSpecialtySerialization(development, context.ghostSpecialty);
	validateParalyzingGazeSerialization(development, context.paralyzingGaze);
	validateForestAmbushSerialization(development, context.forestAmbush);
	validatePetrifyingBloodSerialization(development, context.petrifyingBlood);
	validateLivingInfernoSerialization(development, context.livingInferno);
	validateAerialSupremacySerialization(development, context.aerialSupremacy);
	validateDreadCavalrySerialization(development, context.dreadCavalry);
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
	current.chosenSpecialties = true;
	current.manaLeeches = true;
	current.serpentsEmbrace = true;
	current.ghostSpecialty = true;
	current.paralyzingGaze = true;
	current.forestAmbush = true;
	current.petrifyingBlood = true;
	current.livingInferno = true;
	current.aerialSupremacy = true;
	current.dreadCavalry = true;
	validateCapturedSpecialtiesSerialization(development, current);
}

void copyCapturedIndividualSpecialties(JsonNode & target, const JsonNode & source)
{
	for(const auto key : {"individualSpecialties", "schoolSpellcraftSpecialties", "additionalSpecialties",
		"chosenSpecialties", "recoverySpecialties", "learningExchangeSpecialties", "swampSeerSpecialty",
		"reclamationSpecialty", "ghostSpecialty", "petrifyingBloodSpecialty", "livingInfernoSpecialty", "dreadCavalrySpecialty"})
		if(source.Struct().contains(key))
			target[key] = source[key];
}

}
