/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "../battle/CBattleInfoCallback.h"
#include "../GameLibrary.h"
#include <stdexcept>
namespace spells
{
const CreatureService * BaseMechanics::creatures() const
{
	const auto owned = cb ? cb->battleOwnedCreatureService() : std::nullopt;
	if(owned)
	{
		if(!*owned) throw std::logic_error("Owned spell creature domain was not captured");
		return *owned;
	}
	return LIBRARY->creatures(); // Exact legacy route; owned UNKNOWN cannot reach it.
}
}
