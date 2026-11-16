/* Part of VCMI engine; GNU General Public License v2.0 or later. */
#pragma once

#include "../lib/mapObjects/EstatesRecruitmentQuote.h"
#include "../lib/mapObjects/EstatesArtifactQuote.h"
#include <optional>

class CGameHandler;
class CGDwelling;
class CArmedInstance;
class CGHeroInstance;

namespace recruitmentCommit
{
struct Payment
{
	PlayerColor owner;
	ResourceSet cost;
	std::optional<newHorizonsEstates::RecruitmentQuote> ledger;
	std::optional<newHorizonsEstates::ArtifactPurchaseQuote> artifact;
};
Payment prepare(const CGDwelling & source, const CArmedInstance & destination,
	const CGHeroInstance * buyer, CreatureID creature, int32_t count);
void pay(CGameHandler & handler, const Payment & admitted);
bool grantWarMachine(CGameHandler & handler, const CGHeroInstance * hero, ArtifactID artifact, const Payment & admitted);
}
