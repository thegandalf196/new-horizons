/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CHealth.h"
#include "CUnitState.h"
#include "../json/JsonNode.h"
#include <limits>
#include <algorithm>
#include <stdexcept>
namespace battle
{
namespace
{
const JsonNode * eternalField(const JsonNode & object, const char * key)
{
	if(!object.isStruct()) return nullptr;
	const auto found = object.Struct().find(key);
	return found == object.Struct().end() ? nullptr : &found->second;
}
}
bool hasEternalServitudeState(const JsonNode & unitSnapshot)
{
	const auto * state = eternalField(unitSnapshot, "state");
	if(!state || !state->isStruct()) return false;
	bool present = false;
	for(const auto * name : {"health", "battleFormOriginalHealth"})
	{
		const auto * health = eternalField(*state, name);
		const auto * cohorts = health ? eternalField(*health, "casualtyProvenance") : nullptr;
		if(!cohorts) continue;
		if(!cohorts->isVector()) throw std::runtime_error("Invalid Eternal Servitude cohorts");
		for(const auto & cohort : cohorts->Vector())
		{
			const auto * marker = eternalField(cohort, "eternalServitudeRestored");
			if(!marker) continue;
			if(marker->getType() != JsonNode::JsonType::DATA_INTEGER || marker->Integer() < 0
				|| marker->Integer() > std::numeric_limits<int32_t>::max())
				throw std::runtime_error("Invalid Eternal Servitude restoration count");
			const auto * temporary = eternalField(cohort, "temporarilyRestored");
			if(!temporary || temporary->getType() != JsonNode::JsonType::DATA_INTEGER
				|| temporary->Integer() < marker->Integer())
				throw std::runtime_error("Eternal Servitude exceeds temporary restoration");
			present |= marker->Integer() != 0;
		}
	}
	return present;
}
CHealth CHealth::prepareEternalServitudeRestoration()
{
	ensureCasualtyProvenanceLedger();
	return *this;
}
void CHealth::markEternalServitudeRestoration(const CHealth & previous)
{
	ensureCasualtyProvenanceLedger();
	if(casualtyProvenance.size() != previous.casualtyProvenance.size())
		throw std::logic_error("Eternal Servitude changed casualty provenance shape");
	for(size_t i = 0; i < casualtyProvenance.size(); ++i)
	{
		auto & cohort = casualtyProvenance[i];
		const auto & old = previous.casualtyProvenance[i];
		if(cohort.count != old.count || cohort.provenance != old.provenance
			|| cohort.temporarilyRestored < old.temporarilyRestored)
			throw std::logic_error("Eternal Servitude requires one temporary restoration");
		cohort.eternalServitudeRestored += cohort.temporarilyRestored - old.temporarilyRestored;
	}
	validateCasualtyProvenanceLedger();
}
int32_t CHealth::retainEternalServitude(int32_t percent)
{
	if(percent < 0 || percent > 100)
		throw std::invalid_argument("Invalid Eternal Servitude percentage");
	if(getCount() == 0 || percent == 0) return 0;
	int64_t eligible = 0;
	for(const auto & cohort : casualtyProvenance)
		eligible += cohort.eternalServitudeRestored;
	const auto retained = static_cast<int32_t>(eligible * percent / 100);
	int32_t remaining = retained;
	for(auto & cohort : casualtyProvenance)
	{
		const auto selected = std::min(remaining, cohort.eternalServitudeRestored);
		cohort.count -= selected;
		cohort.temporarilyRestored -= selected;
		cohort.eternalServitudeRestored -= selected;
		resurrected -= selected;
		remaining -= selected;
	}
	std::erase_if(casualtyProvenance, [](const auto & cohort) { return cohort.count == 0; });
	validateCasualtyProvenanceLedger();
	return retained;
}
}
