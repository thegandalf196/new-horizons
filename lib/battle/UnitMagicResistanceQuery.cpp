/*
 * UnitMagicResistanceQuery.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "UnitMagicResistanceQuery.h"
#include "CBattleInfoCallback.h"
#include "IBattleState.h"
#include "Unit.h"
#include "../spells/NewHorizonsMagic.h"
#include <algorithm>

namespace
{
constexpr int NEW_HORIZONS_MAGIC_RESISTANCE_CAP_PERCENT = 75;

class LiveMagicResistanceQuery final : public battle::UnitMagicResistanceQuery
{
	const CBattleInfoCallback & callback;
public:
	explicit LiveMagicResistanceQuery(const CBattleInfoCallback & callback) : callback(callback) {}
protected:
	battle::Units adjacentUnits(const battle::Unit & unit) const override
	{
		return callback.battleAdjacentUnits(&unit);
	}
	bool newHorizonsRulesActive() const override
	{
		const auto * battleState = callback.getBattle();
		return battleState && newHorizonsMagic::rulesActive(battleState->getMagicRules());
	}
};
}

int battle::UnitMagicResistanceQuery::query(const Unit * unit) const
{
	if(!unit)
		return 0;
	requireSubject(*unit);
	int resistance = std::clamp(unit->valOfBonuses(BonusType::MAGIC_RESISTANCE), 0, 100);
	int auraResistance = 0;
	for(const auto * adjacent : adjacentUnits(*unit))
	{
		if(adjacent->unitOwner() == unit->unitOwner())
			auraResistance = std::max(auraResistance, adjacent->valOfBonuses(BonusType::SPELL_RESISTANCE_AURA));
	}
	auraResistance = std::clamp(auraResistance, 0, 100);

	// Independent sources retain legacy integer flooring, not float rounding.
	const int combinedResistance = (10000 - (100 - resistance) * (100 - auraResistance)) / 100;
	const int maximumResistance = newHorizonsRulesActive()
		? NEW_HORIZONS_MAGIC_RESISTANCE_CAP_PERCENT : 100;
	return std::min(combinedResistance, maximumResistance);
}

int CBattleInfoCallback::battleGetMagicResistance(const battle::Unit * unit) const
{
	return LiveMagicResistanceQuery(*this).query(unit);
}
