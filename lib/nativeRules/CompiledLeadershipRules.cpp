/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledLeadershipRules.h"
#include "../entities/hero/NewHorizonsCapabilityRules.h"
#include "../entities/hero/NewHorizonsLeadership.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../GameLibrary.h"
#include "../CCreatureHandler.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include <algorithm>
#include <limits>
#include <stdexcept>
namespace nativeRules
{
namespace
{
constexpr std::array<std::string_view, 10> perkNames{"massedRanks", "veteranCadre", "familiarStandards",
	"foreignLegion", "officerCorps", "missileCommand", "regimentalOrganization", "combinedArms", "grandMarshal", "masterOfLegions"};
CreatureID creature(const std::string & key)
{
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), CreatureID::entityType(), key, true);
	if(!id) throw std::runtime_error("Missing admitted Leadership creature " + key);
	return CreatureID(*id);
}
}
CompiledLeadershipRules::CompiledLeadershipRules(const JsonNode & rules)
{
	if(!newHorizonsHeroes::usesRules(rules)) return;
	version = static_cast<int>(rules["rulesetVersion"].Integer());
	legacy = newHorizonsHeroes::usesLegacyLeadership(rules);
	efficiency = newHorizonsHeroes::usesLeadershipEfficiency(rules);
	resolved = rules.Struct().contains("profile");
	if(!legacy) return;
	if(resolved)
	{
		base = static_cast<int>(rules["profile"]["base"].Integer());
		perLevel = static_cast<int>(rules["profile"]["perLevel"].Integer());
	}
	if(version < 2)
	{
		minimumMovementPercent = static_cast<int>(rules["leadership"]["minimumMovementPercent"].Integer());
		for(size_t i = 0; i < legacySkillBonus.size(); ++i)
			legacySkillBonus[i] = static_cast<int>(rules["leadership"]["skillBonusPercent"][i].Integer());
		return;
	}
	const auto & leadership = rules["leadership"];
	scalePercent = static_cast<int>(leadership["globalScalePercent"].Integer());
	if(efficiency && resolved)
	{
		for(const auto & [key, value] : leadership["resolvedCreatureRequirements"].Struct())
			requirements.emplace(creature(key), static_cast<int>(value.Integer()));
	}
	else
	{
		// Historical lookup keeps explicit overrides before first matching direct upgrade.
		for(const auto & [key, value] : leadership["creatureRequirements"].Struct())
			requirements.emplace(creature(key), static_cast<int>(value.Integer()));
		const int64_t multiplier = leadership["upgradeMultiplierPercent"].Integer();
		const int64_t rounding = leadership["upgradeRounding"].Integer();
		for(const auto & [key, value] : leadership["creatureRequirements"].Struct())
			if(const auto * parent = creature(key).toCreature())
				for(const auto child : parent->upgrades)
					requirements.try_emplace(child, static_cast<int>(((value.Integer() * multiplier + 50LL * rounding) / (100LL * rounding)) * rounding));
	}
	if(!efficiency) return;
	const auto & policy = leadership["efficiencyPolicy"];
	for(size_t rank = 0; rank < ranks.size(); ++rank)
		ranks[rank] = static_cast<int>(policy["authorityRankBonusPercent"][rank].Integer());
	for(size_t i = 0; i < perks.size(); ++i)
		perks[i] = static_cast<int>(policy["authorityPerkBonusPercent"][std::string(perkNames[i])].Integer());
}
newHorizonsHeroes::LeadershipCapacity CompiledLeadershipRules::aggregate(int level, int rank, uint64_t used) const
{
	if(!legacy) return {};
	if(version != 1 || !resolved || rank < 0 || rank > 3)
		throw std::runtime_error("Aggregate Leadership requires resolved v1 and rank NONE..EXPERT");
	return newHorizonsHeroes::leadershipCapacity(base, perLevel, level, legacySkillBonus[rank], used, minimumMovementPercent);
}
int CompiledLeadershipRules::rating(int level) const
{
	if(!legacy) return 0;
	if(version < 2 || !resolved || level < 1) throw std::runtime_error("Per-slot Leadership requires resolved v2+ and positive level");
	const int64_t unscaled = static_cast<int64_t>(base) + static_cast<int64_t>(perLevel) * (level - 1);
	return static_cast<int>(std::min<int64_t>(unscaled * scalePercent / 100, std::numeric_limits<int>::max()));
}
int CompiledLeadershipRules::growthPerLevel() const
{
	if(!legacy) return 0;
	if(version < 2 || !resolved) throw std::runtime_error("Per-slot Leadership requires resolved v2+");
	return static_cast<int>(std::min<int64_t>(static_cast<int64_t>(perLevel) * scalePercent / 100, std::numeric_limits<int>::max()));
}
int CompiledLeadershipRules::requirement(CreatureID id) const
{
	const auto found = requirements.find(id);
	return found == requirements.end() ? 0 : found->second;
}
int CompiledLeadershipRules::authorityRankBonus(int rank) const
{
	return ranks[std::clamp(rank, 0, 3)];
}
int CompiledLeadershipRules::authorityPerkBonus(std::string_view key) const
{
	const auto found = std::ranges::find(perkNames, key);
	return found == perkNames.end() ? 0 : perks[static_cast<size_t>(found - perkNames.begin())];
}
CompiledLeadershipDiscounts::CompiledLeadershipDiscounts(const JsonNode & rules)
{
	necromancy = newHorizonsNecromancy::policy(rules);
	const auto & policy = rules[newHorizonsNecromancy::POLICY_KEY];
	active = necromancy && necromancy->version >= 4;
	if(!active) return;
	for(size_t i = 0; i < skeleton.size(); ++i)
		skeleton[i] = static_cast<int>(policy["skeletonLeadershipReductionPercent"][i].Integer());
	lord = static_cast<int>(policy["lordOfTheDeadLeadershipReductionPercent"].Integer());
}
int CompiledLeadershipDiscounts::discountPercent(int rank, bool lordOfTheDead) const
{
	return active ? (lordOfTheDead ? lord : skeleton[std::clamp(rank, 0, 3)]) : 0;
}
}
