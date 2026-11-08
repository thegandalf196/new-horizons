/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "SpellBonusProvenance.h"
#include "../bonuses/Bonus.h"

namespace spells
{
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
