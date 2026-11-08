/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMaintenanceSpecialties.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/Limiters.h"
#include <stdexcept>
#include <typeinfo>
namespace newHorizonsMaintenanceSpecialties
{
// Current specialties grant no maintenance healing or Defend retaliation budget.
int woundHealing(const CBattleInfoCallback &, const battle::Unit &, Trigger)
{
	return 0;
}
int activationHealing(const CBattleInfoCallback &, const battle::Unit &, BattleUnitTurnReason)
{
	return 0;
}
bool isDefendRetaliationBonus(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != "new-horizons:ordinary-creature-specialty:core:krellion")
		return false;
	const auto * family = dynamic_cast<const CCreatureTypeLimiter *>(bonus->limiter.get());
	if(bonus->source != BonusSource::HERO_SPECIAL
		|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:krellion")))
		|| bonus->type != BonusType::ADDITIONAL_RETALIATION || bonus->duration != BonusDuration::PERMANENT
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->valType != BonusValueType::BASE_NUMBER
		|| bonus->subtype != BonusSubtypeID() || bonus->targetSourceType != BonusSource::OTHER
		|| bonus->turnsRemain != 0 || bonus->appliedByEnemy
		|| !family || typeid(*family) != typeid(CCreatureTypeLimiter)
		|| family->creatureID != CreatureID(CreatureID::decode("core:ogre")) || !family->includeUpgrades
		|| (bonus->val != 0 && bonus->val != 1) || bonus->parameters
		|| bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->statusIdentity.empty() || !bonus->statusTags.empty())
		throw std::runtime_error("Invalid Ogre Bulwark specialty provenance");
	return true;
}
int defendRetaliations(const battle::Unit &)
{
	return 0;
}
}
