/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include <algorithm>
#include "PassiveDivineMandate.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/NewHorizonsPerkState.h"
#include "../json/JsonNode.h"

#include <array>
#include <utility>

namespace newHorizonsPassiveDivineMandate
{
bool enabled(const JsonNode & capturedPerkRules)
{
	const auto & policy = capturedPerkRules["divineMandatePolicy"];
	return policy.isStruct() && policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& policy["version"].Integer() == 2;
}

Policy capturePolicy(const CGHeroInstance * hero)
{
	Policy result;
	if(!hero || hero->getFactionID() != FactionID::CASTLE
		|| !enabled(hero->getPerkState().rules))
		return result;
	result.rank = static_cast<uint8_t>(std::clamp(hero->getPerkSkillRank(SKILL), 0, 3));
	if(result.rank == 0)
		return result;
	static constexpr std::array<std::pair<const char *, Perk>, 10> identifiers = {{
		{"sacredCommand", Perk::SACRED_COMMAND},
		{"knightlyValor", Perk::KNIGHTLY_VALOR},
		{"chaplainSReserve", Perk::CHAPLAINS_RESERVE},
		{"shieldOfFaith", Perk::SHIELD_OF_FAITH},
		{"righteousDiscipline", Perk::RIGHTEOUS_DISCIPLINE},
		{"unwaveringStandard", Perk::UNWAVERING_STANDARD},
		{"inspiringLeadership", Perk::INSPIRING_LEADERSHIP},
		{"consecratedArms", Perk::CONSECRATED_ARMS},
		{"mandateOfHeaven", Perk::MANDATE_OF_HEAVEN},
		{"divineFury", Perk::DIVINE_FURY}
	}};
	for(const auto & [key, perk] : identifiers)
		if(hero->hasActivePerk(SKILL, std::string(SKILL) + "." + key))
			result.perks |= static_cast<uint16_t>(perk);
	result.validate();
	return result;
}
}
