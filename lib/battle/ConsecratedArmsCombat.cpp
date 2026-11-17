/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsCombat.h"
#include "BattleAttackInfo.h"
#include "Unit.h"
#include "../combatScripts/IDamageCalculatorScript.h"
#include <algorithm>
#include <limits>

namespace consecratedArms
{
namespace
{
int64_t multiplyBP(int64_t amount, int basisPoints)
{
	amount = std::max<int64_t>(0, amount);
	basisPoints = std::clamp(basisPoints, 0, 10000);
	// Split the quotient to avoid overflow on valid large stacks.
	return amount / 10000 * basisPoints + amount % 10000 * basisPoints / 10000;
}
}

int fearlessAdvanceChance(const battle::Unit & attacker)
{
	const auto prayer = activePrayer(attacker, Prayer::DAUNTLESS_ASSAULT);
	return prayer && prayer->policy.has(Perk::FEARLESS_ADVANCE) ? 20 : 0;
}

void adjustRetaliationDamage(const BattleAttackInfo & attack, DamageEstimation & damage)
{
	if(!attack.retaliation || attack.shooting || attack.bracePreemptive
		|| attack.preemptiveDamagePercent > 0 || !attack.defender)
		return;
	const int reduction = prayerValues(*attack.defender, Prayer::DAUNTLESS_ASSAULT).retaliationReductionBP;
	if(!reduction)
		return;
	for(auto * bound : {&damage.damage.min, &damage.damage.max})
		*bound = multiplyBP(*bound, 10000 - reduction);
	const auto killedBy = [&attack](int64_t amount) -> int64_t
	{
		const auto integrity = attack.defender->getPhantomIntegrity();
		if(integrity > 0)
			return amount >= integrity ? attack.defender->getCount() : 0;
		const auto first = attack.defender->getFirstHPleft();
		return amount < first ? 0 : std::min<int64_t>(attack.defender->getCount(),
			1 + (amount - first) / std::max<uint32_t>(1, attack.defender->getMaxHealth()));
	};
	damage.kills.min = killedBy(damage.damage.min);
	damage.kills.max = killedBy(damage.damage.max);
}

int64_t judgmentDamage(int64_t bodyDamage, int judgmentBP, bool righteous, bool acted)
{
	bodyDamage = std::max<int64_t>(0, bodyDamage);
	const int numerator = std::clamp(judgmentBP, 0, 2500) * (righteous && acted ? 3 : 2);
	return bodyDamage / 20000 * numerator + bodyDamage % 20000 * numerator / 20000;
}

JudgmentPotency judgmentPotency(const battle::Unit & source)
{
	const auto prayers = activePrayers(source);
	const auto & judgment = prayers[static_cast<size_t>(Prayer::HEAVENS_JUDGMENT)];
	if(!judgment)
		return {};
	int count = 0;
	for(const auto & prayer : prayers)
		count += prayer.has_value();
	return {values(judgment->potencyFaith, judgment->policy, count).judgmentBP,
		judgment->policy.has(Perk::RIGHTEOUS_SENTENCE)};
}

int64_t judgmentRawDamage(const JudgmentPotency & potency, const battle::Unit & target, int64_t actualBodyDamage)
{
	return judgmentDamage(actualBodyDamage, potency.basisPoints, potency.righteousSentence,
		target.hasCompletedActionThisRound());
}

int64_t casualtyLimit(int64_t currentCount, int ceilingBP)
{
	if(currentCount <= 0)
		return 0;
	ceilingBP = std::clamp(ceilingBP, 0, 10000);
	const int64_t whole = currentCount / 10000 * ceilingBP;
	const int64_t fraction = currentCount % 10000 * ceilingBP;
	return whole + fraction / 10000 + (fraction % 10000 != 0);
}
}
