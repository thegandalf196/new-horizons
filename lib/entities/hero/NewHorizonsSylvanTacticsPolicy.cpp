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
	if(policy.getType() != JsonNode::JsonType::DATA_INTEGER || policy.Integer() < 1 || policy.Integer() > 5)
		throw std::runtime_error("Invalid Sylvan Tactics policy");
	return true;
}
bool usesSylvanTacticsV2(const JsonNode & rules)
{
	return usesSylvanTactics(rules) && rules["sylvanTacticsPolicy"].Integer() == 2;
}
bool usesSylvanTacticsV3(const JsonNode & rules)
{
	// Policy 4 (Sylvan Tactics 4.1) keeps the mixed roster, slots and icons of
	// policy 3 and revises only three of its effects. Policy 5 (Ambush) keeps
	// the retained slots of that roster and replaces three of its perks.
	return usesSylvanTactics(rules) && rules["sylvanTacticsPolicy"].Integer() >= 3;
}
bool usesSylvanTacticsV4(const JsonNode & rules)
{
	return usesSylvanTactics(rules) && rules["sylvanTacticsPolicy"].Integer() == 4;
}
bool usesSylvanTacticsV5(const JsonNode & rules)
{
	return usesSylvanTactics(rules) && rules["sylvanTacticsPolicy"].Integer() == 5;
}
bool requiresSylvanAmbushFormat(const JsonNode & rules)
{
	// Never throw for unrelated documents: callers scan every captured kind.
	if(!rules.isStruct() || !rules.Struct().contains("sylvanTacticsPolicy"))
		return false;
	const auto & policy = rules["sylvanTacticsPolicy"];
	return policy.getType() == JsonNode::JsonType::DATA_INTEGER && policy.Integer() == 5;
}
void validateSylvanTacticsSerialization(const JsonNode & rules, bool supported)
{
	if(usesSylvanTactics(rules) && !supported)
		throw std::runtime_error("Cannot discard captured Sylvan Tactics policy");
}
}
