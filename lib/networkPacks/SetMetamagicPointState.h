/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "NetPacksBase.h"
#include "../battle/MetamagicPointState.h"
#include "../battle/BattleSide.h"

/// Publishes post-resolution growth without changing the spell that earned it.
struct DLL_LINKAGE SetMetamagicPointState : public CPackForClient
{
	BattleID battleID = BattleID::NONE;
	BattleSide side = BattleSide::NONE;
	int32_t round = -1;
	MetamagicPointState state;

	void visitTyped(ICPackVisitor & visitor) override;

	void validateShape() const
	{
		state.validate();
		if(battleID == BattleID::NONE || round < 1
			|| (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
			throw std::runtime_error("Invalid Metamagic point update identity");
	}

	void validateTransitionFrom(const MetamagicPointState & previous, int32_t currentRound) const
	{
		validateShape();
		previous.validate();
		if(round != currentRound || state.points < previous.points
			|| (previous.singularityReached && !state.singularityReached)
			|| (previous.echoUsedThisRound && !state.echoUsedThisRound))
			throw std::runtime_error("Invalid Metamagic point update transition");
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_FACTION_SKILL_REVISIONS))
			throw std::runtime_error("Metamagic point updates require the revised faction-skill format");
		if(h.saving)
			validateShape();
		h & battleID;
		h & side;
		h & round;
		h & state;
		validateShape();
	}
};
