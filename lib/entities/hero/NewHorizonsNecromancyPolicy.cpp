/*
 * NewHorizonsNecromancyPolicy.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in the main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsNecromancyPolicy.h"
#include "NewHorizonsCapabilityRules.h"
#include "NewHorizonsNecromancy.h"
#include "NewHorizonsRuleValidation.h"
#include "../../CCreatureHandler.h"
#include "../../mapObjects/CGHeroInstance.h"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace newHorizonsNecromancy
{
namespace
{
Policy readPolicy(const JsonNode & row)
{
	Policy result{};
	result.version = static_cast<int>(row["version"].Integer());
	for(size_t rank = 0; rank < result.skeletonLeadershipReductionPercent.size(); ++rank)
		result.skeletonLeadershipReductionPercent[rank] = static_cast<int>(row["skeletonLeadershipReductionPercent"].Vector()[rank].Integer());
	result.lordOfTheDeadLeadershipReductionPercent = static_cast<int>(row["lordOfTheDeadLeadershipReductionPercent"].Integer());
	result.deathMarchSpeed = static_cast<int>(row["deathMarchSpeed"].Integer());
	result.bonewardMagicalReductionPercent = static_cast<int>(row["bonewardMagicalReductionPercent"].Integer());
	result.tombGuardAdditionalRetaliations = static_cast<int>(row["tombGuardAdditionalRetaliations"].Integer());
	result.graveplatePhysicalReductionPercent = static_cast<int>(row["graveplatePhysicalReductionPercent"].Integer());
	result.graveKnowledgeRecoveryPercent = static_cast<int>(row["graveKnowledgeRecoveryPercent"].Integer());
	if(result.version >= 2)
	{
		result.bonewardMagicResistancePercent = static_cast<int>(row["bonewardMagicResistancePercent"].Integer());
		result.tombGuardDoubleStrike = row["tombGuardDoubleStrike"].Bool();
		result.deadMansLuckBonus = static_cast<int>(row["deadMansLuckBonus"].Integer());
		result.boneCollectorRaiseBonusPercent = static_cast<int>(row["boneCollectorRaiseBonusPercent"].Integer());
		result.soulHarvesterManaPerRaisedCreature = static_cast<int>(row["soulHarvesterManaPerRaisedCreature"].Integer());
		result.soulHarvesterManaCap = static_cast<int>(row["soulHarvesterManaCap"].Integer());
	}
	return result;
}
}

std::optional<Policy> policy(const JsonNode & capturedPerkRules)
{
	if(!capturedPerkRules.isStruct() || !capturedPerkRules.Struct().contains(POLICY_KEY))
		return std::nullopt;

	using namespace newHorizonsHeroes::detail;
	const auto & row = capturedPerkRules[POLICY_KEY];
	fields(row, {"version", "skeletonLeadershipReductionPercent",
		"lordOfTheDeadLeadershipReductionPercent", "deathMarchSpeed",
		"bonewardMagicalReductionPercent", "tombGuardAdditionalRetaliations",
		"graveplatePhysicalReductionPercent", "graveKnowledgeRecoveryPercent",
		"bonewardMagicResistancePercent", "tombGuardDoubleStrike", "deadMansLuckBonus",
		"boneCollectorRaiseBonusPercent", "soulHarvesterManaPerRaisedCreature", "soulHarvesterManaCap"});
	require(integer(row["version"], 1, POLICY_VERSION), "Necromancy policy version");
	if(row["version"].Integer() == 1)
	{
		for(const auto * key : {"bonewardMagicResistancePercent", "tombGuardDoubleStrike", "deadMansLuckBonus",
			"boneCollectorRaiseBonusPercent", "soulHarvesterManaPerRaisedCreature", "soulHarvesterManaCap"})
			require(!row.Struct().contains(key), "Historical Necromancy policy fields");
	}
	else
	{
		require(row["tombGuardDoubleStrike"].isBool(), "Tomb Guard Double Strike");
		for(const auto * key : {"bonewardMagicResistancePercent", "boneCollectorRaiseBonusPercent",
			"soulHarvesterManaPerRaisedCreature"})
			require(integer(row[key], 0, 100), key);
		require(integer(row["deadMansLuckBonus"], 0, 10), "Dead Man's Luck bonus");
		require(integer(row["soulHarvesterManaCap"], 0, 1000000), "Soul Harvester Mana cap");
		require(integer(row["bonewardMagicalReductionPercent"], 0, 0)
			&& integer(row["tombGuardAdditionalRetaliations"], 0, 0), "Retired Necromancy passive coefficients");
	}
	const auto & reductions = row["skeletonLeadershipReductionPercent"];
	require(reductions.isVector() && reductions.Vector().size() == 4,
		"Necromancy requires four rank Leadership reductions");
	for(const auto & value : reductions.Vector())
	{
		require(integer(value, 0, 99), "Necromancy Leadership reduction");
	}
	require(reductions.Vector().front().Integer() == 0,
		"Necromancy unlearned rank must not reduce Leadership");
	require(integer(row["lordOfTheDeadLeadershipReductionPercent"], 0, 99),
		"Lord of the Dead Leadership reduction");
	require(integer(row["deathMarchSpeed"], 0, 100), "Death March Speed");
	require(integer(row["bonewardMagicalReductionPercent"], 0, 100),
		"Boneward magical reduction");
	require(integer(row["tombGuardAdditionalRetaliations"], 0, 100),
		"Tomb Guard additional retaliations");
	require(integer(row["graveplatePhysicalReductionPercent"], 0, 100),
		"Graveplate physical reduction");
	require(integer(row["graveKnowledgeRecoveryPercent"], 0, 100),
		"Grave Knowledge own Skeleton recovery");
	return readPolicy(row);
}

void validatePolicy(const JsonNode & capturedPerkRules)
{
	(void)policy(capturedPerkRules);
}

void validatePolicySerialization(const JsonNode & capturedPerkRules, bool supportsV1, bool supportsV2)
{
	const auto captured = policy(capturedPerkRules);
	if(captured && (!supportsV1 || (captured->version >= 2 && !supportsV2)))
		throw std::runtime_error("Cannot discard captured Necromancy policy");
}

bool isSkeletonFamily(CreatureID creature)
{
	if(!creature.hasValue())
		return false;
	const CreatureID skeleton(CreatureID::SKELETON);
	if(creature == skeleton)
		return true;
	const auto * base = skeleton.toCreature();
	return base && base->upgrades.contains(creature);
}

int discountedLeadershipRequirement(int baseRequirement, int reductionPercent)
{
	if(baseRequirement < 1 || reductionPercent < 0 || reductionPercent >= 100)
		throw std::invalid_argument("Invalid Necromancy Leadership discount");
	const int64_t numerator = static_cast<int64_t>(baseRequirement) * (100 - reductionPercent);
	return static_cast<int>(std::max<int64_t>(1, (numerator + 99) / 100));
}

namespace
{
std::optional<Policy> activePolicy(const CGHeroInstance * hero, CreatureID creature)
{
	if(!hero || !isSkeletonFamily(creature) || hero->getNewHorizonsNecromancyRank() == 0)
		return std::nullopt;
	const auto & rules = hero->getPerkState().rules;
	if(!rules.isStruct() || !rules.Struct().contains(POLICY_KEY))
		return std::nullopt;
	// Captured perk rules validate on initialization, selection and load. Combat
	// queries borrow that invariant instead of revalidating the registry per hit.
	return readPolicy(rules[POLICY_KEY]);
}

int leadershipReduction(const CGHeroInstance & hero, const Policy & captured)
{
	return hero.hasActivePerk(SKILL_ID, LORD_OF_THE_DEAD_ID)
		? captured.lordOfTheDeadLeadershipReductionPercent
		: captured.skeletonLeadershipReductionPercent[hero.getNewHorizonsNecromancyRank()];
}
}

int skeletonLeadershipReductionPercent(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	if(!captured)
		return 0;
	return leadershipReduction(*hero, *captured);
}

int deathMarchSpeed(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	return captured && hero->hasActivePerk(SKILL_ID, CORPSE_PRESERVATION_ID)
		? captured->deathMarchSpeed : 0;
}

int bonewardMagicalReductionPercent(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	return captured && hero->hasActivePerk(SKILL_ID, DARK_CONVERSION_ID)
		? captured->bonewardMagicalReductionPercent : 0;
}

int tombGuardAdditionalRetaliations(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	return captured && hero->hasActivePerk(SKILL_ID, SOUL_HARVESTER_ID)
		? captured->tombGuardAdditionalRetaliations : 0;
}

int bonewardMagicResistancePercent(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	return captured && hero->hasActivePerk(SKILL_ID, DARK_CONVERSION_ID)
		? captured->bonewardMagicResistancePercent : 0;
}

bool tombGuardDoubleStrike(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	return captured && hero->hasActivePerk(SKILL_ID, SOUL_HARVESTER_ID)
		? captured->tombGuardDoubleStrike : false;
}

int deadMansLuckBonus(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	return captured && hero->hasActivePerk(SKILL_ID, OSSUARY_ID)
		? captured->deadMansLuckBonus : 0;
}

int graveplatePhysicalReductionPercent(const CGHeroInstance * hero, CreatureID creature)
{
	const auto captured = activePolicy(hero, creature);
	return captured && hero->hasActivePerk(SKILL_ID, DEATH_LORD_ID)
		? captured->graveplatePhysicalReductionPercent : 0;
}

int graveKnowledgeRecoveryPercent(const CGHeroInstance * hero)
{
	const auto captured = activePolicy(hero, CreatureID(CreatureID::SKELETON));
	return captured && hero->hasActivePerk(SKILL_ID, GRAVE_KNOWLEDGE_ID)
		? captured->graveKnowledgeRecoveryPercent : 0;
}

void applyLeadershipPolicy(newHorizonsHeroes::LeadershipSlotCapacity & capacity,
	const CGHeroInstance & hero, CreatureID creature)
{
	const auto captured = activePolicy(&hero, creature);
	if(!captured)
		return;
	// Both forms use the captured base-family price, even if a map authored an
	// explicit upgrade row. Historical snapshots without this policy are untouched.
	const auto & requirements = hero.getCapabilityRules()["leadership"]["creatureRequirements"];
	const auto canonical = requirements.Struct().find("core:skeleton");
	// The ordinary slot resolver already validated this captured table. Avoid a
	// second full-table validation for its canonical base row on every AI query.
	const int base = canonical != requirements.Struct().end()
		? static_cast<int>(canonical->second.Integer())
		: newHorizonsHeroes::capabilityCreatureLeadershipRequirement(
			hero.getCapabilityRules(), CreatureID(CreatureID::SKELETON));
	if(base <= 0)
		return;
	capacity.requirement = discountedLeadershipRequirement(base,
		leadershipReduction(hero, *captured));
	capacity.maximum = capacity.leadership / capacity.requirement;
}
}
