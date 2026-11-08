/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPassiveSpecialties.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "NewHorizonsCombatSkills.h"
#include "Unit.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusSelector.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/Limiters.h"
#include "../bonuses/Updaters.h"

namespace newHorizonsPassiveSpecialties
{
int casualtyPercent(int32_t entry, int32_t currentOriginalCount)
{
	if(entry <= 0)
		return 0;
	const int64_t lost = std::clamp<int64_t>(static_cast<int64_t>(entry) - currentOriginalCount, 0, entry);
	return static_cast<int>(100 * lost / entry);
}
int boneFormationDefense(int32_t count)
{
	const int64_t size = std::max(0, count);
	return static_cast<int>(std::min<int64_t>(size, 100) / 10
		+ std::clamp<int64_t>(size - 100, 0, 200) / 20 + std::max<int64_t>(0, size - 300) / 50);
}
bool magicalDamageImmune(const battle::Unit & unit)
{
	return unit.hasBonusOfType(BonusType::MAGICAL_DAMAGE_IMMUNITY);
}
namespace
{
bool spatialLimiter(const std::shared_ptr<const ILimiter> & limiter, int depth = 0)
{
	if(!limiter || depth > 8) return false;
	if(dynamic_cast<const UnitAdjacentLimiter *>(limiter.get())) return true;
	if(const auto * aggregate = dynamic_cast<const AggregateLimiter *>(limiter.get()))
		for(const auto & child : aggregate->limiters)
			if(spatialLimiter(child, depth + 1)) return true;
	return false;
}
}
bool isDynamicPassiveBonus(const Bonus & bonus)
{
	if(bonus.source != BonusSource::HERO_SPECIAL) return false;
	if(bonus.type == BonusType::PHYSICAL_DEFENSE)
		return (!bonus.updater && bonus.val == -11)
			|| dynamic_cast<const TimesStackSizeUpdater *>(bonus.updater.get());
	return (bonus.type == BonusType::GENERAL_ATTACK_REDUCTION || bonus.type == BonusType::MORALE
		|| bonus.type == BonusType::LUCK || bonus.type == BonusType::CREATURE_ATTACK_TARGET_PROTECTION)
		&& spatialLimiter(bonus.limiter);
}
bool hasDynamicDamagePassive(const battle::Unit & unit)
{
	return unit.hasBonusOfType(BonusType::CASUALTY_DAMAGE_PERCENTAGE)
		|| unit.hasBonusOfType(BonusType::MAGICAL_DAMAGE_IMMUNITY)
		|| unit.hasBonusOfType(BonusType::CREATURE_ATTACK_TARGET_PROTECTION)
		|| unit.hasBonusOfType(BonusType::DYNAMIC_CREATURE_DAMAGE);
}
int casualtyDamagePercent(const BattleAttackInfo & attack)
{
	if(!attack.physicalDamage || attack.shooting || attack.secondaryAttack
		|| attack.bracePreemptive || attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0
		|| !attack.attacker || !attack.defender || !attack.attacker->alive() || !attack.defender->alive()
		|| attack.attacker->isGhost() || attack.defender->isGhost())
		return 0;
	int result = 0;
	const auto bonuses = attack.attacker->getBonuses(Selector::type()(BonusType::CASUALTY_DAMAGE_PERCENTAGE));
	for(const auto & bonus : *bonuses)
	{
		if(bonus->subtype.getNum() != 0 && bonus->subtype.getNum() != 1)
			continue;
		const bool targetDeficit = bonus->subtype.getNum() == 1;
		if(targetDeficit && !attack.defender->isLiving())
			continue;
		const auto * subject = targetDeficit ? attack.defender : attack.attacker;
		result = std::max(result, std::min(std::clamp(bonus->val, 0, 100),
			casualtyPercent(subject->unitBaseAmount(), subject->getCurrentOriginalCreatureCount())));
	}
	return result;
}
bool permitsCreatureAttackTarget(const CBattleInfoCallback & callback,
	const battle::Unit * attacker, const battle::Unit * target)
{
	return !attacker || !target || !newHorizonsCombatSkills::isOrdinaryCreatureAttacker(attacker)
		|| !callback.battleMatchActionController(attacker, target, false)
		|| !target->hasBonusOfType(BonusType::CREATURE_ATTACK_TARGET_PROTECTION);
}
}
