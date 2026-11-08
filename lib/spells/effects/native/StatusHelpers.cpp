/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StatusHelpers.h"
#include "../../../bonuses/BonusFilter.h"
#include "../../../json/JsonBonus.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsDivineRetribution.h"
#include "../../../combatScripts/NativeJsonParameters.h"

namespace spells::effects::native::status
{
bool isTemporary(const Bonus & bonus)
{
	return bonus.duration != 0 && !(bonus.duration & BonusDuration::PERMANENT);
}

JsonNode bonus(const std::string & type, int64_t value, const std::string & source,
	int turns, const std::string & subtype, bool debuff)
{
	JsonNode result;
	result["type"].String() = type;
	if(!subtype.empty()) result["subtype"].String() = subtype;
	result["val"].Integer() = value;
	result["duration"].String() = "N_TURNS";
	result["turns"].Integer() = turns;
	result["sourceType"].String() = "SPELL_EFFECT";
	result["sourceID"].String() = source;
	result["stacking"].String() = source;
	if(debuff)
	{
		result["statusTags"].Vector().emplace_back("DEBUFF");
		result["statusIdentity"].String() = source;
	}
	return result;
}

void add(const nativeEvents::Context & context, const battle::Unit & unit, JsonNode data, bool cumulative)
{
	const auto record = nativeRules::bonusFromDescriptor(data);
	nativeEvents::addBonus(context, unit, record, cumulative);
}

BonusList previous(const battle::Unit & unit, BonusType type, const std::string & source, const std::string & subtype)
{
	TConstBonusListPtr candidates;
	if(subtype.empty()) candidates = unit.getBonusesOfType(type);
	else candidates = unit.getBonusesOfType(type, decodeBonusSubtype(type, subtype));
	BonusList result;
	for(const auto & record : *candidates)
		if(record->source == BonusSource::SPELL_EFFECT && record->sid.toString() == source) result.push_back(record);
	return result;
}

void refresh(const nativeEvents::Context & context, const battle::Unit & unit, BonusType type,
	const std::string & source, const std::string & subtype)
{
	const auto old = previous(unit, type, source, subtype);
	if(!old.empty()) nativeEvents::removeBonuses(context, unit, old);
}

bool reject(const Mechanics * mechanics, Problem * problem)
{
	if(problem) mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, *problem);
	return false;
}
}
