/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../json/JsonNode.h"
#include "PreparedPilotRules.h"
#include "CompiledLeadershipRules.h"
#include "CompiledMetamagicRules.h"
#include <memory>

namespace newHorizonsHeroes { class CompiledPerkCatalogue; class ValidatedPerkRules; }
namespace nativeRules
{
/// An admitted, immutable catalogue. Acquisition and rank facts belong to heroes.
class DLL_LINKAGE CapturedPerkRules final
{
	JsonNode coldDocument;
	std::optional<CompiledLeadershipDiscounts> leadership;
	std::optional<CompiledMetamagicRules> metamagicRules;
	std::shared_ptr<const PreparedPilotPerks> native;
	std::shared_ptr<const newHorizonsHeroes::CompiledPerkCatalogue> compiled;
	std::shared_ptr<const newHorizonsHeroes::ValidatedPerkRules> admission;
	explicit CapturedPerkRules(JsonNode document);
public:
	static std::shared_ptr<const CapturedPerkRules> compile(JsonNode document);
	const JsonNode & document() const { return coldDocument; }
	const CompiledLeadershipDiscounts & leadershipDiscounts() const { return *leadership; }
	const CompiledMetamagicRules & metamagic() const { return *metamagicRules; }
	std::shared_ptr<const PreparedPilotPerks> prepared() const { return native; }
	const newHorizonsHeroes::CompiledPerkCatalogue & catalogue() const { return *compiled; }
	const newHorizonsHeroes::ValidatedPerkRules & admitted() const { return *admission; }
};
}
