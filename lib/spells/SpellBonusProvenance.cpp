/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpellBonusProvenance.h"
#include "ISpellMechanics.h"
#include "../battle/CBattleInfoCallback.h"
#include "../networkPacks/SetStackEffect.h"
#include "../bonuses/Bonus.h"

namespace spells
{
void stampSpellEffectOrigins(const Mechanics & mechanics, const CBattleInfoCallback & battle,
	PlayerColor caster, SpellID cast, SpellID effect, SetStackEffect & packet)
{
	if(caster == PlayerColor::CANNOT_DETERMINE) return;
	const auto descriptor = mechanics.getCreatureAbilityDescriptor();
	const auto stamp = [&](auto & changes)
	{
		for(auto & [unitId, bonuses] : changes)
		{
			const auto * recipient = battle.battleGetUnitByID(unitId);
			const auto owner = recipient ? battle.battleGetOwner(recipient) : PlayerColor::CANNOT_DETERMINE;
			for(auto & bonus : bonuses)
			{
				stampSpellBonusProvenance(bonus, cast, effect, caster, owner);
				if(descriptor && bonus.source == BonusSource::SPELL_EFFECT
					&& (bonus.sid == BonusSourceID(cast) || bonus.sid == BonusSourceID(effect)))
					bonus.abilityDescriptor = descriptor;
			}
		}
	};
	stamp(packet.toAdd);
	stamp(packet.toUpdate);
}

void stampSpellBonusProvenance(Bonus & bonus, SpellID cast, SpellID effect,
	PlayerColor caster, PlayerColor recipient)
{
	if(caster == PlayerColor::CANNOT_DETERMINE || bonus.source != BonusSource::SPELL_EFFECT
		|| (bonus.sid != BonusSourceID(cast) && bonus.sid != BonusSourceID(effect))) return;
	bonus.spellCasterOwner = caster;
	if(recipient != PlayerColor::CANNOT_DETERMINE && bonus.type == BonusType::MORALE && bonus.val < 0)
		bonus.appliedByEnemy = caster != recipient;
}
}
