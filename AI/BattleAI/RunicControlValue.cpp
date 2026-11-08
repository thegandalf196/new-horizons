/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RunicControlValue.h"
#include "AttackPossibility.h"
#include "CreatureActivationValue.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/NewHorizonsBerserk.h"
namespace runicReflectionAI
{
float berserkValue(const battle::Unit & before, StackWithBonuses & after, PlayerColor reference,
	const std::shared_ptr<HypotheticBattle> & board, Weights weights)
{
	if(before.hasBonusOfType(BonusType::ATTACKS_NEAREST_CREATURE) || !after.canMove()) return 0;
	if(const auto speed = newHorizonsBerserk::forcedActivationSpeedBonus(*board, &after)) after.addUnitBonus({*speed});
	const auto forced = board->getBerserkForcedActions(&after);
	DamageCache baseline;
	const auto ordinary = creatureActivationValue::expectedTargetActivationValue(&before, baseline, board, true);
	const auto ordinarySigned = board->battleGetOwner(&before) == reference ? ordinary : -ordinary;
	double forcedValue = 0;
	for(const auto & action : forced)
	{
		if(!action.target || (action.type != EActionType::WALK_AND_ATTACK && action.type != EActionType::SHOOT)) continue;
		const bool shooting = action.type == EActionType::SHOOT;
		const auto attacks = AttackPossibility::getAttackCount(after, shooting, *board);
		auto target = action.target->acquireState();
		DamageCache damage;
		for(int index = 0; index < attacks && target->alive(); ++index)
		{
			BattleAttackInfo attack(&after, target.get(), 0, shooting);
			attack.attackerPos = action.position;
			attack.defenderPos = target->getPosition();
			const auto raw = board->battleExpectedLuckDamage(attack);
			const auto applied = battleAIProjectDamage(target.get(), raw, battleAIDamageProvenance(&after, attack.physicalDamage));
			const auto value = AttackPossibility::calculateDamageReduce(&after, target.get(), applied.healthLoss, damage, board);
			forcedValue += board->battleGetOwner(action.target) == reference ? -value : value;
			auto amount = raw;
			target->damage(amount, false, battleAIDamageProvenance(&after, attack.physicalDamage));
		}
	}
	// Walking/no-action outcomes remain in the denominator. Native target ties
	// are averaged; this estimate does not choose the most favorable victim.
	const float delta = (forced.empty() ? 0 : static_cast<float>(forcedValue / forced.size())) - ordinarySigned;
	return delta >= 0 ? delta * weights.positive : delta * weights.negative * weights.collateral;
}
}
