/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CUnitState.h"
#include "NewHorizonsRangefinder.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"
namespace battle
{
uint8_t CUnitState::getRangedFullDamageDistance() const
{
	if(!isShooter())
		return 0;
	if(const auto range = newHorizonsRangefinder::fullDamageDistance(*this))
		return *range;

	// overwrite full ranged damage distance with the value set in Additional info field of LIMITED_SHOOTING_RANGE bonus
	if(hasBonusOfType(BonusType::LIMITED_SHOOTING_RANGE))
	{
		auto bonus = this->getBonus(Selector::type()(BonusType::LIMITED_SHOOTING_RANGE));
		if(bonus != nullptr && bonus->parameters)
			return bonus->parameters->toNumber();
	}

	if (hasBonusOfType(BonusType::NO_DISTANCE_PENALTY))
		return GameConstants::BATTLE_SHOOTING_RANGE_DISTANCE;

	return GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE;
}

uint8_t CUnitState::getShootingRangeDistance() const
{
	if(!isShooter())
		return 0;

	uint8_t shootingRangeDistance = GameConstants::BATTLE_SHOOTING_RANGE_DISTANCE;

	// overwrite full ranged damage distance with the value set in Additional info field of LIMITED_SHOOTING_RANGE bonus
	if(hasBonusOfType(BonusType::LIMITED_SHOOTING_RANGE))
	{
		auto bonus = newHorizonsRangefinder::legalRangeBonus(*this);
		if(bonus != nullptr)
			shootingRangeDistance = bonus->val;
	}

	return shootingRangeDistance;
}

}
