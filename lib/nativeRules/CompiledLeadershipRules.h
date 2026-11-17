/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include "../entities/hero/NewHorizonsNecromancyPolicy.h"
#include <array>
#include <map>
#include <optional>
#include <string_view>
class JsonNode;
namespace newHorizonsHeroes { struct LeadershipCapacity; }
namespace nativeRules
{
/// Compiled only after owning capability document admission. Mutable rank,
/// selected perks and army composition are explicit inputs at each operation.
class DLL_LINKAGE CompiledLeadershipRules final
{
	int version = 0;
	bool legacy = false;
	bool efficiency = false;
	bool resolved = false;
	int base = 0;
	int perLevel = 0;
	int scalePercent = 100;
	int minimumMovementPercent = 100;
	std::array<int, 4> legacySkillBonus{};
	std::map<CreatureID, int> requirements;
	std::array<int, 4> ranks{};
	std::array<int, 10> perks{};
public:
	explicit CompiledLeadershipRules(const JsonNode & admittedCapability);
	bool usesEfficiency() const { return efficiency; }
	bool usesLegacy() const { return legacy; }
	bool usesPerSlotRating() const { return version >= 2; }
	newHorizonsHeroes::LeadershipCapacity aggregate(int level, int rank, uint64_t used) const;
	int rating(int level) const;
	int growthPerLevel() const;
	int requirement(CreatureID) const;
	int authorityRankBonus(int rank) const;
	int authorityPerkBonus(std::string_view) const;
};
/// Source is the captured perk profile, independent of capability identities.
class DLL_LINKAGE CompiledLeadershipDiscounts final
{
	bool active = false;
	std::optional<newHorizonsNecromancy::Policy> necromancy;
	std::array<int, 4> skeleton{};
	int lord = 0;
public:
	explicit CompiledLeadershipDiscounts(const JsonNode & admittedPerks);
	const std::optional<newHorizonsNecromancy::Policy> & necromancyPolicy() const { return necromancy; }
	int discountPercent(int rank, bool lordOfTheDead) const;
};
}
