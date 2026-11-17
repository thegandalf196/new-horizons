/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CompiledDevelopmentRules.h"
#include "../GameLibrary.h"
#include "../CSkillHandler.h"
#include "../modding/IdentifierStorage.h"
#include "../modding/ModScope.h"
#include <algorithm>
#include <stdexcept>

namespace nativeRules
{
namespace
{
int resolve(const std::string & type, const std::string & key)
{
	const auto id = LIBRARY->identifiers()->getIdentifier(ModScope::scopeGame(), type, key, true);
	if(!id || *id < 0) throw std::runtime_error("Missing admitted development identity " + key);
	return *id;
}
}
const CompiledDevelopmentRules & CompiledDevelopmentRules::absent()
{
	static const CompiledDevelopmentRules empty{JsonNode()};
	return empty;
}
CompiledDevelopmentRules::CompiledDevelopmentRules(const JsonNode & rules, DevelopmentContext context)
{
	if(context == DevelopmentContext::World) newHorizonsHeroes::validateHeroRules(rules, false);
	else newHorizonsHeroes::validateResolvedHeroRules(rules);
	enabled = newHorizonsHeroes::usesRules(rules);
	if(!enabled) return;
	divisor = static_cast<int>(rules["powerDivisor"].Integer());
	maximum = static_cast<int>(rules["maxPrimary"].Integer());
	movementPerLevel = static_cast<int>(rules["dailyMovement"]["pointsPerLevel"].Integer());
	if(rules["lighthouseDeparture"]["enabled"].Bool())
		lighthouseDeparture = static_cast<int>(rules["lighthouseDeparture"]["seaMovementPercent"].Integer());
	if(context == DevelopmentContext::World)
	{
		for(const auto & [key, row] : rules["classProfiles"].Struct())
			classes.emplace(HeroClassID(resolve(HeroClassID::entityType(), key)), newHorizonsHeroes::parsePrimaryProfile(row));
		return;
	}
	primary = newHorizonsHeroes::parsePrimaryProfile(rules["profile"]);
	for(int id = 0; id < LIBRARY->skillh->size(); ++id)
		knownSkills.insert(SecondarySkill::encode(id));
	weightedOffers = newHorizonsHeroes::usesSkillOfferWeights(rules);
	splitOffers = newHorizonsHeroes::usesSplitSkillOffers(rules);
	navigationStart = newHorizonsHeroes::usesNavigationStartReplacement(rules);
	if(weightedOffers)
		for(const auto & [key, row] : rules["skillOfferWeights"].Struct())
		{
			const SecondarySkill skill(resolve(SecondarySkill::entityType(), key));
			if(SecondarySkill::encode(skill.getNum()) != key)
				throw std::runtime_error("Captured offer weight requires exact scoped identity");
			weights.emplace(skill, static_cast<int>(row.Integer()));
			namedWeights.emplace(key, static_cast<int>(row.Integer()));
		}
	for(const auto & key : rules["excludedSkills"].Vector())
		excluded.emplace(resolve(SecondarySkill::entityType(), key.String()));
	if(primary->progressionVersion == newHorizonsHeroes::PRIMARY_PROFILE_VERSION_TWENTY_POINT)
		for(const auto & row : rules["extraGrowth"].Vector())
		{
			GrowthRow compiled{SecondarySkill(resolve(SecondarySkill::entityType(), row["skill"].String())),
				PrimarySkill(row["primary"].Integer()), {}};
			for(size_t rank = 0; rank < compiled.chance.size(); ++rank)
				compiled.chance[rank] = static_cast<int>(row["chances"][rank].Integer());
			growth.push_back(compiled);
		}
	compileSpecialties(rules);
	compileCombatPolicies(rules);
	compileSkillPolicies(rules);
}
std::optional<newHorizonsHeroes::PrimaryProfile> CompiledDevelopmentRules::classProfile(HeroClassID id) const
{
	const auto found = classes.find(id);
	return found == classes.end() ? std::nullopt : std::optional(found->second);
}
std::optional<int> CompiledDevelopmentRules::skillOfferWeight(SecondarySkill skill) const
{
	const auto found = weights.find(skill);
	return found == weights.end() ? std::nullopt : std::optional(found->second);
}
std::optional<int> CompiledDevelopmentRules::skillOfferWeight(const std::string & skill) const
{
	const auto found = namedWeights.find(skill);
	// Unknown external names preserve the existing callback's explicit rejection.
	if(!knownSkills.contains(skill)) throw std::runtime_error("Perk parent skill identity does not match its captured weight table");
	return found == namedWeights.end() ? std::nullopt : std::optional(found->second);
}
std::vector<newHorizonsHeroes::SkillGrowthChance> CompiledDevelopmentRules::skillGrowthChances(
	const std::function<int(SecondarySkill)> & rank) const
{
	std::vector<newHorizonsHeroes::SkillGrowthChance> result;
	for(const auto & row : growth)
	{
		const int value = rank(row.skill);
		if(value < 0 || value > 3) throw std::runtime_error("Invalid skill rank for primary growth");
		if(row.chance[value] > 0) result.push_back({row.skill, row.attribute, row.chance[value]});
	}
	return result;
}
int CompiledDevelopmentRules::skillSpecialtyCoreBonusPercent(SecondarySkill skill) const
{
	return skillSpecialty && std::ranges::find(skillSpecialty->skills, skill) != skillSpecialty->skills.end()
		? skillSpecialty->coreBonusPercent : 0;
}
}
