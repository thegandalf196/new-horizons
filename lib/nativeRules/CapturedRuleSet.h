/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CapturedPerkRules.h"
#include "CompiledCapabilityRules.h"
#include "CompiledDevelopmentRules.h"
#include "CompiledMasteryRules.h"
#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>

namespace nativeRules
{
class CompiledMetamagicEnergy;
using CapturedDocumentId = uint32_t;
using CapturedRuleSetId = uint32_t;
constexpr CapturedDocumentId NO_CAPTURED_DOCUMENT = 0;
constexpr CapturedRuleSetId NO_CAPTURED_RULE_SET = 0;

enum class CapturedRuleKind
{
	Mastery,
	Perks,
	ResolvedCapability,
	ResolvedDevelopment,
	WorldCapability,
	WorldDevelopment,
	WorldMagic
};

/// Immutable document ownership; components preserve independent capture policies.
class DLL_LINKAGE CapturedRuleDocument final
{
	CapturedRuleKind type;
	JsonNode coldDocument;
	std::shared_ptr<const CapturedPerkRules> perkRules;
	std::shared_ptr<const CompiledCapabilityRules> capabilityRules;
	std::shared_ptr<const CompiledDevelopmentRules> developmentRules;
	std::shared_ptr<const CompiledMasteryRules> masteryRules;
	std::shared_ptr<const CompiledMetamagicEnergy> magicEnergy;
	explicit CapturedRuleDocument(CapturedRuleKind kind, JsonNode document);
	explicit CapturedRuleDocument(std::shared_ptr<const CapturedPerkRules> perks);
public:
	static std::shared_ptr<const CapturedRuleDocument> compile(CapturedRuleKind kind, JsonNode document);
	static std::shared_ptr<const CapturedRuleDocument> wrapPerks(std::shared_ptr<const CapturedPerkRules> perks);
	CapturedRuleKind kind() const { return type; }
	const JsonNode & document() const { return perkRules ? perkRules->document() : coldDocument; }
	const std::shared_ptr<const CapturedPerkRules> & perks() const { return perkRules; }
	const std::shared_ptr<const CompiledCapabilityRules> & capabilities() const { return capabilityRules; }
	const std::shared_ptr<const CompiledDevelopmentRules> & development() const { return developmentRules; }
	const std::shared_ptr<const CompiledMasteryRules> & mastery() const { return masteryRules; }
	const std::shared_ptr<const CompiledMetamagicEnergy> & metamagicEnergy() const { return magicEnergy; }
};

/// Share only exact source identity, including nested scopes/types/override bits.
/// A custom callback disagreement is admitted from its raw capture at this boundary.
DLL_LINKAGE std::shared_ptr<const CapturedRuleDocument> captureMagicRules(
	std::shared_ptr<const CapturedRuleDocument> owner, const JsonNode & raw);

/// A world-local identity. IDs have meaning only in the owning pool.
/// Hero selections, skill ranks and mutable bonus instances are never shared.
struct DLL_LINKAGE CapturedRuleSet final
{
	std::array<CapturedDocumentId, 4> documents{}; // mastery, perks, capability, development
	bool operator==(const CapturedRuleSet &) const = default;
	template<typename Handler> void serialize(Handler & h)
	{
		for(auto & id : documents)
		{
			int64_t wire = h.saving ? id : 0;
			h & wire;
			if(wire <= 0 || wire > 100000) throw std::runtime_error("Invalid captured component identity");
			if(!h.saving) id = static_cast<CapturedDocumentId>(wire);
		}
	}
};
}
