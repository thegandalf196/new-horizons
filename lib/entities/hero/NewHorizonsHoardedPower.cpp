/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsHoardedPower.h"
#include "NewHorizonsRuleValidation.h"
#include "NewHorizonsRecoverySpecialties.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
#include <limits>
#include "../../battle/SideInBattle.h"

namespace newHorizonsHeroes
{
std::optional<HoardedPowerRules> hoardedPowerRules(const JsonNode & development)
{
	const auto & rows = development["revisedSpecialties"];
	if(!rows.isStruct() || !rows.Struct().contains("hoardedPower"))
		return std::nullopt;
	const auto & row = rows["hoardedPower"];
	detail::fields(row, {"baseCapPercent", "capPercentPerLevel", "maximumCapPercent"});
	detail::require(detail::integer(row["baseCapPercent"], 0, 100), "Hoarded Power base cap");
	detail::require(detail::integer(row["capPercentPerLevel"], 0, 100), "Hoarded Power cap scaling");
	detail::require(detail::integer(row["maximumCapPercent"], row["baseCapPercent"].Integer(), 100), "Hoarded Power maximum cap");
	return HoardedPowerRules{static_cast<int>(row["baseCapPercent"].Integer()),
		static_cast<int>(row["capPercentPerLevel"].Integer()), static_cast<int>(row["maximumCapPercent"].Integer())};
}

int32_t hoardedPowerCap(const HoardedPowerRules & rules, int level, int32_t maximumMana)
{
	const int64_t percent = std::min<int64_t>(rules.maximumCapPercent,
		int64_t{rules.baseCapPercent} + int64_t{rules.capPercentPerLevel} * std::max(0, level - 1));
	return static_cast<int32_t>(std::max<int64_t>(0, int64_t{maximumMana} * percent / 100));
}

std::optional<int32_t> hoardedPowerCap(const CGHeroInstance & hero)
{
	if(!hero.getHeroType() || hero.getHeroType()->getJsonKey() != "core:nagash")
		return std::nullopt;
	const auto rules = hoardedPowerRules(hero.getPrimaryGrowthRules());
	return rules ? std::optional<int32_t>(hoardedPowerCap(*rules, hero.level, hero.manaLimit())) : std::nullopt;
}

bool restoreFixedSpellPoints(CGHeroInstance & hero, int32_t amount)
{
	if(amount < 0)
		return false;
	const auto cap = hoardedPowerCap(hero);
	const auto room = std::max<int64_t>(0, int64_t{hero.manaLimit()} - hero.getNormalSpellPoints());
	if(!hero.restoreNormalSpellPoints(amount))
		return false;
	if(cap && amount > room)
		return hero.grantBufferSpellPoints(static_cast<int32_t>(amount - room));
	return true;
}

void reconcileHoardedPowerBuffer(CGHeroInstance & hero, SideInBattle * side)
{
	const auto cap = hoardedPowerCap(hero);
	if(!cap || hero.getBufferSpellPoints() <= *cap)
		return;
	const auto lost = hero.getBufferSpellPoints() - *cap;
	if(!hero.removeBufferSpellPoints(lost))
		throw std::runtime_error("Invalid Hoarded Power Buffer cap reconciliation");
	if(side)
		side->temporaryBufferRemaining -= std::min(side->temporaryBufferRemaining, lost);
}

int64_t rewardBufferGain(const CGHeroInstance & hero, int64_t normalBeforeRecovery,
	int32_t fixedRecovery, int32_t explicitGrant, int64_t bufferSpent)
{
	const auto cap = hoardedPowerCap(hero);
	if(!cap)
		return explicitGrant;
	const auto room = std::max<int64_t>(0, int64_t{hero.manaLimit()} - normalBeforeRecovery);
	const auto overflow = std::max<int64_t>(0, int64_t{fixedRecovery} - room);
	return std::min(overflow + explicitGrant,
		std::max<int64_t>(0, int64_t{*cap} - hero.getBufferSpellPoints() + bufferSpent));
}

int32_t dailyHoardedPower(const CGHeroInstance & hero, bool completedDay)
{
	const auto cap = hoardedPowerCap(hero);
	if(!cap || (hero.getVisitedTown() && hero.getVisitedTown()->hasBuilt(BuildingID::MAGES_GUILD_1)))
		return 0;
	const int64_t recovery = std::max<int64_t>(hero.manaRegain(), enhancedMysticismRecovery(hero, completedDay));
	const int64_t room = std::max<int64_t>(0, int64_t{hero.manaLimit()} - hero.getNormalSpellPoints());
	return static_cast<int32_t>(std::min(std::max<int64_t>(0, recovery - room),
		std::max<int64_t>(0, int64_t{*cap} - hero.getBufferSpellPoints())));
}

int32_t blackHarvestRecoveryLimit(const CGHeroInstance & hero, int32_t temporaryBufferRemaining)
{
	const auto cap = hoardedPowerCap(hero);
	const auto remaining = std::clamp(temporaryBufferRemaining, 0, hero.getBufferSpellPoints());
	const int64_t extra = cap ? std::max<int64_t>(0, int64_t{*cap} - hero.getBufferSpellPoints() + remaining) : 0;
	return static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(), int64_t{hero.manaLimit()} + extra));
}
}
