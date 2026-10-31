/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCoordinatedMarks.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusParameters.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <stdexcept>
namespace newHorizonsCoordinatedMarks
{
namespace
{
constexpr auto consecrated = "new-horizons:ordinary-creature-specialty:core:ingham:consecratedTarget";
constexpr auto feathered = "new-horizons:ordinary-creature-specialty:core:lorelei:featheredFeint";
bool isConsecrated(const Bonus & bonus) { return bonus.stacking == consecrated; }
const char * heroIdentity(bool shot) { return shot ? "core:ingham" : "core:lorelei"; }
const char * statusIdentity(bool shot) { return shot ? "consecratedTarget" : "featheredFeint"; }
bool family(const battle::Unit & unit, const char * key)
{
	const auto base = CreatureID(CreatureID::decode(key));
	return unit.unitType() && (unit.creatureId() == base || base.toCreature()->isMyDirectOrIndirectUpgrade(unit.unitType()));
}
bool primaryPhysical(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return attack.attacker && attack.defender && attack.physicalDamage && !attack.secondaryAttack
		&& !attack.bracePreemptive && attack.preemptiveDamagePercent == 0 && attack.cleaveDamagePercent == 0
		&& newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		&& battle.battleGetOwner(attack.attacker) != battle.battleGetOwner(attack.defender);
}
int coefficient(const CBattleInfoCallback & battle, const battle::Unit & actor, bool shot)
{
	return newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, actor, heroIdentity(shot));
}
std::shared_ptr<Bonus> marker(const CBattleInfoCallback & battle, const battle::Unit & actor, bool shot)
{
	const auto side = battle.playerToSide(battle.battleGetOwner(&actor));
	const int value = coefficient(battle, actor, shot);
	if(value <= 0 || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| actor.unitId() > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()))
		return nullptr;
	auto result = std::make_shared<Bonus>(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::HERO_SPECIAL,
		value, BonusSourceID(HeroTypeID(HeroTypeID::decode(heroIdentity(shot)))), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	result->turnsRemain = 1;
	result->stacking = shot ? consecrated : feathered;
	result->statusIdentity = statusIdentity(shot);
	result->statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	result->parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{static_cast<int32_t>(side), static_cast<int32_t>(actor.unitId())});
	result->description.appendRawString(shot
		? "Consecrated Target: the next allied primary melee strike ignores " + std::to_string(value) + "% Creature Defense this round."
		: "Feathered Feint: the next different allied primary physical attacker ignores " + std::to_string(value) + "% of physical protection this round.");
	return result;
}
bool applies(const CBattleInfoCallback & battle, const BattleAttackInfo & attack, const Bonus & bonus)
{
	if(!isMark(&bonus) || bonus.turnsRemain <= 0)
		return false;
	const auto & provenance = bonus.parameters->toVector();
	return provenance[0] == static_cast<int32_t>(battle.playerToSide(battle.battleGetOwner(attack.attacker)))
		&& (isConsecrated(bonus) ? !attack.shooting : static_cast<uint32_t>(provenance[1]) != attack.attacker->unitId());
}

}
bool isMark(const Bonus * bonus)
{
	if(!bonus || (bonus->stacking != consecrated && bonus->stacking != feathered))
		return false;
	const bool shot = isConsecrated(*bonus);
	bool validParameters = false;
	if(bonus->parameters)
		try
		{
			const auto & values = bonus->parameters->toVector();
			validParameters = values.size() == 2 && (values[0] == 0 || values[0] == 1) && values[1] >= 0;
		}
		catch(const std::exception &) {}
	if(bonus->source != BonusSource::HERO_SPECIAL || bonus->type != BonusType::NONE
		|| bonus->sid != BonusSourceID(HeroTypeID(HeroTypeID::decode(heroIdentity(shot))))
		|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain < 0 || bonus->turnsRemain > 1
		|| bonus->val <= 0 || bonus->val > 100 || bonus->valType != BonusValueType::BASE_NUMBER
		|| bonus->subtype != BonusSubtypeID() || bonus->targetSourceType != BonusSource::OTHER
		|| bonus->effectRange != BonusLimitEffect::NO_LIMIT || bonus->appliedByEnemy || bonus->hidden
		|| bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE || bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusIdentity != statusIdentity(shot) || !validParameters
		|| bonus->statusTags.size() != 2 || bonus->statusTags[0] != BonusStatusTag::DEBUFF
		|| bonus->statusTags[1] != BonusStatusTag::NON_TRANSFERABLE
		|| bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater || !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid coordinated specialty mark provenance");
	return true;
}
bool hasMark(const battle::Unit & target)
{
	return target.hasBonusOfType(BonusType::NONE) && target.hasBonus(CSelector(isMark));
}
Penetration penetration(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	Penetration result;
	if(!primaryPhysical(battle, attack) || !attack.defender->hasBonusOfType(BonusType::NONE))
		return result;
	for(const auto & bonus : *attack.defender->getBonusesOfType(BonusType::NONE))
		if(applies(battle, attack, *bonus))
		{
			auto & percent = isConsecrated(*bonus) ? result.creatureDefensePercent : result.physicalProtectionPercent;
			percent = std::max(percent, bonus->val);
		}
	return result;
}
int creatureDefenseIgnorePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return penetration(battle, attack).creatureDefensePercent;
}
int physicalProtectionIgnorePercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return penetration(battle, attack).physicalProtectionPercent;
}
std::vector<Bonus> consumed(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	std::vector<Bonus> result;
	if(!primaryPhysical(battle, attack) || !attack.defender->hasBonusOfType(BonusType::NONE))
		return result;
	for(const auto & bonus : *attack.defender->getBonusesOfType(BonusType::NONE))
		if(applies(battle, attack, *bonus))
			result.push_back(*bonus);
	return result;
}
std::shared_ptr<Bonus> consecratingShot(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!primaryPhysical(battle, attack) || !attack.shooting || attack.retaliation
		|| !family(*attack.attacker, "core:monk"))
		return nullptr;
	return marker(battle, *attack.attacker, true);
}
bool canMarkReturn(const CBattleInfoCallback & battle, const battle::Unit & actor)
{
	return family(actor, "core:harpy") && newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&actor)
		&& coefficient(battle, actor, false) > 0;
}
bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!primaryPhysical(battle, attack))
		return false;
	return hasMark(*attack.defender) || (!attack.shooting && hasMark(*attack.attacker)) || (attack.shooting && !attack.retaliation
		&& family(*attack.attacker, "core:monk") && coefficient(battle, *attack.attacker, true) > 0)
		|| (!attack.shooting && !attack.retaliation && canMarkReturn(battle, *attack.attacker));
}
newHorizonsOrdinaryStrike::EffectPlan prepareReturn(const CBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & victim)
{
	newHorizonsOrdinaryStrike::EffectPlan result;
	if(!actor.alive() || !victim.alive() || actor.isGhost() || victim.isGhost()
		|| actor.isTimeStopped() || victim.isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(&actor) || !family(actor, "core:harpy")
		|| battle.battleGetOwner(&actor) == battle.battleGetOwner(&victim))
		return result;
	const auto effect = marker(battle, actor, false);
	if(!effect)
		return result;
	result.target = victim.unitId();
	for(const auto & old : *victim.getBonusesOfType(BonusType::NONE))
		if(isMark(old.get()) && !isConsecrated(*old))
			result.remove.push_back(*old);
	result.add.push_back(*effect);
	return result;
}
newHorizonsOrdinaryStrike::EffectPlan completedReturn(const CBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & victim, bool actuallyReturned)
{
	return actuallyReturned ? prepareReturn(battle, actor, victim) : newHorizonsOrdinaryStrike::EffectPlan{};
}

}
