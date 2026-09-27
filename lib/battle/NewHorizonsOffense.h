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
inline constexpr int CLEAVE_DAMAGE_PERCENT = 50;

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
