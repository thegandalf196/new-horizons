/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CurePhysicalAffliction.h"
#include "../battle/NewHorizonsFrozen.h"
#include "../battle/NewHorizonsGraveDust.h"
#include "../battle/PhysicalAffliction.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusList.h"
#include <stdexcept>
namespace spells::curePhysical
{
bool livingTarget(const battle::Unit * unit)
{
	return unit && unit->isValidTarget(false) && unit->alive()
		&& !unit->hasBonusOfType(BonusType::UNDEAD)
		&& !unit->hasBonusOfType(BonusType::NON_LIVING)
		&& !unit->hasBonusOfType(BonusType::MECHANICAL)
		&& !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

bool validKind(const std::string_view kind)
{
	return kind == "frozen" || kind == "graveDust";
}
std::vector<Bonus> graveDustRemoval(const battle::Unit & unit)
{
	const auto reserved = unit.getUnstackedBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && newHorizonsGraveDust::isReservedSource(*bonus);
	}));
	if(!reserved) throw std::invalid_argument("Invalid Grave Dust provenance");
	if(!reserved->empty()) newHorizonsGraveDust::validateUnitGroup(unit);
	for(const auto & affliction : physicalAfflictions::enumerate(unit))
		if(affliction.kind == "graveDust")
		{
			return physicalAfflictions::removalPlan(unit, affliction);
		}
	return {};
}
bool present(const JsonNode & rules, const battle::Unit & unit, const std::string_view kind)
{
	if(kind == "frozen") return newHorizonsFrozen::enabled(rules) && newHorizonsFrozen::isFrozen(unit);
	if(kind == "graveDust") return !graveDustRemoval(unit).empty();
	return false;
}
}
