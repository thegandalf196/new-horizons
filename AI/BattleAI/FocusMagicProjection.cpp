/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ForecastNativeAdapter.h"
#include "FocusMagicProjection.h"
#include "StackWithBonuses.h"
#include "HypotheticBattle.h"
#include "VisibleCandidateWorld.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsFocusMagic.h"
#include <algorithm>
namespace focusMagicAI
{
bool requiresProjection(const CBattleInfoCallback & state)
{
	return std::ranges::any_of(state.battleGetAllUnits(false), [](const battle::Unit * unit)
	{ return unit && newHorizonsFocusMagic::active(*unit); });
}
std::optional<AttackPossibility> evaluateDirect(const Environment * environment, const BattleAttackInfo & attack,
	BattleHex hex, DamageCache & cache, const std::shared_ptr<CBattleInfoCallback> & source, bool perfectMoment,
	const std::optional<newHorizonsAerialSupremacy::Excursion> & aerial, const dreadCavalryAI::Action * dread,
	bool validatedApproach, BattleHex approachOrigin)
{
	if(!environment || std::dynamic_pointer_cast<HypotheticBattle>(source) || !requiresProjection(*source)) return {};
	if(source->battleGetUnitByID(attack.attacker->unitId()) != attack.attacker
		|| source->battleGetUnitByID(attack.defender->unitId()) != attack.defender)
		throw std::invalid_argument("Direct Focus actors must belong to their visible callback");
	auto world = std::make_shared<HypotheticBattle>(environment, source);
	auto rebound = attack;
	rebound.attacker = world->getForUpdate(attack.attacker->unitId()).get();
	rebound.defender = world->getForUpdate(attack.defender->unitId()).get();
	auto result = AttackPossibility::evaluate(rebound, hex, cache, world, perfectMoment, aerial,
		dread, validatedApproach, approachOrigin);
	result.queryWorld = std::make_shared<VisibleCandidateWorld>(world);
	return result;
}
void afterAttack(battle::CUnitState & unit, bool ranged, bool counter, bool physical, bool accepted, HypotheticBattle * branch)
{
	if(branch && forecast::NativeAdapter::afterAttack(*branch, unit, ranged, counter, physical, accepted)) return;
	unit.afterAttack(ranged, counter, physical);
	if(!accepted || !ranged || !newHorizonsFocusMagic::active(unit)) return;
	auto * isolated = dynamic_cast<StackWithBonuses *>(&unit);
	if(!isolated) throw std::logic_error("Focus forecast requires an isolated bonus owner");
	const auto consumed = newHorizonsFocusMagic::consume(unit);
	if(!consumed.remove.empty()) isolated->removeUnitBonus(consumed.remove);
	if(!consumed.add.empty()) isolated->addUnitBonus(consumed.add);
}
}
