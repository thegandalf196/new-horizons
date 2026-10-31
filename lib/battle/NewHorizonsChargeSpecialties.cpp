/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsChargeSpecialties.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../GameConstants.h"
#include "../bonuses/BonusList.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include <stdexcept>
namespace newHorizonsChargeSpecialties
{
namespace
{
constexpr auto identity = "new-horizons:ordinary-creature-specialty:core:tyris:lanceFormation";
bool firstPrimary(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return attack.attacker && attack.defender && attack.attacker->alive() && attack.defender->alive()
		&& !attack.attacker->isGhost() && !attack.defender->isGhost()
		&& !attack.attacker->isTimeStopped() && !attack.defender->isTimeStopped()
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		&& attack.physicalDamage && !attack.shooting && !attack.retaliation && !attack.secondaryAttack
		&& !attack.bracePreemptive && attack.preemptiveDamagePercent == 0 && attack.cleaveDamagePercent == 0
		&& attack.ordinaryAttackIndex == 0
		&& battle.battleGetOwner(attack.attacker) != battle.battleGetOwner(attack.defender);
}
bool family(const battle::Unit & actor, const char * creature)
{
	const auto base = CreatureID(CreatureID::decode(creature));
	return actor.unitType() && (actor.creatureId() == base
		|| base.toCreature()->isMyDirectOrIndirectUpgrade(actor.unitType()));
}
int companion(const CBattleInfoCallback & battle, const battle::Unit & actor, const char * key, int maximum)
{
	const auto * hero = newHorizonsHeroes::ordinaryCreatureVisibleOwnerHero(battle, actor);
	if(!hero)
		throw std::runtime_error("Missing visible captured charge-specialty owner");
	const auto & value = hero->getPrimaryGrowthRules()["additionalSpecialties"]["ordinaryCreatureSpecialties"][key];
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < 1 || value.Integer() > maximum)
		throw std::runtime_error("Invalid captured charge-specialty companion coefficient");
	return static_cast<int>(value.Integer());
}
}
bool requiresChargeMovementPreview(const CBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & defender)
{
	const BattleAttackInfo ordinary(&actor, &defender, 0, false);
	if(!firstPrimary(battle, ordinary))
		return false;
	if(family(actor, "core:cavalier") && actor.hasBonusOfType(BonusType::JOUSTING)
		&& !defender.hasBonusOfType(BonusType::CHARGE_IMMUNITY))
		return newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, actor, "core:tyris") > 0;
	if(family(actor, "core:roc") && actor.hasBonusOfType(BonusType::FLYING))
		return newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, actor, "core:shiva") > 0;
	return false;
}
int defenseFromCharge(int actualHexes, int defensePerHex, int maximumDefense)
{
	if(actualHexes < 0 || actualHexes > GameConstants::BFIELD_SIZE || defensePerHex < 0 || defensePerHex > 100
		|| maximumDefense < 1 || maximumDefense > 100)
		throw std::invalid_argument("Invalid Lance Formation distance or coefficient");
	return std::min(maximumDefense, actualHexes * defensePerHex);
}
bool isLanceFormation(const Bonus * bonus)
{
	if(!bonus || (bonus->stacking != identity && bonus->statusIdentity != "lanceFormation"))
		return false;
	if(bonus->stacking != identity || bonus->statusIdentity != "lanceFormation"
		|| bonus->source != BonusSource::HERO_SPECIAL
		|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:tyris")))
		|| bonus->type != BonusType::PRIMARY_SKILL || bonus->subtype != BonusSubtypeID(PrimarySkill::DEFENSE)
		|| bonus->duration != BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION || bonus->turnsRemain != 0
		|| bonus->val <= 0 || bonus->val > 100 || bonus->valType != BonusValueType::ADDITIVE_VALUE
		|| bonus->targetSourceType != BonusSource::OTHER || bonus->effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus->appliedByEnemy || bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusTags != std::vector<BonusStatusTag>{BonusStatusTag::NON_TRANSFERABLE}
		|| bonus->parameters || bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid Lance Formation marker");
	return true;
}
newHorizonsOrdinaryStrike::EffectPlan lanceFormation(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	newHorizonsOrdinaryStrike::EffectPlan result;
	if(!firstPrimary(battle, attack) || attack.chargeDistance <= 0 || !family(*attack.attacker, "core:cavalier")
		|| !attack.attacker->hasBonusOfType(BonusType::JOUSTING)
		|| attack.defender->hasBonusOfType(BonusType::CHARGE_IMMUNITY))
		return result;
	const int perHex = newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, "core:tyris");
	if(perHex <= 0)
		return result;
	const int maximum = companion(battle, *attack.attacker, "lanceFormationMaximumDefense", 100);
	int amount = defenseFromCharge(attack.chargeDistance, perHex, maximum);
	for(const auto & existing : *attack.attacker->getUnstackedBonuses(CSelector(isLanceFormation)))
	{
		result.remove.push_back(*existing);
		amount = std::max(amount, existing->val);
	}
	Bonus bonus(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::PRIMARY_SKILL,
		BonusSource::HERO_SPECIAL, amount, BonusSourceID(HeroTypeID(HeroTypeID::decode("core:tyris"))),
		BonusSubtypeID(PrimarySkill::DEFENSE), BonusValueType::ADDITIVE_VALUE);
	bonus.stacking = identity;
	bonus.statusIdentity = "lanceFormation";
	bonus.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	bonus.description.appendRawString("Lance Formation: +" + std::to_string(amount)
		+ " Creature Defense until the next genuine Creature Activation");
	result.target = attack.attacker->unitId();
	result.add.push_back(std::move(bonus));
	return result;
}
int stormWingsDefenseIgnorePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!firstPrimary(battle, attack) || !family(*attack.attacker, "core:roc")
		|| !attack.attacker->hasBonusOfType(BonusType::FLYING))
		return 0;
	const int percent = newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, "core:shiva");
	if(percent <= 0)
		return 0;
	const int threshold = companion(battle, *attack.attacker, "stormWingsMinimumFlightDistance", GameConstants::BFIELD_SIZE);
	if(attack.voluntaryFlightDistance < 0 || attack.voluntaryFlightDistance > GameConstants::BFIELD_SIZE)
		throw std::runtime_error("Invalid attack-local voluntary-flight distance");
	return attack.voluntaryFlightDistance >= threshold && attack.voluntaryFlightDistance == attack.chargeDistance
		? percent : 0;
}
}
