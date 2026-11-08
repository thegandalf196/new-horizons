/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NetPacksBase.h"
#include "../battle/BattleSide.h"

/// A completed own hero cast activates one permanent family, after its effects.
struct DLL_LINKAGE SetElementalAttunementState : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleSide side = BattleSide::NONE;
	SpellID spellID;
	uint8_t beforeMask = 0;
	uint8_t afterMask = 0;
	void visitTyped(ICPackVisitor & visitor) override;
	void validateShape() const
	{
		if(battleID == BattleID::NONE || !spellID.hasValue()
			|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
			|| beforeMask > 15 || afterMask > 15 || beforeMask == afterMask
			|| (beforeMask & afterMask) != beforeMask)
			throw std::runtime_error("Invalid completed elemental Attunement update");
	}
	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_NATIVE_RULES_REDESIGN))
			throw std::runtime_error("Elemental Attunement updates require the native rules redesign format");
		if(h.saving) validateShape();
		h & battleID; h & side; h & spellID; h & beforeMask; h & afterMask;
		if(!h.saving) validateShape();
	}
};
