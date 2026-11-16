/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCommandAdmission.h"
#include "NewHorizonsPerkRules.h"
#include "NewHorizonsNecromancyPolicy.h"
#include "NewHorizonsCapabilityRules.h"
#include "../../mapObjects/NewHorizonsEstatesPolicy.h"

namespace newHorizonsHeroes
{
void validateCommandPerkSerialization(const JsonNode & perks, bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported)
{
	newHorizonsEstates::validateRevisedSerialization(perks, estatesSupported);
	validateFourOfferProgressionSerialization(perks, fourOfferSupported);
	validateUnifiedSkillProgressionSerialization(perks, leadershipSupported);
	newHorizonsNecromancy::validateLeadershipPolicySerialization(perks, leadershipSupported);
	// Check the Necromancy revision itself, even if a malformed capture omitted
	// the Command marker. Old revision-1/2 envelopes retain their separate gates.
	newHorizonsNecromancy::validateCommandPolicySerialization(perks, supported);
	if(usesCommandAuthorityPolicy(perks) && !supported)
		throw std::runtime_error("Authority perk policy requires a new game/save format");
	newHorizonsWarfare::validateRulesSerialization(perks, supported);
}

void validateCommandCaptures(const JsonNode & capabilities, const JsonNode & perks, const JsonNode & combat,
	bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported)
{
	validateLeadershipEfficiencySerialization(capabilities, leadershipSupported);
	validateArmyCommandSerialization(capabilities, supported);
	validateCommandPerkSerialization(perks, supported, leadershipSupported, fourOfferSupported, estatesSupported);
	newHorizonsWarfare::validateRulesSerialization(combat, supported);
}

void validateCommandOverrides(const JsonNode & overrides, bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported)
{
	validateCommandCaptures(overrides["heroes"]["newHorizonsCapabilities"],
		overrides["heroes"]["newHorizonsPerks"], overrides["combat"]["heroCommands"], supported, leadershipSupported, fourOfferSupported, estatesSupported);
}

void validateCommandCrossover(const JsonNode & hero, bool supported, bool leadershipSupported, bool fourOfferSupported, bool estatesSupported)
{
	validateCommandCaptures(hero["capabilityRules"], hero["perkState"]["rules"], JsonNode(), supported, leadershipSupported, fourOfferSupported, estatesSupported);
	const auto rules = capturedCommandRules(hero["capabilityRules"]);
	CommandState::fromJson(hero["commandProgression"], rules).validateSerialization(rules, supported);
}
}
