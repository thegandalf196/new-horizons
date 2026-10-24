/*
 * HeroManaRegeneration.cpp, part of VCMI / New Horizons
 * Authors: listed in AUTHORS.h
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CGHeroInstance.h"
#include "CGTownInstance.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsRecoverySpecialties.h"
#include "../bonuses/BonusList.h"
si32 CGHeroInstance::manaRegain() const
{
	int percentageRegeneration = valOfBonuses(BonusType::MANA_PERCENTAGE_REGENERATION);
	const int64_t regeneratedByPercentage = static_cast<int64_t>(manaLimit()) * percentageRegeneration / 100;
	// Keep the graph's eligibility and duplicate/stacking rules, without changing
	// shared artifact definitions. Sum converted tiers widely before final clamp.
	BonusList otherRecovery;
	int64_t artifactRecovery = 0;
	const auto recoveryBonuses = getAllBonuses(Selector::type()(BonusType::MANA_REGENERATION));
	for(const auto & bonus : *recoveryBonuses)
	{
		const auto replacement = bonus->source == BonusSource::ARTIFACT && bonus->valType == BonusValueType::BASE_NUMBER
			? newHorizonsHeroes::capabilityArtifactManaRegeneration(capabilityRules, bonus->sid.as<ArtifactID>(), manaLimit())
			: std::nullopt;
		if(replacement)
			artifactRecovery += *replacement;
		else
			otherRecovery.push_back(bonus);
	}
	const int64_t regeneratedByValue = artifactRecovery + otherRecovery.totalValue();
	int64_t regeneration = std::max(regeneratedByValue, regeneratedByPercentage);
	if(hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.mysticism"))
		regeneration = std::max(regeneration, newHorizonsHeroes::enhancedMysticismRecovery(*this, false));

	return static_cast<si32>(std::clamp<int64_t>(regeneration,
		std::numeric_limits<si32>::min(), std::numeric_limits<si32>::max()));
}

si32 CGHeroInstance::getManaNewTurn(bool completedDay, std::optional<int> previousMovementLimit) const
{
	const int32_t normal = getNormalSpellPoints();
	const int32_t maximumMana = manaLimit();
	if(getVisitedTown() && getVisitedTown()->hasBuilt(BuildingID::MAGES_GUILD_1))
	{
		//if hero starts turn in town with mage guild - restore Normal to capacity
		return std::max(normal, maximumMana);
	}
	int64_t regeneration = manaRegain();
	const auto mysticism = newHorizonsHeroes::enhancedMysticismRecovery(*this, completedDay);
	if(mysticism > 0)
		regeneration = std::max(regeneration, mysticism);
	if(completedDay && hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.meditation"))
	{
		const int movementLimit = previousMovementLimit ? *previousMovementLimit : movementPointsLimit();
		if(movementLimit > 0
			&& static_cast<int64_t>(movementPointsRemaining()) * 4 >= static_cast<int64_t>(movementLimit))
			regeneration += static_cast<int64_t>(maximumMana) * 15 / 100;
	}
	int64_t res = static_cast<int64_t>(normal) + regeneration;
	res = std::min<int64_t>(res, maximumMana);
	res = std::max<int64_t>(res, normal);
	res = std::max<int64_t>(res, 0);
	return static_cast<si32>(std::min<int64_t>(res, std::numeric_limits<si32>::max()));
}
