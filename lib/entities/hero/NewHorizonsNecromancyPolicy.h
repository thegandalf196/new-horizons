/*
 * NewHorizonsNecromancyPolicy.h, part of VCMI engine
 * Authors: listed in file AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include "../../constants/EntityIdentifiers.h"

#include <array>
#include <optional>
#include <set>

class CGHeroInstance;
class JsonNode;
namespace newHorizonsHeroes { struct LeadershipSlotCapacity; }

namespace newHorizonsNecromancy
{
inline constexpr const char * POLICY_KEY = "necromancyPolicy";
inline constexpr int POLICY_VERSION = 2;

/// New-game coefficients are captured with the perk identities and tiers.
/// Absence preserves historical conversion perks and undiscounted Leadership.
struct DLL_LINKAGE Policy
{
	int version;
	std::array<int, 4> skeletonLeadershipReductionPercent;
	int lordOfTheDeadLeadershipReductionPercent;
	int deathMarchSpeed;
	int bonewardMagicalReductionPercent;
	int tombGuardAdditionalRetaliations;
	int graveplatePhysicalReductionPercent;
	int graveKnowledgeRecoveryPercent;
	int bonewardMagicResistancePercent = 0;
	bool tombGuardDoubleStrike = false;
	int deadMansLuckBonus = 0;
	int boneCollectorRaiseBonusPercent = 0;
	int soulHarvesterManaPerRaisedCreature = 0;
	int soulHarvesterManaCap = 0;

	bool operator==(const Policy &) const = default;
};

DLL_LINKAGE std::optional<Policy> policy(const JsonNode & capturedPerkRules);
DLL_LINKAGE void validatePolicy(const JsonNode & capturedPerkRules);
DLL_LINKAGE void validatePolicySerialization(const JsonNode & capturedPerkRules, bool supportsV1, bool supportsV2);
DLL_LINKAGE bool isSkeletonFamily(CreatureID creature);
/// Copied direct-upgrade membership, retaining native invalid/base-ID short circuits.
DLL_LINKAGE bool isSkeletonFamily(CreatureID creature, const std::optional<std::set<CreatureID>> & upgrades);
/// Discount once with a wide numerator, round up, and retain a minimum cost of one.
DLL_LINKAGE int discountedLeadershipRequirement(int baseRequirement, int reductionPercent);
DLL_LINKAGE int skeletonLeadershipReductionPercent(const CGHeroInstance * hero, CreatureID creature);
/// Read-only passive queries; the caller supplies the applicable hero/controller.
DLL_LINKAGE int deathMarchSpeed(const CGHeroInstance * hero, CreatureID creature);
DLL_LINKAGE int bonewardMagicalReductionPercent(const CGHeroInstance * hero, CreatureID creature);
DLL_LINKAGE int tombGuardAdditionalRetaliations(const CGHeroInstance * hero, CreatureID creature);
DLL_LINKAGE int bonewardMagicResistancePercent(const CGHeroInstance * hero, CreatureID creature);
DLL_LINKAGE bool tombGuardDoubleStrike(const CGHeroInstance * hero, CreatureID creature);
DLL_LINKAGE int deadMansLuckBonus(const CGHeroInstance * hero, CreatureID creature);
DLL_LINKAGE int graveplatePhysicalReductionPercent(const CGHeroInstance * hero, CreatureID creature);
/// Own-army Skeleton recovery rate; the result resolver owns casualty eligibility.
DLL_LINKAGE int graveKnowledgeRecoveryPercent(const CGHeroInstance * hero);
/// Apply only to a validated hero slot projection after flat Leadership bonuses.
/// This changes its effective price/maximum, never the hero rating or army.
DLL_LINKAGE void applyLeadershipPolicy(newHorizonsHeroes::LeadershipSlotCapacity & capacity,
	const CGHeroInstance & hero, CreatureID creature);
}
