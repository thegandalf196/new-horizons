/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PrismaticVanguardProjection.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/battle/NewHorizonsPrismaticVanguard.h"
namespace prismaticVanguardAI
{
void entry(HypotheticBattle & battle, StackWithBonuses & unit)
{
	const auto policy = battle.battleGetVisibleHeroPolicy(unit.unitSide());
	const auto * hero = policy.kind() == VisibleHeroPolicy::Knowledge::VISIBLE_HERO ? policy.legacyHero() : nullptr;
	if(const auto entry = newHorizonsPrismaticVanguard::entryBonus(hero, unit.creatureId()))
		battle.addUnitBonus(unit.unitId(), {*entry});
}
void begin(HypotheticBattle & battle, StackWithBonuses & unit, BattleUnitTurnReason reason)
{
	const auto plan = newHorizonsPrismaticVanguard::activationPlan(battle, unit, reason);
	if(!plan.remove.empty())
		unit.removeUnitBonus(CSelector(newHorizonsPrismaticVanguard::isEffect));
	if(!plan.add.empty())
		battle.addUnitBonus(unit.unitId(), plan.add);
}
void completeWait(StackWithBonuses & unit)
{
	const CSelector active([](const Bonus * bonus)
	{
		return newHorizonsPrismaticVanguard::isEffect(bonus) && bonus->duration == BonusDuration::STACK_ACTIVATION;
	});
	if(unit.hasBonus(active))
		unit.removeUnitBonus(active);
}
}
