/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "NativeEventCommon.h"
#include "../../CStack.h"
#include "../../battle/CUnitState.h"
#include "../../battle/NewHorizonsFrozen.h"
#include "../../bonuses/BonusList.h"
#include "../../networkPacks/PacksForClientBattle.h"
#include "../../networkPacks/SetStackEffect.h"
#include "../../spells/CSpell.h"

namespace nativeEvents
{
std::pair<int64_t, int64_t> damage(const Context & context, const battle::Unit & unit, int64_t amount,
	const battle::Unit * source, bool magical, const CSpell * spell, bool destroyRemains)
{
	if(spell && context.battle.battleGetUnitByID(unit.unitId()) != &unit)
		throw std::runtime_error("Damage target must be a unit in the given battle");
	if(source && context.battle.battleGetUnitByID(source->unitId()) != source)
		throw std::runtime_error("Damage source must be a unit in the given battle");
	BattleStackAttacked hit;
	hit.damageAmount = amount;
	hit.stackAttacked = unit.unitId();
	hit.attackerID = source ? source->unitId() : -1;
	auto state = unit.acquireState();
	CStack::prepareAttacked(hit, *context.server.getRNG(), state, destroyRemains, false,
		magical || spell ? battle::DamageProvenance::SPELL : battle::DamageProvenance::OTHER);
	if(spell)
	{
		hit.flags |= BattleStackAttacked::SPELL_EFFECT;
		hit.spellID = spell->getId();
	}
	StacksInjured pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	pack.stacks.push_back(hit);
	context.server.apply(pack);
	return {hit.damageAmount, hit.killedAmount};
}

std::pair<int64_t, int64_t> heal(const Context & context, const battle::Unit & unit, int64_t amount,
	EHealLevel level, EHealPower power)
{
	auto changed = unit.acquire();
	const auto healed = changed->heal(amount, level, power);
	BattleUnitsChanged pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	UnitChanges update(unit.unitId(), UnitChanges::EOperation::UPDATE);
	update.data = changed->save();
	update.healthDelta = healed.healedHealthPoints;
	pack.changedStacks.push_back(update);
	context.server.apply(pack);
	return {healed.healedHealthPoints, healed.resurrectedCount};
}

void animation(const Context & context, const battle::Unit & unit, const std::string & name,
	const std::string & sound, double transparency, bool deferred)
{
	BattleAnimationPlayed pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	pack.animation = AnimationPath::builtin(name);
	pack.sound = sound.empty() ? AudioPath() : AudioPath::builtin(sound);
	pack.transparency = static_cast<float>(transparency);
	pack.deferred = deferred;
	BattleAnimationPlayed::Target target;
	target.unitID = unit.unitId();
	target.tile = BattleHex::INVALID;
	pack.targets.push_back(target);
	context.server.apply(static_cast<CPackForClient &>(pack));
}

void addBonus(const Context & context, const battle::Unit & unit, const Bonus & bonus, bool cumulative)
{
	SetStackEffect pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	if(cumulative) pack.toAdd.emplace_back(unit.unitId(), std::vector<Bonus>{bonus});
	else pack.toUpdate.emplace_back(unit.unitId(), std::vector<Bonus>{bonus});
	context.server.apply(pack);
}

void removeBonuses(const Context & context, const battle::Unit & unit, const BonusList & bonuses)
{
	std::vector<Bonus> records;
	for(const auto & bonus : bonuses) records.push_back(*bonus);
	if(records.empty()) return;
	SetStackEffect pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	pack.toRemove.emplace_back(unit.unitId(), std::move(records));
	context.server.apply(pack);
}

void clearFrozen(const Context & context, const battle::Unit & unit)
{
	if(context.battle.battleGetUnitByID(unit.unitId()) != &unit)
		throw std::runtime_error("Frozen removal requires a unit in the given battle");
	if(!newHorizonsFrozen::enabled(context.battle.getBattle()->getMagicRules())) return;
	const auto removed = newHorizonsFrozen::removalPlan(unit);
	if(removed.empty()) return;
	SetStackEffect pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	pack.toRemove.emplace_back(unit.unitId(), removed);
	context.server.apply(pack);
}

const battle::Unit * addUnit(const Context & context, battle::UnitInfo info)
{
	info.id = context.battle.battleNextUnitId();
	BattleUnitsChanged pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	UnitChanges addition(info.id, UnitChanges::EOperation::ADD);
	info.save(addition.data);
	pack.changedStacks.push_back(addition);
	context.server.apply(pack);
	return context.battle.battleGetUnitByID(info.id);
}

void removeUnit(const Context & context, const battle::Unit & unit)
{
	BattleUnitsChanged pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	pack.changedStacks.emplace_back(unit.unitId(), UnitChanges::EOperation::REMOVE);
	context.server.apply(pack);
}

void refreshUnits(const Context & context)
{
	StacksInjured pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	context.server.apply(pack);
}

void changeUnit(const Context & context, battle::Unit & state, int64_t healthDelta)
{
	BattleUnitsChanged pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	UnitChanges update(state.unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state.save();
	update.healthDelta = healthDelta;
	pack.changedStacks.push_back(update);
	context.server.apply(pack);
}

void moveUnit(const Context & context, const battle::Unit & unit, BattleHex destination, bool teleport)
{
	BattleStackMoved pack;
	pack.battleID = context.battle.getBattle()->getBattleID();
	pack.stack = unit.unitId();
	pack.distance = 0;
	pack.teleporting = teleport;
	pack.tilesToMove.insert(destination);
	context.server.apply(pack);
}
}
