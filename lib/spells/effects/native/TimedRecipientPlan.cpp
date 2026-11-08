/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Timed.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsSpellAvailability.h"
#include "../../NewHorizonsTimedStatusPlan.h"
#include "../../NewHorizonsSorceryRevision.h"
#include "../../../nativeRules/NativeRulePilots.h"
#include "../../../nativeRules/PreparedPilotRules.h"
#include "../../../battle/CBattleInfoCallback.h"
#include "../../../battle/IBattleState.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include <vcmi/spells/Spell.h>

namespace spells::effects::native
{
bool Timed::slowSpeed(const Mechanics & mechanics)
{
	const auto * state = mechanics.battle() ? mechanics.battle()->getBattle() : nullptr;
	return state && newHorizonsSorceryRevision::slowAffectsSpeed(state->getMagicRules());
}

Timed::CastPlan Timed::prepareCast(const Mechanics * mechanics) const
{
	CastPlan result;
	const auto * spell = newHorizonsMagic::effectSpellForCast(*mechanics);
	result.key = spell->getJsonKey();
	if(!mechanics->usesNewHorizonsMagicV3())
		for(const auto * restricted : {"new-horizons:crusade", "new-horizons:shieldOfChaos", "new-horizons:entangle", "new-horizons:vengefulVines"})
			if(result.key == restricted) { result.allowed = false; return result; }
	result.converted = convert(mechanics);
	const auto * hero = mechanics->getHeroCaster();
	if(result.key == "new-horizons:sanctuary" && mechanics->usesNewHorizonsMagicV3() && hero
		&& hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.sanctuaryKeeper"))
	{
		const auto * rules = hero->getPreparedPilotPerks();
		const auto * authored = rules ? rules->addition(nativeRules::TimedSourceSlot::Sanctuary) : nullptr;
		result.prepared = authored ? authored->timedAdditions({spell->getId(), 0, true, false, true}).front()
			: nativeRules::sanctuaryKeeperMorale(spell->getId());
		result.preparedKey = "sanctuaryKeeperMorale";
	}
	else if(result.key == "core:forgetfulness" && mechanics->usesNewHorizonsMagicV3())
	{
		const bool mindbreaker = hero && hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.mindbreaker");
		const auto * rules = hero ? hero->getPreparedPilotPerks() : nullptr;
		const auto * authored = rules ? rules->addition(nativeRules::TimedSourceSlot::Forgetfulness) : nullptr;
		const auto rounds = newHorizonsMagic::forgetfulnessDurationForCast(*mechanics);
		result.prepared = authored ? authored->timedAdditions({spell->getId(), rounds, false, mindbreaker, true}).front()
			: nativeRules::creatureSuppression(spell->getId(), result.key, rounds, mindbreaker);
		result.preparedKey = "creatureAbilitySuppression";
	}
	return result;
}

Timed::RecipientPlan Timed::prepareRecipient(const Mechanics * mechanics, const battle::Unit * unit, const CastPlan & cast) const
{
	RecipientPlan result{cast.converted, cast.prepared, cast.preparedKey};
	double preparedValue = result.prepared ? result.prepared->val : 0;
	specialty(mechanics, unit, result.buffer, result.prepared ? &preparedValue : nullptr);
	if(result.prepared) result.prepared->val = static_cast<int32_t>(preparedValue);
	recipientPower(mechanics, unit, cast.key, result.buffer);
	if(cast.key == "core:slow")
	{
		const auto * state = mechanics->battle() ? mechanics->battle()->getBattle() : nullptr;
		const int percent = mechanics->isMassSlow() ? 60 : state
			? newHorizonsMagic::spellVariantPowerPercent(state->getMagicRules(), mechanics->getSpellId()) : 100;
		if(percent < 100)
			for(auto & [name, bonus] : result.buffer)
				if((bonus["type"].String() == "STACKS_INITIATIVE" || (slowSpeed(*mechanics) && bonus["type"].String() == "STACKS_SPEED"))
					&& bonus["val"].Float() < 0)
					bonus["val"].Float() = newHorizonsSorceryRevision::scaleSlowVariantValue(bonus["val"].Float(), percent);
	}
	return result;
}
}
