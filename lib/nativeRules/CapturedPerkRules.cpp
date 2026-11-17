/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CapturedPerkRules.h"
#include "../network/InitializationProfile.h"
#include "../entities/hero/NewHorizonsPerkRules.h"
#include "../entities/hero/LegacyPerkPresentation.h"
#include "../entities/hero/CompiledPerkCatalogue.h"

namespace nativeRules
{
CapturedPerkRules::CapturedPerkRules(JsonNode document)
	: coldDocument(std::move(document))
{
	nhLoadProfile::Scope measured(nhLoadProfile::Metric::NativePrepare);
	newHorizonsHeroes::normalizeLegacyHeroProcText(coldDocument);
	admission = std::make_shared<const newHorizonsHeroes::ValidatedPerkRules>(coldDocument, native);
	compiled = std::make_shared<const newHorizonsHeroes::CompiledPerkCatalogue>(*admission);
	leadership.emplace(coldDocument);
	metamagicRules.emplace(*this);
}

std::shared_ptr<const CapturedPerkRules> CapturedPerkRules::compile(JsonNode document)
{
	return std::shared_ptr<const CapturedPerkRules>(new CapturedPerkRules(std::move(document)));
}
}
