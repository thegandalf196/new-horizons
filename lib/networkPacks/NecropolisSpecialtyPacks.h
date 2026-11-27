/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NetPacksBase.h"
#include "../constants/EntityIdentifiers.h"
#include "../battle/BattleSide.h"
struct DLL_LINKAGE RecordResidualLife : CPackForClient
{
	BattleID battleID;
	BattleSide side = BattleSide::NONE;
	int64_t damage = 0;
	void visitTyped(ICPackVisitor &) override;
	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_NECROPOLIS_SPECIALTIES))
			throw std::runtime_error("Residual Life requires the Necropolis specialty format");
		h & battleID;
		h & side;
		h & damage;
		if(damage < 0 || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER))
			throw std::runtime_error("Invalid Residual Life receipt");
	}
};
