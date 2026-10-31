/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsRangefinder.h"
#include "Unit.h"
#include "BattleHexArray.h"
#include "../bonuses/IBonusBearer.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"
bool CBattleInfoCallback::battleHasDistancePenalty(const IBonusBearer * shooter, const BattleHex & shooterPosition, const BattleHex & destHex) const
{
	RETURN_IF_NOT_BATTLE(false);
	if(const auto penalty = newHorizonsRangefinder::distancePenalty(*shooter, shooterPosition, destHex, *this))
		return *penalty;

	const std::string cachingStrNoDistancePenalty = "type_NO_DISTANCE_PENALTY";
	static const auto selectorNoDistancePenalty = Selector::type()(BonusType::NO_DISTANCE_PENALTY);

	if(shooter->hasBonus(selectorNoDistancePenalty, cachingStrNoDistancePenalty))
		return false;

	if(const auto * target = battleGetUnitByPos(destHex, true))
	{
		//If any hex of target creature is within range, there is no penalty
		int range = GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE;

		auto bonus = shooter->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
		if(bonus != nullptr && bonus->parameters)
			range = bonus->parameters->toNumber();

		if(isEnemyUnitWithinSpecifiedRange(shooterPosition, target, range))
			return false;
	}
	else
	{
		if(BattleHex::getDistance(shooterPosition, destHex) <= GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE)
			return false;
	}

	return true;
}

bool CBattleInfoCallback::isEnemyUnitWithinSpecifiedRange(const BattleHex & attackerPosition, const battle::Unit * defenderUnit, unsigned int range) const
{
	for(const auto & hex : defenderUnit->getHexes())
		if(BattleHex::getDistance(attackerPosition, hex) <= range)
			return true;

	return false;
}

bool CBattleInfoCallback::isHexWithinSpecifiedRange(const BattleHex & attackerPosition, const BattleHex & targetPosition, unsigned int range) const
{
	if(BattleHex::getDistance(attackerPosition, targetPosition) <= range)
		return true;

	return false;
}
