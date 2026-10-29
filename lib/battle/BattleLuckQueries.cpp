/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "CUnitState.h"
#include "IBattleState.h"
#include "DamageCalculationTiming.h"
#include "NewHorizonsCombatSkills.h"
#include "NewHorizonsForestAmbush.h"
#include "../GameLibrary.h"
#include "../IGameSettings.h"
namespace
{
LuckRollRules battleLuckRules(const IBattleInfo & battle)
{
	auto rules = battle.getLuckRollRules();
	if(rules.diceSize == 0)
	{
		const auto & settings = *LIBRARY->engineSettings();
		rules.goodChance = settings.getVector(EGameSettings::COMBAT_GOOD_LUCK_CHANCE);
		rules.badChance = settings.getVector(EGameSettings::COMBAT_BAD_LUCK_CHANCE);
		rules.diceSize = settings.getInteger(EGameSettings::COMBAT_LUCK_DICE_SIZE);
		rules.affectsAllTargets = settings.getBoolean(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS);
	}
	return rules;
}
}

int CBattleInfoCallback::battleGetAttackLuck(const battle::Unit * attacker, const battle::Unit * target,
	bool shooting, bool includeChanceOnlySerendipity) const
{
	if(!attacker || !getBattle())
		return 0;
	if(getBattle()->getRawFinalDirectPhysicalLuckPolicy()
		&& !newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attacker,
			!(shooting && attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))))
		return 0;
	const auto rules = battleLuckRules(*getBattle());
	const int maximum = static_cast<int>(rules.goodChance.size());
	const int minimum = -static_cast<int>(rules.badChance.size());
	const auto cap = [attacker](int luck)
	{
		return attacker->hasBonusOfType(BonusType::MAXIMUM_LUCK)
			? std::min(luck, attacker->valOfBonuses(BonusType::MAXIMUM_LUCK)) : luck;
	};
	if(attacker->hasBonusOfType(BonusType::MAX_LUCK))
		return cap(maximum);
	if(attacker->hasBonusOfType(BonusType::NO_LUCK))
		return 0;
	const int baseLuck = static_cast<int>(std::clamp<int64_t>(
		static_cast<int64_t>(attacker->valOfBonuses(BonusType::LUCK))
			+ attacker->necromancyCombatPassives().luckBonus, minimum, maximum));
	const auto side = playerToSide(battleGetOwner(attacker));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return cap(baseLuck);
	const auto mark = battleGetFocusFireState(side);
	const bool focused = shooting && target && battleIsFocusFireRecipient(attacker, side)
		&& battleIsFocusFireTargetActive(side) && mark && mark->targetUnitId == target->unitId();
	const auto & fortune = getBattle()->getSylvanLuckState(side);
	int luck = fortune.chanceLuck(baseLuck, attacker->unitId(), focused)
		+ newHorizonsForestAmbush::shotLuck(*this, attacker, shooting);
	const auto controllerSide = playerToSide(battleGetActionController(attacker));
	if((controllerSide == BattleSide::ATTACKER || controllerSide == BattleSide::DEFENDER)
		&& getBattle()->getLuckSerendipityState(controllerSide).availableAt(battleGetRound())
		&& !battleTacticDist() && attacker->alive() && !attacker->isTimeStopped()
		&& newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attacker,
			!(shooting && attacker->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))))
		luck += 2;
	if(!includeChanceOnlySerendipity && fortune.serendipity
		&& !fortune.positiveLuckUnits.contains(attacker->unitId()))
		--luck;
	return cap(std::clamp(luck, minimum, maximum));
}

int64_t CBattleInfoCallback::battleExpectedLuckDamage(const BattleAttackInfo & attack) const
{
	const auto average = [](const DamageRange & range) { return range.min + (range.max - range.min) / 2; };
	damageCalculationTiming::noteCalculation(damageCalculationTiming::Calculation::INITIAL);
	const auto normal = average(calculateDmgRange(attack).damage);
	const auto side = playerToSide(battleGetOwner(attack.attacker));
	if(attack.luckyStrike || attack.unluckyStrike)
		return normal;
	if(battleCanTriggerPerfectFortune(attack))
	{
		auto guaranteed = attack;
		guaranteed.luckyStrike = true;
		guaranteed.unluckyStrike = false;
		damageCalculationTiming::noteCalculation(damageCalculationTiming::Calculation::LUCKY_SECOND);
		return average(calculateDmgRange(guaranteed).damage);
	}
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return normal;
	const auto fortune = getBattle()->getSylvanLuckState(side);
	const int luck = battleGetAttackLuck(attack.attacker, attack.defender, attack.shooting);
	if(luck == 0)
		return normal;
	if(luck < 0 && fortune.canIgnoreNegativeLuck(
		newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(attack.attacker, attack.physicalDamage)))
		return normal;
	const auto rules = battleLuckRules(*getBattle());
	if(luck > 0 && attack.secondaryAttack && !rules.affectsAllTargets)
		return normal;
	const auto & chances = luck > 0 ? rules.goodChance : rules.badChance;
	const auto dice = rules.diceSize;
	if(chances.empty() || dice <= 0)
		return normal;
	double chance = std::clamp(static_cast<double>(chances[std::min<size_t>(std::abs(luck), chances.size()) - 1]) / dice, 0.0, 1.0);
	// Forecast this attack's first adverse result. Deterministic outcomes do not
	// spend an allowance; positive Luck is not an adverse result for this army.
	if(luck < 0 && chance > 0.0 && chance < 1.0
		&& getBattle()->getAdverseCombatRerollState(side).available())
		chance *= chance;
	auto rolled = attack;
	rolled.luckyStrike = luck > 0;
	rolled.unluckyStrike = luck < 0;
	damageCalculationTiming::noteCalculation(luck > 0 ? damageCalculationTiming::Calculation::LUCKY_SECOND
		: damageCalculationTiming::Calculation::UNLUCKY_SECOND);
	return static_cast<int64_t>(normal * (1.0 - chance) + average(calculateDmgRange(rolled).damage) * chance);
}
