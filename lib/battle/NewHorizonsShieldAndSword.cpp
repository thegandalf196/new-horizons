/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsShieldAndSword.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../CCreatureHandler.h"
#include <stdexcept>
namespace newHorizonsShieldAndSword
{
namespace
{
constexpr auto identity = "new-horizons:ordinary-creature-specialty:core:sorsha:shieldAndSword";
int reduction(const CBattleInfoCallback & battle, const battle::Unit & actor)
{
	const int percent = newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, actor, "core:sorsha");
	if(percent <= 0)
		return 0;
	const auto family = CreatureID(CreatureID::decode("core:swordsman"));
	if(!actor.unitType() || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&actor)
		|| (actor.creatureId() != family && !family.toCreature()->isMyDirectOrIndirectUpgrade(actor.unitType())))
		return 0;
	return percent;
}
}
bool isProtection(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != identity)
		return false;
	if(bonus->source != BonusSource::HERO_SPECIAL
		|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:sorsha")))
		|| bonus->type != BonusType::GENERAL_DAMAGE_REDUCTION
		|| bonus->subtype != BonusCustomSubtype::damageTypeAll
		|| bonus->duration != BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		|| bonus->valType != BonusValueType::BASE_NUMBER || bonus->val <= 0 || bonus->val > 100
		|| bonus->targetSourceType != BonusSource::OTHER || bonus->turnsRemain != 0
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->appliedByEnemy
		|| bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusIdentity != "shieldAndSword"
		|| bonus->statusTags.size() != 1
		|| bonus->statusTags[0] != BonusStatusTag::NON_TRANSFERABLE
		|| bonus->parameters || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid Shield and Sword specialty provenance");
	return true;
}
bool eligible(const CBattleInfoCallback & battle, const battle::Unit & actor)
{
	return reduction(battle, actor) > 0;
}
newHorizonsOrdinaryStrike::EffectPlan completion(const CBattleInfoCallback & battle,
	const battle::Unit & actor, bool completedPrimaryMelee)
{
	newHorizonsOrdinaryStrike::EffectPlan result;
	if(!completedPrimaryMelee || !actor.alive() || actor.isGhost() || actor.isTimeStopped())
		return result;
	int percent = reduction(battle, actor);
	if(percent <= 0)
		return result;
	const auto protections = actor.getUnstackedBonuses(CSelector(isProtection));
	for(const auto & existing : *protections)
	{
		result.remove.push_back(*existing);
		percent = std::max(percent, existing->val);
	}
	Bonus protection(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::GENERAL_DAMAGE_REDUCTION,
		BonusSource::HERO_SPECIAL, percent, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:sorsha"))),
		BonusCustomSubtype::damageTypeAll, BonusValueType::BASE_NUMBER);
	protection.stacking = identity;
	protection.statusIdentity = "shieldAndSword";
	protection.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	protection.description.appendRawString("Shield and Sword: " + std::to_string(percent)
		+ "% less physical damage until the next Creature Activation");
	result.target = actor.unitId();
	result.add.push_back(std::move(protection));
	return result;
}
}
