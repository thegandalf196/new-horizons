/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "OwnedUnitEnvironment.h"
#include "../bonuses/OwnedBonusPayloads.h"
namespace battle::ownedFrameCapture
{
/// INTERNAL synchronous identity capture under the actual source/library lease.
/// No spell/node pointer escapes; finite raw admission precedes identifier reads.
DLL_LINKAGE OwnedUnitEnvironment::Facts::LegacyTimeStopIdentity captureLegacyTimeStopIdentities(
	const std::vector<std::shared_ptr<const Bonus>> & roots);
}
