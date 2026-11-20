/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicDoubleCast.h"
#include "../json/JsonNode.h"

namespace newHorizonsMetamagic
{
bool requiresDoubleCastFormat(const JsonNode & rules)
{
	if(!rules.isStruct())
		return false;
	const auto & prayers = rules["divineMandatePolicy"];
	const bool chosenPrayers = prayers.isStruct()
		&& prayers["version"].getType() == JsonNode::JsonType::DATA_INTEGER && prayers["version"].Integer() == 4;
	return chosenPrayers || claimsDoubleCastPolicy(rules);
}
}
