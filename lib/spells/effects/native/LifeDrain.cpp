/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "LifeDrain.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsShadowMagnitudes.h"
#include "../../CSpell.h"
#include "../../../battle/CUnitState.h"
#include "../../../combatScripts/nativeEvents/NativeEventCommon.h"
#include "../../../texts/MetaString.h"
#include <cmath>

namespace spells::effects::native
{
bool LifeDrain::enemy(const Mechanics * mechanics, const battle::Unit * unit) const
{
	return unit && unit->alive() && unit->isValidTarget(false)
		&& ((mechanics->battle()->battleGetOwner(unit) == mechanics->getCasterColor()) == mechanics->isMagicMirror())
		&& isReceptive(mechanics, unit);
}

bool LifeDrain::friendly(const Mechanics * mechanics, const battle::Unit * unit) const
{
	return unit && unit->alive() && unit->isValidTarget(false)
		&& ((mechanics->battle()->battleGetOwner(unit) == mechanics->getCasterColor()) != mechanics->isMagicMirror());
}

void LifeDrain::adjustTargetTypes(std::vector<TargetType> & types, const Mechanics *) const
{
	if(types.empty()) return;
	if(types.front() != TargetType::CREATURE) types.clear();
	else if(types.size() == 1) types.push_back(TargetType::CREATURE);
	else if(types.size() != 2 || types[1] != TargetType::CREATURE) types.clear();
}

bool LifeDrain::applicableGeneral(Problem & problem, const Mechanics * mechanics) const
{
	bool hasEnemy = false, hasFriend = false;
	if(mechanics->usesNewHorizonsMagicV3())
		mechanics->battle()->battleGetUnitsIf([&](const battle::Unit * unit)
		{
			if(!hasEnemy && enemy(mechanics, unit)) hasEnemy = true;
			if(!hasFriend && friendly(mechanics, unit)) hasFriend = true;
			return hasEnemy && hasFriend;
		});
	if(hasEnemy && hasFriend) return true;
	mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	return false;
}

bool LifeDrain::valid(const Mechanics * mechanics, const Target & target) const
{
	return mechanics->usesNewHorizonsMagicV3() && !target.empty() && target.size() <= 2
		&& enemy(mechanics, target.front().unitValue)
		&& (target.size() == 1 || friendly(mechanics, target[1].unitValue));
}

bool LifeDrain::applicableTarget(Problem & problem, const Mechanics * mechanics, const Target & target) const
{
	if(valid(mechanics, target)) return true;
	mechanics->adaptProblem(ESpellCastProblem::NO_APPROPRIATE_TARGET, problem);
	return false;
}

Target LifeDrain::transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const
{
	if(!mechanics->usesNewHorizonsMagicV3()) return {};
	const auto first = !aim.empty() && aim.front().unitValue ? Target{aim.front()} : target;
	const auto filtered = UnitEffect::transformTarget(mechanics, aim, first);
	if(filtered.empty()) return {};
	Target result{filtered.front()};
	if(aim.size() >= 2) result.push_back(aim[1]);
	return result;
}

SpellEffectValue LifeDrain::getHealthChange(const Mechanics * mechanics, const Target & target) const
{
	SpellEffectValue result;
	if(!mechanics->usesNewHorizonsMagicV3() || target.empty() || !enemy(mechanics, target.front().unitValue)) return result;
	const auto * unit = target.front().unitValue;
	auto copy = unit->acquireState();
	const auto health = copy->getAvailableHealth();
	const auto count = copy->getCount();
	auto amount = mechanics->adjustEffectValue(unit);
	copy->damage(amount);
	result.hpDelta = -(health - copy->getAvailableHealth());
	result.unitsDelta = -(count - copy->getCount());
	return result;
}

void LifeDrain::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	if(target.size() != 2 || !valid(mechanics, target)) return;
	const auto * enemyUnit = target.front().unitValue;
	const auto * friendlyUnit = target[1].unitValue;
	const auto enemyId = enemyUnit->unitId();
	server->recordResolvedSpellRecipient(enemyId);
	server->recordResolvedSpellRecipient(friendlyUnit->unitId());
	const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
	const auto [damage, killed] = nativeEvents::damage(context, *enemyUnit, mechanics->adjustEffectValue(enemyUnit), mechanics->getUnitCaster(), true);
	if(const auto * current = mechanics->battle()->battleGetUnitByID(enemyId)) nativeEvents::clearFrozen(context, *current);
	const auto rawHealing = static_cast<int64_t>(std::floor(static_cast<double>(damage)
		* newHorizonsMagic::lifeDrainHealingPercent(mechanics->getHeroCaster()) / 100));
	const auto healing = mechanics->adjustMetamagicMagnitude(rawHealing, friendlyUnit);
	int64_t healed = 0;
	if(healing > 0) healed = nativeEvents::heal(context, *friendlyUnit, healing, EHealLevel::HEAL, EHealPower::PERMANENT).first;
	if(damage > 0) nativeEvents::spellDamageLog(context, *mechanics->getSpellId().toSpell(), enemyUnit, damage, killed);
	if(healed > 0)
	{
		MetaString message;
		message.appendTextID("new-horizons.combat.lifeDrain.healed");
		message.replaceTextID(nativeEvents::creatureName(friendlyUnit, friendlyUnit->getCount()));
		message.replaceNumber(healed);
		nativeEvents::log(context, message);
	}
}
}
