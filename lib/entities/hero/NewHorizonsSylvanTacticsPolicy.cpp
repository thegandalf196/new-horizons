/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkRules.h"
#include <stdexcept>
namespace newHorizonsHeroes
{
bool usesSylvanTactics(const JsonNode & rules)
{
	if(!rules.isStruct() || !rules.Struct().contains("sylvanTacticsPolicy")) return false;
	const auto & policy = rules["sylvanTacticsPolicy"];
	if(policy.getType() != JsonNode::JsonType::DATA_INTEGER || policy.Integer() != 1)
		throw std::runtime_error("Invalid Sylvan Tactics policy");
	return true;
}
void validateSylvanTacticsSerialization(const JsonNode & rules, bool supported)
{
	if(usesSylvanTactics(rules) && !supported)
		throw std::runtime_error("Cannot discard captured Sylvan Tactics policy");
}
}
