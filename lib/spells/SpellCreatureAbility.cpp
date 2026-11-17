/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include <stdexcept>
namespace spells
{
void BattleCast::setCreatureAbilityDescriptor(creatureAbility::Descriptor value)
{
	if(value)
	{
		value->validate();
		const auto * identity = std::get_if<SpellID>(&value->identity);
		if(value->resolution != creatureAbility::Resolution::SPELL_CAST || !identity || *identity != getSpell()->getId())
			throw std::runtime_error("Spell producer descriptor requires its exact logical spell identity");
	}
	creatureAbilityDescriptor = std::move(value);
}
}
