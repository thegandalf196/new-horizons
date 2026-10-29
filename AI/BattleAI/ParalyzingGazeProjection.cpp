/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ParalyzingGazeProjection.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/NewHorizonsParalyzingGaze.h"
namespace paralyzingGazeAI
{
bool qualifies(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return newHorizonsParalyzingGaze::qualifiesAttack(battle, attack.attacker, attack.defender,
		attack.shooting, attack.physicalDamage, attack.secondaryAttack
			|| attack.bracePreemptive || attack.preemptiveDamagePercent > 0 || attack.cleaveDamagePercent > 0);
}
bool needsProjection(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	return qualifies(battle, attack) || (attack.defender && newHorizonsParalyzingGaze::pending(*attack.defender));
}
bool cancel(HypotheticBattle & battle, uint32_t opponent, uint32_t retaliator, bool shooting,
	std::optional<Consumption> & receipt)
{
	receipt = consume(battle, opponent, retaliator, shooting);
	return receipt && !receipt->ignoredByDefiant;
}
void apply(HypotheticBattle & battle, uint32_t source, uint32_t target, int64_t damage, bool admitted)
{
	if(!admitted || damage <= 0)
		return;
	const auto * attacker = battle.battleGetUnitByID(source);
	const auto * recipient = battle.battleGetUnitByID(target);
	if(newHorizonsParalyzingGaze::qualifiesAttack(battle, attacker, recipient, true, true, false)
		&& !newHorizonsParalyzingGaze::pending(*recipient))
		battle.getForUpdate(target)->addUnitBonus({newHorizonsParalyzingGaze::marker()});
}
std::optional<Consumption> consume(HypotheticBattle & battle, uint32_t opponentId, uint32_t retaliatorId, bool shooting,
	std::optional<newHorizonsAerialSupremacy::Excursion> aerial)
{
	const auto * opponent = battle.battleGetUnitByID(opponentId);
	const auto * current = battle.battleGetUnitByID(retaliatorId);
	if(!current || !opponent || !newHorizonsParalyzingGaze::canConsume(*current))
		return std::nullopt;
	const auto token = newHorizonsParalyzingGaze::pending(*current);
	const bool ignored = battle.battleCanUseGazeDefiant(opponent, current, shooting, aerial ? &*aerial : nullptr);
	if(ignored)
	{
		const auto side = battle.playerToSide(battle.battleGetActionController(current));
		auto state = battle.getArmorerDefiantState(side);
		state.consumeAt(battle.battleGetRound());
		battle.setArmorerDefiantState(side, state);
	}
	auto target = battle.getForUpdate(retaliatorId);
	if(!ignored)
		target->counterAttacks.use();
	target->removeUnitBonus(std::vector<Bonus>{*token});
	return Consumption{opponentId, retaliatorId, shooting, ignored, aerial};
}
void replay(HypotheticBattle & battle, const Consumption & receipt)
{
	const auto * opponent = battle.battleGetUnitByID(receipt.opponent);
	const auto * retaliator = battle.battleGetUnitByID(receipt.retaliator);
	if(!opponent || !opponent->alive() || opponent->isGhost() || opponent->isTimeStopped()
		|| !retaliator || !newHorizonsParalyzingGaze::canConsume(*retaliator)
		|| battle.battleCanUseGazeDefiant(opponent, retaliator, receipt.shooting, receipt.aerial ? &*receipt.aerial : nullptr) != receipt.ignoredByDefiant)
		throw std::runtime_error("Stale Paralyzing Gaze ordinary-counter receipt");
	// The same synchronous branch owns admission and mutation. Reject stale
	// receipts before spending its token, counter resource or Defiant budget.
	consume(battle, receipt.opponent, receipt.retaliator, receipt.shooting, receipt.aerial);
}
bool consumeAndAllowCounter(HypotheticBattle & battle, uint32_t opponent, uint32_t retaliator, bool shooting)
{
	const auto gaze = consume(battle, opponent, retaliator, shooting);
	return !gaze || gaze->ignoredByDefiant;
}

}
