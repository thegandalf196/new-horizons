/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../json/JsonNode.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include <map>
#include <vector>
#include <array>
#include "../constants/EntityIdentifiers.h"
#include <optional>
#include "CompiledLeadershipRules.h"

namespace nativeRules
{
enum class CapabilityContext { Hero, World };
enum class SiegeOutput { BallistaDamage, CatapultStructuralDamage, FirstAidHealing, DefensiveTowerDamage };

/// A trusted projection of one immutable resolved class capture.
/// Raw JSON entry points keep their full validation contracts.
class DLL_LINKAGE CompiledCapabilityRules final
{
	struct Formula { int base = 0; int coefficientHalf = 0; };
	struct ManaTier { int minimum; int percent; };
	int policy = 0;
	std::optional<CompiledLeadershipRules> leadershipRules;
	std::map<ArtifactID, ManaTier> manaTiers;
	std::optional<newHorizonsHeroes::GlyphsOfFearAura> fearAura;
	std::optional<int> skeletonHealthPercent;
	std::vector<ArtifactID> shopInventory;
	std::map<FactionID, ArtifactID> favoredMachines;
	int shopDiscountPercent = 100;
	int shopRoundingGold = 1;
	std::array<int, 4> ballistaMultiplier{};
	std::array<int, 4> siegeRating{};
	std::optional<SecondarySkill> machineSkill;
	std::array<int, 4> controlChance{};
	std::array<Formula, 4> output{};
public:
	explicit CompiledCapabilityRules(const JsonNode & rules, CapabilityContext context = CapabilityContext::Hero);
	int version() const { return policy; }
	const CompiledLeadershipRules * leadership() const { return leadershipRules ? &*leadershipRules : nullptr; }
	std::optional<int> artifactManaRegeneration(ArtifactID artifact, int maximumMana) const;
	std::optional<newHorizonsHeroes::GlyphsOfFearAura> glyphsOfFearAura() const { return fearAura; }
	std::optional<int> skeletonTransformerHealthPercent() const { return skeletonHealthPercent; }
	const std::vector<ArtifactID> & warMachineShopInventory() const { return shopInventory; }
	std::optional<int> warMachineShopPrice(FactionID faction, ArtifactID artifact, int ordinaryPrice) const;
	bool hasSiege() const { return policy >= 3; }
	int ballistaDamageMultiplier(int rank) const;
	int rating(int rank) const;
	std::optional<SecondarySkill> warMachinesSkill() const { return machineSkill; }
	int directControlChance(int rank) const;
	int siegeOutput(int rating, SiegeOutput kind) const;
};
}
