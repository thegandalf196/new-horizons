/*
 * ProjectedHeroCommandCoefficient.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HypotheticBattle.h"
#include "InitialExportBattleSubject.h"
#include "../../lib/battle/HeroCommandCoefficient.h"
#include "../../lib/bonuses/BonusList.h"
#include <stdexcept>

int HypotheticBattle::heroCommandCoefficient(const JsonNode & effect, const CGHeroInstance & hero,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent) const
{
	if(const auto owned = initialExportSubject())
	{
		owned->synchronizeValues(*this);
		return forwardHeroCommandCoefficient(*owned, effect, hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
	}
	return BattleProxy::heroCommandCoefficient(effect, hero, warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
}

int InitialExportBattleSubject::heroCommandCoefficient(const JsonNode & effect, const CGHeroInstance & hero,
	int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent) const
{
	std::optional<BattleSide> owner;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		if(getSideHero(side) == &hero)
		{
			if(owner)
				throw std::invalid_argument("Coefficient policy hero has ambiguous original side identity");
			owner = side;
		}
	if(!owner)
		throw std::invalid_argument("Coefficient requires its actual captured owner hero, not equal metadata");
	const auto all = owned->projectedHeroBonuses(*owner, false);
	auto primary = std::make_shared<BonusList>();
	all->getBonuses(*primary, Selector::type()(BonusType::PRIMARY_SKILL));
	return heroCommands::coefficient(effect, heroCommands::CoefficientInputs::projected(hero, primary),
		warcastingBonusPercent, divineMandateEfficiencyBonusPercent);
}
