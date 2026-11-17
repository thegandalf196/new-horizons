/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../SpellDamageContext.h"
#include "../../../battle/RedemptionEvent.h"
#include "UnitEffect.h"
#include "StatusHelpers.h"
#include "../../NewHorizonsSorcerySpells.h"
#include "../../ISpellMechanics.h"
#include "../../../CStack.h"
#include "../../../battle/CUnitState.h"
#include "../../../networkPacks/PacksForClientBattle.h"
#include "../../../networkPacks/SetStackEffect.h"
#include "../../NewHorizonsMetamagicOverflow.h"
#include <typeinfo>

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
		std::optional<battle::VowCasualtyQuota> quota;
		auto damageContext = previewSpellDamageContext(*mechanics, unit, quota);
		damageContext.bypassMagicalAbsorption = true;
		copy->damageWithContext(damage, false, battle::DamageProvenance::SPELL, damageContext, nullptr, true);
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
		int64_t postShield = hit.damageAmount;
		const auto healthBefore = unit.getShadowGiftCurrentHealth();
		hit.stackAttacked = unit.unitId();
		hit.attackerID = -1;
		hit.flags |= BattleStackAttacked::SPELL_EFFECT;
		hit.spellID = mechanics->getSpellId();
		auto state = unit.acquireState();
		// Preserve Force Lance's temporary-HP bypass and ordinary remains. The
		// typed shared shield stage owns any explicit current defensive eligibility.
		auto damageContext = mechanics->spellDamageContext(unit);
		damageContext.bypassMagicalAbsorption = true;
		CStack::prepareAttacked(hit, *server->getRNG(), state, false, true, battle::DamageProvenance::SPELL, &postShield, &damageContext);
		StacksInjured injury;
		injury.battleID = mechanics->getBattleID();
		injury.stacks.push_back(hit);
		if(damageContext.redemption) damageContext.redemption->record(*mechanics->battle(), injury.stacks);
		server->apply(injury);
		server->recordMetamagicOverflowPrimary({id, healthBefore, postShield, hit.damageAmount, !state->alive()});
		server->recordResolvedSpellRecipient(unit.unitId());
		const nativeEvents::Context context{*server, *mechanics->battle(), nullptr, nullptr, parameters, {}};
		if(const auto * current = mechanics->battle()->battleGetUnitByID(id))
			nativeEvents::clearFrozen(context, *current);
		if(state->alive())
		{
			SetStackEffect pin;
			pin.battleID = mechanics->getBattleID();
			auto effect = newHorizonsSorcerySpells::forceLancePin(mechanics->getSpellId());
			const auto origin = mechanics->getCreatureAbilityDescriptor();
			const auto family = origin ? origin->family : mechanics->getHeroCaster()
				? creatureAbility::Family::HERO_SPELL : creatureAbility::Family::CREATURE_SPELL;
			effect.abilityDescriptor = creatureAbility::spellEffect(mechanics->getSpellId(), family,
				creatureAbility::HOSTILE_AFFLICTION, creatureAbility::IRRESISTIBLE | creatureAbility::REMOVABLE | creatureAbility::DISPELLABLE,
				mechanics->battle()->sideToPlayer(mechanics->getEffectCasterSide()));
			pin.toUpdate.emplace_back(unit.unitId(), std::vector<Bonus>{std::move(effect)});
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
bool isForceLanceEffect(const spells::effects::Effect & effect)
{
	return typeid(effect) == typeid(spells::effects::native::ForceLance);
}

std::shared_ptr<spells::effects::Effect> createForceLanceEffect(const std::string & path)
{
	return path == "spells/forceLance" ? std::make_shared<spells::effects::native::ForceLance>() : nullptr;
}
}
