/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "NetPacksBase.h"
#include "../constants/EntityIdentifiers.h"

/// Astral's Practice Makes Perfect: the authoritative new practice bonus of one spell
/// after his cast. The state itself is an ordinary permanent hero bonus.
struct DLL_LINKAGE SetNewHorizonsSpellPractice : CPackForClient
{
	ObjectInstanceID heroID = ObjectInstanceID::NONE;
	SpellID spell = SpellID::NONE;
	int32_t percent = 0;
	void visitTyped(ICPackVisitor & visitor) override;

	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_REVISED_SPECIALTIES))
			throw std::runtime_error("Practice Makes Perfect packet requires the revised specialty format");
		h & heroID;
		h & spell;
		h & percent;
		if(!h.saving && (percent < 0 || percent > 1000))
			throw std::runtime_error("Invalid Practice Makes Perfect packet");
	}
};
