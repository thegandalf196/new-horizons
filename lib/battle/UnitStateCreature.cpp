/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "UnitCreatureResolver.h"
#include "../CCreatureHandler.h"
#include "../GameLibrary.h"
#include "../json/JsonNode.h"
#include <stdexcept>
#include "../serializer/JsonSerializeFormat.h"
namespace battle
{
const CCreature * CUnitState::stateCreature(CreatureID id) const
{
	if(const auto * resolver = unitCreatureResolver()) return resolver->creature(id);
	return id.toCreature();
}
CreatureID CUnitState::decodeStateCreature(const std::string & key) const
{
	if(const auto * resolver = unitCreatureResolver()) return CreatureID(resolver->decode(key));
	return CreatureID(CreatureID::decode(key));
}
bool CUnitState::validStateCreature(CreatureID id) const
{
	if(const auto * resolver = unitCreatureResolver()) return resolver->creature(id) != nullptr;
	return id.toEntity(LIBRARY) != nullptr;
}
void CUnitState::validateOwnedFormIdentifiers(const JsonNode & data) const
{
	const auto * resolver = unitCreatureResolver();
	if(!resolver) return; // Native identifier resolution and reset order stay unchanged.
	if(!data.isStruct()) return;
	const auto & state = data["state"];
	if(!state.isStruct()) return;
	for(const auto * name : {"battleFormCreature", "battleFormOriginalCreature"})
	{
		const auto found = state.Struct().find(name);
		if(found == state.Struct().end() || found->second.isNull()) continue;
		if(!found->second.isString()) throw std::invalid_argument("Owned battle-form identifier must be a string");
		if(found->second.String().empty()) continue; // Existing default/absent ID semantics.
		const CreatureID id(resolver->decode(found->second.String()));
		if(!id.hasValue() || !resolver->creature(id)) throw std::invalid_argument("Missing owned battle-form descriptor");
	}
}
void CUnitState::serializeStateCreature(JsonSerializeFormat & handler, const std::string & field, CreatureID & id) const
{
	if(const auto * resolver = unitCreatureResolver())
		handler.serializeId(field, id, CreatureID(-1),
			[resolver](const std::string & key) { return resolver->decode(key); },
			[resolver](int32_t value) { return resolver->encode(value); });
	else
		handler.serializeId(field, id, CreatureID(-1)); // Exact original native identifier/metadata path.
}
const UnitCreatureResolver * CUnitStateDetached::unitCreatureResolver() const
{
	return unit->unitCreatureResolver();
}

int32_t CUnitState::creatureLevel() const
{
	return static_cast<int32_t>(stateCreature(battleFormCreature())->getLevel());
}

bool CUnitState::doubleWide() const
{
	return stateCreature(battleFormCreature())->isDoubleWide();
}

int32_t CUnitState::creatureCost() const
{
	return stateCreature(battleFormCreature())->getRecruitCost(EGameResID::GOLD);
}

int32_t CUnitState::creatureIconIndex() const
{
	return stateCreature(battleFormCreature())->getIconIndex();
}

FactionID CUnitState::getFactionID() const
{
	return stateCreature(battleFormCreature())->getFactionID();
}

std::string CUnitState::getCasterNameTextID() const
{
	if(const auto * resolver = unitCreatureResolver()) return resolver->creature(creatureId())->getNamePluralTextID();
	const auto * creature = creatureId().toEntity(LIBRARY);
	return creature->getNamePluralTextID();
}

const CCreature * CUnitStateDetached::unitType() const
{
	return hasBattleFormState() ? stateCreature(battleFormCreature()) : unit->unitType();
}

}
