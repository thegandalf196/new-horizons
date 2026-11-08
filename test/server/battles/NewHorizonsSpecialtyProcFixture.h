/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsSpecialtyEventFixture.h"
#include "../../../lib/battle/NewHorizonsSpecialtyProcs.h"
#include "../../../lib/battle/NewHorizonsStun.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/mapObjects/army/CArmedInstance.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/AbilityCaster.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../server/battles/BattleSpecialtyProcs.h"
class NewHorizonsSpecialtyProcFixture : public NewHorizonsSpecialtyEventFixture
{
protected:
	void passiveCast(SpellID spell, CStack & caster, CStack & target)
	{
		spells::AbilityCaster ability(&caster, 0);
		spells::BattleCast cast(battle(), &ability, spells::Mode::PASSIVE, spell.toSpell());
		spells::Target aim; aim.emplace_back(&target);
		cast.cast(gameHandler->spellcastEnvironment(), aim);
	}
	std::vector<std::byte> randomBytes(GameRandomizer & random)
	{
		CMemorySerializer wire; random.serialize(wire.oser); return wire.extractBuffer();
	}
	void status(CStack & unit, Bonus bonus)
	{
		SetStackEffect effect; effect.battleID = BattleID(0);
		effect.toAdd.emplace_back(unit.unitId(), std::vector<Bonus>{std::move(bonus)});
		gameHandler->sendAndApply(effect);
	}
	int forfeitures(uint32_t id) const
	{
		return std::ranges::count_if(server.startedActions, [id](const auto & action)
		{ return action.ba.stackNumber == id && action.ba.actionType == EActionType::NO_ACTION; });
	}
	void noMorale(CStack & unit)
	{
		unit.addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::NO_MORALE,
			BonusSource::OTHER, 1, BonusSourceID()));
	}
};
