/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once

#include "../ResourceSet.h"

class CGHeroInstance;

namespace newHorizonsEstates
{
struct DLL_LINKAGE ArtifactPurchaseQuote
{
	ResourceSet cost;
	int32_t week = -1;
	bool usesHoarder = false;
};

DLL_LINKAGE void validateHoarderReceipt(int32_t lastWeek, bool supported);
DLL_LINKAGE TResource hoarderGoldCost(TResource baseGold, bool eligible, int32_t lastWeek, int32_t currentWeek);
/// A prospective reservation suppresses Hoarder without changing the live weekly receipt.
DLL_LINKAGE ArtifactPurchaseQuote artifactPurchaseQuote(const ResourceSet & baseCost, const CGHeroInstance * hero, bool hoarderReserved = false);
}
