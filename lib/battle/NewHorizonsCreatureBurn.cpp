/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsCreatureBurn.h"
#include "BodyHealthLoss.h"
#include "NewHorizonsMagicalAbilityDamage.h"
#include "NewHorizonsCombatSkills.h"
#include "BattleAttackInfo.h"
#include "BattleActivationPredicate.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../entities/hero/NewHorizonsOrdinaryCreatureSpecialties.h"
#include <limits>
#include <stdexcept>
namespace newHorizonsCreatureBurn
{
namespace
{
struct NativeIdentities
{
	HeroTypeID rashka() const { return HeroTypeID(HeroTypeID::decode("core:rashka")); }
	HeroTypeID ignissa() const { return HeroTypeID(HeroTypeID::decode("core:ignissa")); }
};
struct CapturedIdentities
{
	const IdentityFacts & values;
	void require() const
	{
		if(!values.rashka.hasValue() || !values.ignissa.hasValue() || values.rashka == values.ignissa)
			throw std::logic_error("Captured creature burn identities are incomplete");
	}
	HeroTypeID rashka() const { require(); return values.rashka; }
	HeroTypeID ignissa() const { require(); return values.ignissa; }
};
template<class Identities> bool burnHero(HeroTypeID hero, const Identities & identities)
{
	return hero == identities.rashka() || hero == identities.ignissa();
}
int coefficient(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender || attack.shooting || !attack.physicalDamage
		|| attack.secondaryAttack || attack.bracePreemptive || attack.preemptiveDamagePercent > 0
		|| attack.cleaveDamagePercent > 0 || !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost()
		|| attack.attacker->isTimeStopped() || attack.defender->isTimeStopped()
		|| !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attack.attacker)
		|| battle.battleGetOwner(attack.attacker) == battle.battleGetOwner(attack.defender)
		|| newHorizonsMagicalAbilityDamage::fireImmune(*attack.defender))
		return 0;
	for(const auto & [hero, creature] : {std::pair{"core:rashka", "core:efreet"},
		std::pair{"core:ignissa", "core:fireElemental"}})
	{
		if(std::string_view(hero) == "core:rashka" && !attack.retaliation)
			continue;
		const int value = newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, hero);
		const auto family = CreatureID(CreatureID::decode(creature));
		if(value > 0 && attack.attacker->unitType() && (attack.attacker->creatureId() == family
			|| family.toCreature()->isMyDirectOrIndirectUpgrade(attack.attacker->unitType())))
		{
			if(!battle.battleUsesNewHorizonsMultiplicativeMDR())
				throw std::runtime_error("Captured creature burn policy requires multiplicative magical damage reduction");
			return value;
		}
	}
	return 0;
}
}
template<class Identities> bool validateBurn(const Bonus * bonus, const Identities & identities)
{
	if(!bonus || (bonus->stacking != IDENTITY && bonus->statusIdentity != "ordinaryCreatureBurn"))
		return false;
	if(bonus->stacking != IDENTITY || bonus->statusIdentity != "ordinaryCreatureBurn"
		|| bonus->source != BonusSource::HERO_SPECIAL || !burnHero(bonus->sid.as<HeroTypeID>(), identities)
		|| bonus->type != BonusType::NONE || bonus->subtype != BonusSubtypeID()
		|| bonus->duration != BonusDuration::ONE_BATTLE || bonus->turnsRemain != 0
		|| bonus->val <= 0 || bonus->valType != BonusValueType::BASE_NUMBER
		|| bonus->targetSourceType != BonusSource::OTHER || bonus->effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus->appliedByEnemy || bonus->bonusOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->spellCasterOwner != PlayerColor::CANNOT_DETERMINE
		|| bonus->statusTags != std::vector<BonusStatusTag>{BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE}
		|| !bonus->parameters || bonus->parameters->toNumber() < 0
		|| bonus->limiter || bonus->propagator || bonus->updater || bonus->propagationUpdater
		|| !bonus->customIconPath.empty())
		throw std::runtime_error("Invalid ordinary creature burn provenance");
	return true;
}
IdentityFacts captureIdentityFacts()
{
	const NativeIdentities native;
	const IdentityFacts result{native.rashka(), native.ignissa()};
	CapturedIdentities{result}.require();
	return result;
}
bool isBurn(const Bonus * bonus) { return validateBurn(bonus, NativeIdentities{}); }
bool isBurnCaptured(const Bonus * bonus, const IdentityFacts & identities) { return validateBurn(bonus, CapturedIdentities{identities}); }

Bonus marker(HeroTypeID hero, uint32_t source, int32_t rawDamage)
{
	if(!burnHero(hero, NativeIdentities{}) || source > static_cast<uint32_t>(std::numeric_limits<int32_t>::max()) || rawDamage <= 0)
		throw std::invalid_argument("Invalid ordinary creature burn source or budget");
	Bonus result(BonusDuration::ONE_BATTLE, BonusType::NONE, BonusSource::HERO_SPECIAL,
		rawDamage, BonusSourceID(hero), BonusSubtypeID(), BonusValueType::BASE_NUMBER);
	result.stacking = IDENTITY;
	result.statusIdentity = "ordinaryCreatureBurn";
	result.statusTags = {BonusStatusTag::DEBUFF, BonusStatusTag::NON_TRANSFERABLE};
	result.parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(source));
	result.description.appendRawString("Burn: one delayed Fire damage tick at the next genuine Creature Activation");
	return result;
}
int64_t bodyHealthLoss(int64_t appliedDamage, int64_t temporaryHealthBefore)
{
	return battle::bodyHealthLoss(appliedDamage, temporaryHealthBefore);
}
int32_t damageBudget(int64_t actualBodyHealthLoss, int percent)
{
	if(actualBodyHealthLoss <= 0 || percent <= 0)
		return 0;
	if(percent > 100)
		throw std::invalid_argument("Invalid ordinary creature burn percentage");
	const int64_t scaled = actualBodyHealthLoss / 100 * percent + actualBodyHealthLoss % 100 * percent / 100;
	return static_cast<int32_t>(std::clamp<int64_t>(scaled, 1, std::numeric_limits<int32_t>::max()));
}
bool eligible(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return coefficient(battle, attack) > 0;
}
newHorizonsOrdinaryStrike::EffectPlan plan(const CBattleInfoCallback & battle,
	const BattleAttackInfo & attack, int64_t actualBodyHealthLoss)
{
	newHorizonsOrdinaryStrike::EffectPlan result;
	const auto budget = damageBudget(actualBodyHealthLoss, coefficient(battle, attack));
	if(budget <= 0)
		return result;
	const auto hero = HeroTypeID(HeroTypeID::decode(attack.retaliation
		&& newHorizonsHeroes::ordinaryCreatureSpecialtyValue(battle, *attack.attacker, "core:rashka") > 0
		? "core:rashka" : "core:ignissa"));
	Bonus next = marker(hero, attack.attacker->unitId(), budget);
	const auto existingBurns = attack.defender->getUnstackedBonuses(CSelector([](const Bonus * bonus) { return isBurn(bonus); }));
	for(const auto & existing : *existingBurns)
	{
		result.remove.push_back(*existing);
		// Equal budgets keep the previous application and its captured source.
		if(existing->val >= next.val)
			next = *existing;
	}
	result.target = attack.defender->unitId();
	result.add.push_back(std::move(next));
	return result;
}
template<class Classifier, class ActivationQuery, class FireQuery>
Tick activationTickWith(const battle::Unit & target, BattleUnitTurnReason reason, const Classifier & classify,
	const ActivationQuery & begins, const FireQuery & fireDamage)
{
	Tick result;
	if(!target.alive() || target.isGhost() || !begins()
		|| ((reason == BattleUnitTurnReason::TURN_QUEUE || reason == BattleUnitTurnReason::AUTOMATIC_ACTION) && target.waited()))
		return result;
	int32_t strongest = 0;
	const auto existingBurns = target.getUnstackedBonuses(CSelector(classify));
	for(const auto & existing : *existingBurns)
	{
		result.remove.push_back(*existing);
		if(existing->val > strongest)
		{
			strongest = existing->val;
			result.source = static_cast<uint32_t>(existing->parameters->toNumber());
		}
	}
	if(!result.remove.empty())
		result.damage = fireDamage(strongest);
	return result;
}

Tick activationTick(const CBattleInfoCallback & battle, const battle::Unit & target, BattleUnitTurnReason reason)
{
	return activationTickWith(target, reason, [](const Bonus * bonus) { return isBurn(bonus); },
		[&] { return battle.battleBeginsActivation(&target, reason); },
		[&](int32_t damage) { return newHorizonsMagicalAbilityDamage::adjustFireDamage(battle, target, damage); });
}
Tick activationTickCaptured(const CBattleInfoCallback & battle, const battle::Unit & target,
	BattleUnitTurnReason reason, const IdentityFacts & identities)
{
	return activationTickWith(target, reason, [&identities](const Bonus * bonus) { return isBurnCaptured(bonus, identities); },
		[&] { return battle.battleBeginsActivation(&target, reason); },
		[&](int32_t damage) { return newHorizonsMagicalAbilityDamage::adjustFireDamage(battle, target, damage); });
}
Tick activationTickCaptured(const battle::Unit & target, BattleUnitTurnReason reason,
	const IdentityFacts & identities, const battle::ActivationContext & activation,
	const newHorizonsMagicalAbilityDamage::FireDamageContext & fire)
{
	return activationTickWith(target, reason, [&identities](const Bonus * bonus) { return isBurnCaptured(bonus, identities); },
		[&] { return battle::beginsActivation(activation, &target, reason); },
		[&](int32_t damage) { return newHorizonsMagicalAbilityDamage::adjustFireDamage(fire, target, damage); });
}
}
