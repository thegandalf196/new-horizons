/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsGnollHarriers.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../entities/hero/CHero.h"
#include <stdexcept>
namespace newHorizonsGnollHarriers
{
namespace { constexpr auto identity = "new-horizons:latest-voluntary-movement"; }
bool isMovementHistory(const Bonus * bonus)
{
	if(!bonus || bonus->stacking != identity)
		return false;
	if(bonus->type != BonusType::NONE || bonus->subtype != BonusSubtypeID()
		|| bonus->source != BonusSource::OTHER || bonus->sid != BonusSourceID()
		|| bonus->duration != BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION
		|| bonus->valType != BonusValueType::BASE_NUMBER || bonus->val != 1
		|| bonus->targetSourceType != BonusSource::OTHER || bonus->turnsRemain != 0
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->appliedByEnemy
		|| bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusIdentity != "latestVoluntaryMovement" || bonus->statusTags != std::vector{BonusStatusTag::NON_TRANSFERABLE}
		|| bonus->parameters || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid latest voluntary movement provenance");
	return true;
}
bool trackingRequired(const CBattleInfoCallback & battle)
{
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto policy = battle.battleGetVisibleHeroPolicy(side);
		const auto * hero = policy.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO ? policy.legacyHero() : nullptr;
		if(hero && hero->getHeroType() && hero->getHeroType()->getJsonKey() == "core:drakon"
			&& newHorizonsHeroes::hasOrdinaryCreatureSpecialty(hero)
			&& hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"]["gnollHarriersDamagePercent"].Integer() > 0)
			return true;
	}
	return false;
}
std::optional<Bonus> movement(const CBattleInfoCallback & battle, const battle::Unit & unit,
	bool voluntary, BattleHex before, BattleHex after)
{
	if(!voluntary || !before.isValid() || !after.isValid() || before == after
		|| !unit.alive() || unit.isGhost() || unit.isTimeStopped() || !trackingRequired(battle)
		|| unit.hasBonus(CSelector(isMovementHistory)))
		return std::nullopt;
	Bonus result(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::NONE, BonusSource::OTHER,
		1, BonusSourceID(), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	result.stacking = identity;
	result.statusIdentity = "latestVoluntaryMovement";
	result.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	return result;
}
int damagePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender || !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost()
		|| attack.attacker->isTimeStopped() || attack.defender->isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| !attack.physicalDamage || attack.shooting || attack.secondaryAttack || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender)
		|| !attack.defender->hasBonus(CSelector(isMovementHistory)))
		return 0;
	const auto family = CreatureID(CreatureID::decode("core:gnoll"));
	if(!attack.attacker->unitType() || (attack.attacker->creatureId() != family
		&& !family.toCreature()->isMyDirectOrIndirectUpgrade(attack.attacker->unitType())))
		return 0;
	return newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, "core:drakon");
}
}
