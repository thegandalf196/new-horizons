/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "DreadCavalryState.h"
#include "../json/JsonNode.h"
#include <algorithm>
namespace
{
int32_t integer(const JsonNode & value, int32_t minimum, int32_t maximum)
{
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER
		|| value.Integer() < minimum || value.Integer() > maximum)
		throw std::runtime_error("Invalid Dread Cavalry integer field");
	return static_cast<int32_t>(value.Integer());
}
}
namespace battle
{
bool DreadCavalryState::hasState() const
{
	return *this != DreadCavalryState{};
}
void DreadCavalryState::validateShape() const
{
	if(!pending)
	{
		auto expected = DreadCavalryState{};
		expected.usedThisActivation = usedThisActivation;
		if(*this != expected)
			throw std::runtime_error("Inactive Dread Cavalry carries a pending grant");
		return;
	}
	if(!usedThisActivation || victimId > static_cast<uint32_t>(std::numeric_limits<int32_t>::max())
		|| (controller != BattleSide::ATTACKER && controller != BattleSide::DEFENDER)
		|| remainingMovement < 0 || sharedTailCap < 0 || independentVanish < 0
		|| damagePercent < 1 || damagePercent > 100)
		throw std::runtime_error("Invalid pending Dread Cavalry grant");
}
void DreadCavalryState::arm(uint32_t victim, BattleSide currentController,
	int remaining, int outputPercent, int sharedCap, int vanish)
{
	validateShape();
	if(usedThisActivation)
		throw std::runtime_error("Dread Cavalry is already used in this Creature Activation");
	auto next = DreadCavalryState{};
	next.usedThisActivation = next.pending = true;
	next.victimId = victim; next.controller = currentController;
	next.remainingMovement = remaining; next.damagePercent = outputPercent;
	next.sharedTailCap = sharedCap; next.independentVanish = vanish;
	next.validateShape();
	*this = next;
}
void DreadCavalryState::retirePending()
{
	const bool used = usedThisActivation;
	*this = {};
	usedThisActivation = used;
}
int DreadCavalryState::legacyTailAfterMovement(int actualCost) const
{
	validateShape();
	if(!pending || actualCost < 0)
		throw std::runtime_error("Dread Cavalry requires its actual nonnegative movement cost");
	const int remaining = std::max(0, remainingMovement - actualCost);
	return std::max(independentVanish, std::min(sharedTailCap, remaining));
}
JsonNode DreadCavalryState::toJson() const
{
	validateShape();
	JsonNode node;
	if(!hasState())
		return node;
	node["version"].Integer() = 1;
	node["used"].Bool() = usedThisActivation;
	if(pending)
	{
		node["pending"].Bool() = true;
		node["victim"].Integer() = victimId;
		node["controller"].Integer() = static_cast<int32_t>(controller);
		node["remainingMovement"].Integer() = remainingMovement;
		node["damagePercent"].Integer() = damagePercent;
		node["sharedTailCap"].Integer() = sharedTailCap;
		node["independentVanish"].Integer() = independentVanish;
	}
	return node;
}
DreadCavalryState DreadCavalryState::fromJson(const JsonNode & node)
{
	DreadCavalryState result;
	if(node.isNull())
		return result;
	if(!node.isStruct())
		throw std::runtime_error("Dread Cavalry state must be an object");
	for(const auto & [key, value] : node.Struct())
		if(key != "version" && key != "used" && key != "pending" && key != "victim" && key != "controller"
			&& key != "remainingMovement" && key != "damagePercent" && key != "sharedTailCap" && key != "independentVanish")
			throw std::runtime_error("Unknown Dread Cavalry state field");
	integer(node["version"], 1, 1);
	if(!node["used"].isBool() || (node.Struct().contains("pending") && !node["pending"].isBool()))
		throw std::runtime_error("Dread Cavalry state flags must be boolean");
	result.usedThisActivation = node["used"].Bool();
	result.pending = node["pending"].isBool() && node["pending"].Bool();
	if(result.pending)
	{
		const auto maximum = std::numeric_limits<int32_t>::max();
		result.victimId = integer(node["victim"], 0, maximum);
		result.controller = static_cast<BattleSide>(integer(node["controller"], 0, 1));
		result.remainingMovement = integer(node["remainingMovement"], 0, maximum);
		result.damagePercent = integer(node["damagePercent"], 1, 100);
		result.sharedTailCap = integer(node["sharedTailCap"], 0, maximum);
		result.independentVanish = integer(node["independentVanish"], 0, maximum);
	}
	else
		for(const auto key : {"victim", "controller", "remainingMovement", "damagePercent", "sharedTailCap", "independentVanish"})
			if(node.Struct().contains(key))
				throw std::runtime_error("Inactive Dread Cavalry carries grant fields");
	result.validateShape();
	return result;
}
DreadCavalryState dreadCavalryStateFromUnitJson(const JsonNode & unit)
{
	return DreadCavalryState::fromJson(unit["state"]["dreadCavalry"]);
}
}
