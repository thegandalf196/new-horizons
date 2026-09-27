/*
 * NewHorizonsOffense.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "HeroCommand.h"
#include "../bonuses/IBonusBearer.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"

namespace newHorizonsOffense
{
inline constexpr const char * SKILL = "new-horizons:offense";
inline constexpr const char * CLEAVE = "new-horizons:offense.cleave";
inline constexpr const char * VENGEANCE = "new-horizons:offense.vengeance";
inline constexpr const char * RELENTLESS_ASSAULT = "new-horizons:offense.relentlessAssault";
inline constexpr const char * NO_QUARTER = "new-horizons:offense.noQuarter";
inline constexpr int CLEAVE_DAMAGE_PERCENT = 50;
inline constexpr int NO_QUARTER_MORALE_PENALTY = -2;

inline Bonus noQuarterRetaliationBonus()
{
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::NO_RETALIATION,
		BonusSource::OTHER, 1, BonusSourceID(BonusCustomSource::newHorizonsNoQuarter));
	bonus.stacking = NO_QUARTER;
	return bonus;
}

inline Bonus noQuarterMoralePenalty()
{
	Bonus bonus(BonusDuration::STACK_ACTIVATION, BonusType::MORALE,
		BonusSource::OTHER, NO_QUARTER_MORALE_PENALTY,
		BonusSourceID(BonusCustomSource::newHorizonsNoQuarter));
	bonus.stacking = NO_QUARTER;
	bonus.description.appendRawString("No Quarter");
	return bonus;
}

inline bool isNoQuarterRetaliationBonus(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::ONE_BATTLE
		&& bonus->type == BonusType::NO_RETALIATION && bonus->val == 1
		&& bonus->source == BonusSource::OTHER
		&& bonus->sid == BonusSourceID(BonusCustomSource::newHorizonsNoQuarter);
}

inline bool isNoQuarterMoralePenalty(const Bonus * bonus)
{
	return bonus && bonus->duration == BonusDuration::STACK_ACTIVATION
		&& bonus->type == BonusType::MORALE && bonus->val == NO_QUARTER_MORALE_PENALTY
		&& bonus->source == BonusSource::OTHER
		&& bonus->sid == BonusSourceID(BonusCustomSource::newHorizonsNoQuarter);
}

inline bool isNoQuarterBonus(const Bonus * bonus)
{
	return isNoQuarterRetaliationBonus(bonus) || isNoQuarterMoralePenalty(bonus);
}

/// Strictly below one quarter without multiplying potentially large HP values.
inline bool belowNoQuarterThreshold(int64_t currentHealth, int64_t maximumHealth)
{
	if(currentHealth <= 0 || maximumHealth <= 0)
		return false;
	const int64_t quarter = maximumHealth / 4;
	return currentHealth < quarter || (currentHealth == quarter && maximumHealth % 4 != 0);
}

inline Bonus vengeanceRetaliationBonus()
{
	Bonus bonus(BonusDuration::N_TURNS, BonusType::ADDITIONAL_RETALIATION,
		BonusSource::HERO_COMMAND, 1,
		BonusSourceID(BonusCustomSource(static_cast<int32_t>(HeroCommand::RIPOSTE))));
	bonus.turnsRemain = 1;
	return bonus;
}

inline bool hasVengeanceRetaliationBonus(const IBonusBearer * unit)
{
	if(!unit)
		return false;
	const auto matching = unit->getAllBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus && bonus->duration == BonusDuration::N_TURNS && bonus->turnsRemain > 0
			&& bonus->source == BonusSource::HERO_COMMAND
			&& bonus->sid == BonusSourceID(BonusCustomSource(static_cast<int32_t>(HeroCommand::RIPOSTE)))
			&& bonus->type == BonusType::ADDITIONAL_RETALIATION && bonus->val == 1;
	}));
	return matching && !matching->empty();
}
}
