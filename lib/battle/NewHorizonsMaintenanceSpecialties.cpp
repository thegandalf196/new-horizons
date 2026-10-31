/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMaintenanceSpecialties.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../entities/hero/CHero.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusSelector.h"
#include "../bonuses/Limiters.h"
#include <algorithm>
#include <stdexcept>
#include <typeinfo>
namespace newHorizonsMaintenanceSpecialties
{
int woundHealing(const CBattleInfoCallback & battle, const battle::Unit & unit, Trigger trigger)
{
	if(!unit.alive() || unit.isGhost() || unit.isTimeStopped())
		return 0;
	const auto * hero = battle.battleGetFightingHero(unit.unitSide());
	if(!hero || !hero->getHeroType() || !newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero))
		return 0;
	const bool defend = trigger == Trigger::VOLUNTARY_DEFEND;
	if(hero->getHeroType()->getJsonKey() != (defend ? "core:josephine" : "core:gerwulf"))
		return 0;
	const auto family = CreatureID(CreatureID::decode(defend ? "core:ironGolem" : "core:ballista"));
	if(unit.creatureId() != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(unit.creatureId().toCreature()))
		return 0;
	const auto & policy = hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"];
	const int budget = static_cast<int>(policy[defend ? "fieldReassemblyHealingHP" : "fieldMaintenanceHealingHP"].Integer());
	return static_cast<int>(std::min<int64_t>(budget,
		std::max<int64_t>(0, static_cast<int64_t>(unit.getMaxHealth()) - unit.getFirstHPleft())));
}
int activationHealing(const CBattleInfoCallback & battle, const battle::Unit & unit, BattleUnitTurnReason reason)
{
	if(!battle.battleBeginsActivation(&unit, reason)
		|| ((reason == BattleUnitTurnReason::TURN_QUEUE || reason == BattleUnitTurnReason::AUTOMATIC_ACTION)
			&& unit.waited()))
		return 0;
	return woundHealing(battle, unit, Trigger::ACTIVATION_START);
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
int defendRetaliations(const battle::Unit & unit)
{
	if(!unit.alive() || unit.isGhost() || !unit.defended())
		return 0;
	static const auto selector = Selector::type()(BonusType::ADDITIONAL_RETALIATION).And(
		CSelector(isDefendRetaliationBonus));
	const auto bonus = unit.getAllBonuses(selector, "newHorizonsOgreBulwark")->getFirst(Selector::all);
	return bonus ? bonus->val : 0;
}
}
