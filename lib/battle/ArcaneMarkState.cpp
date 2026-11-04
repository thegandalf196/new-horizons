/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneMarkState.h"
#include "PhysicalPoisonSpellOrigin.h"
#include "../json/JsonNode.h"

namespace battle
{
namespace
{
size_t index(BattleSide side)
{
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		throw std::invalid_argument("Arcane Marks require an original hero battle side");
	return static_cast<size_t>(side);
}
const JsonNode * field(const JsonNode & object, const char * name)
{
	if(!object.isStruct())
		return nullptr;
	const auto it = object.Struct().find(name);
	return it == object.Struct().end() ? nullptr : &it->second;
}
}
int32_t ArcaneMarkState::get(BattleSide side) const
{
	return marks.at(index(side));
}
void ArcaneMarkState::set(BattleSide side, int32_t count)
{
	if(count < 0 || count > MAX_MARKS)
		throw std::out_of_range("Arcane Mark count must be between zero and five");
	marks.at(index(side)) = static_cast<uint8_t>(count);
}
void ArcaneMarkState::add(BattleSide side, int32_t count)
{
	if(count < 0)
		throw std::out_of_range("Cannot add a negative Arcane Mark count");
	const auto slot = index(side);
	marks[slot] = static_cast<uint8_t>(std::min<int64_t>(MAX_MARKS, static_cast<int64_t>(marks[slot]) + count));
}
JsonNode ArcaneMarkState::toJson() const
{
	JsonNode result;
	result.Vector().emplace_back(static_cast<int>(marks[0]));
	result.Vector().emplace_back(static_cast<int>(marks[1]));
	return result;
}
ArcaneMarkState ArcaneMarkState::fromJson(const JsonNode & data)
{
	if(!data.isVector() || data.Vector().size() != 2)
		throw std::runtime_error("Arcane Marks must contain exactly two integer counters");
	ArcaneMarkState result;
	for(size_t slot = 0; slot < 2; ++slot)
	{
		const auto & value = data.Vector()[slot];
		if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 0 || value.Integer() > MAX_MARKS)
			throw std::runtime_error("Arcane Mark counter must be an integer from zero through five");
		result.marks[slot] = static_cast<uint8_t>(value.Integer());
	}
	return result;
}
ArcaneMarkState arcaneMarksFromUnitJson(const JsonNode & unit)
{
	const auto * state = field(unit, "state");
	const auto * marks = state ? field(*state, "arcaneMarks") : nullptr;
	return marks ? ArcaneMarkState::fromJson(*marks) : ArcaneMarkState{};
}
bool demonicGateOpenedFromUnitJson(const JsonNode & unit)
{
	const auto * state = field(unit, "state");
	const auto * value = state ? field(*state, "demonicGateOpened") : nullptr;
	if(!value)
		return false;
	if(!value->isBool())
		throw std::runtime_error("Demonic Gate use must be a boolean");
	return value->Bool();
}
bool hasPassiveFactionUnitState(const JsonNode & unit)
{
	const auto marks = arcaneMarksFromUnitJson(unit);
	const bool opened = demonicGateOpenedFromUnitJson(unit);
	const auto poisonSide = physicalPoisonSpellOriginFromJson(unit);
	return marks.hasState() || opened || poisonSide != BattleSide::NONE;
}
}
