/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "AttackControllerProjection.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/ISpellMechanics.h"
#include <algorithm>
namespace attackControllerAI
{
newHorizonsAttackControllers::Strike capture(const CBattleInfoCallback & board,
	const BattleAttackInfo & attack, BattleHex impact, int64_t damage)
{
	auto accepted = attack;
	accepted.defenderPos = impact;
	return newHorizonsAttackControllers::captureStrike(board, accepted, damage);
}
bool needsPreview(const CBattleInfoCallback & board, const BattleAttackInfo & attack, BattleHex impact)
{
	const auto hasEffect = [](const auto & receipt)
	{
		return receipt.fireball || receipt.bloodlust || !receipt.cripple.add.empty();
	};
	if(hasEffect(capture(board, attack, impact, 1))) return true;
	if(!attack.shooting && attack.attacker && attack.defender)
	{
		auto counter = attack.reverse();
		counter.retaliation = true;
		return hasEffect(capture(board, counter, attack.attacker->getPosition(), 1));
	}
	return false;
}
void applyCripple(HypotheticBattle & board, const newHorizonsAttackControllers::Strike & receipt)
{
	if(!receipt.cripple.add.empty())
		if(const auto * unit = board.battleGetUnitByID(receipt.cripple.target); unit && unit->alive() && !unit->isTimeStopped())
		{
			board.removeUnitBonus(receipt.cripple.target, receipt.cripple.remove);
			board.addUnitBonus(receipt.cripple.target, receipt.cripple.add);
		}
}
std::vector<Injury> apply(HypotheticBattle & board, const newHorizonsAttackControllers::Strike & receipt)
{
	if(!receipt.fireball) return {};
	const auto * source = board.battleGetUnitByID(receipt.source);
	if(!source || !source->alive() || source->isGhost() || source->isTimeStopped()) return {};
	const auto policy = board.battleGetVisibleHeroPolicy(board.playerToSide(board.battleGetOwner(source)));
	if(policy.kind() != VisibleHeroPolicy::Knowledge::VISIBLE_HERO) return {};
	newHorizonsAttackControllers::BallistaCaster caster(*source, *policy.legacyHero());
	spells::BattleCast cast(&board, &caster, spells::Mode::PASSIVE, SpellID(SpellID::FIREBALL).toSpell());
	const spells::Target target{spells::Destination(receipt.impact)};
	const auto mechanics = cast.mechanicsForTarget(target);
	if(!mechanics->canBeCastAt(target)) return {};
	// Native geometry/defenses own the actual affected set. The finite roster
	// snapshot also catches native linked damage without inventing another cast.
	std::vector<std::shared_ptr<battle::CUnitState>> before;
	for(const auto * unit : board.battleGetAllUnits(false))
		if(unit && unit->alive() && !unit->isGhost()) before.push_back(unit->acquireState());
	mechanics->castEval(board.getServerCallback(), target);
	std::vector<Injury> result;
	for(auto & unit : before)
		if(const auto * after = board.battleGetUnitByID(unit->unitId()))
			if(const auto loss = unit->getAvailableHealth() - after->getAvailableHealth(); loss > 0)
				result.push_back({std::move(unit), loss});
	return result;
}
void score(AttackPossibility & preview, const std::shared_ptr<HypotheticBattle> & board,
	const std::vector<Injury> & injuries, DamageCache & cache)
{
	const auto reference = board->battleGetActionController(preview.attack.attacker);
	for(const auto & hit : injuries)
	{
		const auto id = hit.before->unitId();
		const auto value = AttackPossibility::calculateDamageReduce(nullptr, hit.before.get(), hit.healthLoss, cache, board);
		if(board->battleGetOwner(hit.before.get()) == reference) preview.attackerDamageReduce += value;
		else preview.defenderDamageReduce += value;
		if(id == preview.attackerState->unitId()) continue;
		const auto existing = std::ranges::find_if(preview.affectedUnits,
			[id](const auto & unit) { return unit->unitId() == id; });
		if(existing == preview.affectedUnits.end()) preview.affectedUnits.push_back(board->getForUpdate(id));
	}
}
}
