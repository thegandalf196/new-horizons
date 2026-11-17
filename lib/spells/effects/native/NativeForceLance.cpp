/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../NewHorizonsSorcerySpells.h"
#include "../../ISpellMechanics.h"
#include "../../../CStack.h"
#include "../../../battle/CUnitState.h"
#include "../../../networkPacks/PacksForClientBattle.h"
#include "../../../networkPacks/SetStackEffect.h"

namespace spells::effects::native
{
namespace
{
class ForceLance final : public UnitEffect
{
	bool isValidTarget(const Mechanics * mechanics, const battle::Unit * unit) const override
	{
		return unit && unit->alive() && unit->isValidTarget(false) && !unit->isInvincible()
			&& mechanics->ownerMatches(unit);
	}
	bool isReceptive(const Mechanics *, const battle::Unit * unit) const override
	{
		// Admission bypass is local to this effect; ordinary spells keep immunity.
		return unit && !unit->isInvincible();
	}
public:
	Target transformTarget(const Mechanics * mechanics, const Target & aim, const Target & target) const override
	{
		auto result = UnitEffect::transformTarget(mechanics, aim, target);
		if(result.size() > 1) result.resize(1);
		return result;
	}
	SpellEffectValue getHealthChange(const Mechanics * mechanics, const Target & target) const override
	{
		SpellEffectValue result;
		if(target.size() != 1 || !isValidTarget(mechanics, target.front().unitValue)) return result;
		const auto & unit = *target.front().unitValue;
		auto copy = unit.acquireState();
		const auto before = copy->getAvailableHealth();
		const auto count = copy->getCount();
		auto damage = newHorizonsSorcerySpells::forceLanceDamage(*mechanics);
		copy->damageShadowGiftSacrifice(damage);
		result.hpDelta = copy->getAvailableHealth() - before;
		result.unitsDelta = copy->getCount() - count;
		return result;
	}
	void apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const override
	{
		if(target.size() != 1 || !isValidTarget(mechanics, target.front().unitValue)) return;
		const auto & unit = *target.front().unitValue;
		const auto id = unit.unitId();
		BattleStackAttacked hit;
		hit.damageAmount = newHorizonsSorcerySpells::forceLanceDamage(*mechanics);
		hit.stackAttacked = unit.unitId();
		hit.attackerID = -1;
		hit.flags |= BattleStackAttacked::SPELL_EFFECT;
		hit.spellID = mechanics->getSpellId();
		auto state = unit.acquireState();
		// Bypass magical absorption/temporary shields while preserving ordinary remains.
		CStack::prepareAttacked(hit, *server->getRNG(), state, false, true, battle::DamageProvenance::SPELL);
		StacksInjured injury;
		injury.battleID = mechanics->getBattleID();
		injury.stacks.push_back(hit);
		server->apply(injury);
		server->recordResolvedSpellRecipient(unit.unitId());
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		if(const auto * current = mechanics->battle()->battleGetUnitByID(id))
			nativeEvents::clearFrozen(context, *current);
		if(state->alive())
		{
			SetStackEffect pin;
			pin.battleID = mechanics->getBattleID();
			pin.toUpdate.emplace_back(unit.unitId(), std::vector<Bonus>{newHorizonsSorcerySpells::forceLancePin(mechanics->getSpellId())});
			server->apply(pin);
		}
		if(server->describeChanges())
		{
			nativeEvents::spellDamageLog(context, *mechanics->getSpellId().toSpell(), &unit, hit.damageAmount, hit.killedAmount);
		}
	}
};
}
}

namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createForceLanceEffect(const std::string & path)
{
	return path == "spells/forceLance" ? std::make_shared<spells::effects::native::ForceLance>() : nullptr;
}
}
