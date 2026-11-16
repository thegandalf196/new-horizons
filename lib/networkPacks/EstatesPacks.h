/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once

#include "NetPacksBase.h"
#include "../ResourceSet.h"
#include "../gameState/EstatesState.h"

/// One admitted kingdom update: recruitment payment and its ledger commit
/// together, while an appointment uses an empty payment.
struct DLL_LINKAGE EstatesKingdomChanged : CPackForClient
{
	PlayerColor owner;
	newHorizonsEstates::KingdomState before;
	newHorizonsEstates::KingdomState after;
	ResourceSet payment;
	void visitTyped(ICPackVisitor & visitor) override;

	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_ESTATES))
			throw std::runtime_error("Estates kingdom update requires revised Estates format");
		h & owner;
		h & before;
		h & after;
		h & payment;
	}
};

struct DLL_LINKAGE AssignTownSteward : CPackForServer
{
	ObjectInstanceID town;
	ObjectInstanceID hero = ObjectInstanceID::NONE;
	void visitTyped(ICPackVisitor & visitor) override;

	template<typename Handler> void serialize(Handler & h)
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_ESTATES))
			throw std::runtime_error("Steward appointment requires revised Estates format");
		h & static_cast<CPackForServer &>(*this);
		h & town;
		h & hero;
	}
};
