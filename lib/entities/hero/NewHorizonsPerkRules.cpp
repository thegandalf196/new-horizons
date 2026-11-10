/*
 * NewHorizonsPerkRules.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "../../nativeRules/PreparedPilotRules.h"
#include "NewHorizonsPerkRules.h"
#include "../../battle/NewHorizonsElementalAttunement.h"
#include "../../battle/NewHorizonsWarfare.h"
#include "NewHorizonsNecromancyPolicy.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsRuleValidation.h"

#include <array>
#include <cmath>
#include <cctype>
#include <set>
#include <stdexcept>

namespace newHorizonsHeroes
{
bool crisisCommandProfileActive(const JsonNode &)
{
	return false;
}

void validateCrisisCommandProfileSerialization(const JsonNode & rules, bool supported)
{
	if(crisisCommandProfileActive(rules) && !supported)
		throw std::runtime_error("Cannot serialize active Crisis Command profile to an older format");
}

namespace
{
void require(bool valid, const std::string & detail)
{
	if(!valid)
		throw std::runtime_error("Invalid New Horizons perk rules: " + detail);
}

void fields(const JsonNode & node, std::initializer_list<std::string_view> allowed)
{
	require(node.isStruct(), "object required");
	for(const auto & [key, value] : node.Struct())
		require(std::find(allowed.begin(), allowed.end(), key) != allowed.end(), "unknown field " + key);
}

bool integer(const JsonNode & node, int minimum, int maximum)
{
	return node.isNumber() && std::isfinite(node.Float()) && node.Float() >= minimum && node.Float() <= maximum
		&& std::floor(node.Float()) == node.Float();
}

bool scopedId(std::string_view id)
{
	const auto separator = id.find(':');
	if(separator == std::string_view::npos || separator == 0 || separator + 1 >= id.size()
		|| id.find(':', separator + 1) != std::string_view::npos)
		return false;
	for(size_t i = 0; i < id.size(); ++i)
	{
		if(i == separator)
			continue;
		const auto c = static_cast<unsigned char>(id[i]);
		if(i == 0 || (i == separator + 1))
		{
			if(!std::islower(c))
				return false;
		}
		else if(i < separator)
		{
			if(!(std::islower(c) || std::isdigit(c) || c == '_' || c == '-' || c == '.'))
				return false;
		}
		else if(!(std::isalnum(c) || c == '_' || c == '-' || c == '.'))
			return false;
	}
	return true;
}

void validateEffectParameters(const JsonNode & parameters)
{
	fields(parameters, {"commandBonus", "orderEfficiencyPercent", "attackEfficiencyBonusPP",
		"defenseEfficiencyBonusPP", "firstOrderEfficiencyBonusPP", "allEfficiencyBonusPP",
		"efficiencyBonusPPPerMorale", "maximumEfficiencyBonusPP", "creatureAttack", "creatureDefense",
		"maximumHealthPercent", "initiative", "battlefieldSpeed", "additionalRetaliations",
		"magicalReductionPercent", "directAttackDamagePercent", "attackAndSpellReductionPercent",
		"focusFireMeleeBonusPercent", "feintReductionBonusPP", "feintReductionCapPercent",
		"slotGrades", "requiresActiveOrder"});
	require(!parameters.Struct().empty(), "nonempty effect parameters");
	for(const auto & [key, value] : parameters.Struct())
	{
		if(key == "requiresActiveOrder")
			require(value.isBool() && value.Bool(), "requiresActiveOrder must be true");
		else if(key == "slotGrades")
		{
			require(value.isVector() && !value.Vector().empty() && value.Vector().size() <= 3, "slotGrades");
			std::set<std::string> grades;
			for(const auto & grade : value.Vector())
			{
				require(grade.isString() && (grade.String() == "core" || grade.String() == "elite"
					|| grade.String() == "champion"), "slot grade");
				require(grades.insert(grade.String()).second, "duplicate slot grade");
			}
		}
		else
			require(value.getType() == JsonNode::JsonType::DATA_INTEGER && integer(value, 0, 1000000),
				"nonnegative integer effect parameter " + key);
	}
}

void validateEffect(const JsonNode & effect, const std::string & context)
{
	fields(effect, {"status", "description", "parameters"});
	if(effect.Struct().contains("parameters"))
		validateEffectParameters(effect["parameters"]);
	require(effect["status"].isString() && (effect["status"].String() == "active"
		|| effect["status"].String() == "planned"), context + " effect status");
	require(effect["description"].isString() && !effect["description"].String().empty(), context + " effect description");
}

void validateDetails(const JsonNode & node, const std::string & context)
{
	if(node.Struct().contains("details"))
		require(node["details"].isString() && !node["details"].String().empty(), context + " nonempty details");
}

void validateRank(const JsonNode & rank, const std::string & context)
{
	fields(rank, {"description", "details", "effect", "mechanics"});
	validateDetails(rank, context);
	require(rank["description"].isString() && !rank["description"].String().empty(), context + " description");
	validateEffect(rank["effect"], context);
}

void validatePerk(const JsonNode & node, const std::string & skillId, const std::string & context,
	std::set<std::string> & identities)
{
	fields(node, {"id", "name", "requires", "description", "details", "effect", "mechanics", "legacyIdentity"});
	if(node.Struct().contains("legacyIdentity"))
	{
		const auto & legacy = node["legacyIdentity"];
		fields(legacy, {"id", "commandPolicyVersion"});
		require(node["id"].isString() && node["id"].String() == "new-horizons:command.decisiveOrders"
			&& legacy["id"].isString() && legacy["id"].String() == "new-horizons:command.seizeInitiative"
			&& legacy["commandPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& legacy["commandPolicyVersion"].Integer() == 1, "released Grand Marshal identity mapping");
	}
	validateDetails(node, context);
	for(const auto * key : {"id", "name", "requires", "description"})
		require(node[key].isString() && !node[key].String().empty(), context + " nonempty " + key);
	require(scopedId(node["id"].String()), context + " scoped id");
	require(node["id"].String().starts_with(skillId + "."), context + " must belong to its skill");
	require(identities.insert(node["id"].String()).second, context + " duplicate id");
	require(node["requires"].String() == "basic" || node["requires"].String() == "advanced"
		|| node["requires"].String() == "expert", context + " prerequisite");
	validateEffect(node["effect"], context);
}

void parsePerk(const JsonNode & node, PerkDefinition & result)
{
	result.id = node["id"].String();
	result.name = node["name"].String();
	result.requiredRank = node["requires"].String();
	result.description = node["description"].String();
	result.details = node["details"].isString() ? node["details"].String() : std::string{};
	result.effect = node["effect"];
}

void parseSkill(const JsonNode & node, SkillDefinition & result)
{
	result.id = node["id"].String();
	result.name = node["name"].String();
	result.domain = node["domain"].String();
	result.availability = node["availability"].String();
	result.description = node["description"].String();
	result.details = node["details"].isString() ? node["details"].String() : std::string{};
	result.ranks = node["ranks"];
	result.perks.clear();
	for(const auto & entry : node["perks"].Vector())
	{
		PerkDefinition perk;
		parsePerk(entry, perk);
		result.perks.push_back(std::move(perk));
	}
}
}

bool usesPerkRules(const JsonNode & rules)
{
	return !rules.isNull() && !(rules.isStruct() && rules.Struct().empty());
}

bool usesCommandAuthorityPolicy(const JsonNode & rules)
{
	if(!rules.isStruct() || !rules.Struct().contains("commandAuthorityPolicy"))
		return false;
	const auto & policy = rules["commandAuthorityPolicy"];
	fields(policy, {"version"});
	require(policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& policy["version"].Integer() == 1, "commandAuthorityPolicy version");
	return true;
}

size_t canonicalSkillCount(const JsonNode & rules)
{
	return usesCommandAuthorityPolicy(rules) ? PERK_CANONICAL_SKILL_COUNT : PERK_LEGACY_SKILL_COUNT;
}

void validateSkillOfferWeightRow(const JsonNode & classWeights, const JsonNode & rules)
{
	require(classWeights.isStruct() && classWeights.Struct().size() == canonicalSkillCount(rules),
		"complete policy-specific skill offer table required");
	require(classWeights.Struct().contains("new-horizons:authority") == usesCommandAuthorityPolicy(rules),
		"Authority offer row requires the Command/Authority policy");

	std::set<int> seenSkills;
	for(const auto & [skill, weight] : classWeights.Struct())
	{
		const auto skillId = detail::resolve(SecondarySkill::entityType(), skill);
		require(seenSkills.insert(skillId).second, "duplicate skill offer skill");
		require(integer(weight, 0, 100), "skill offer weight");
		if(skill == "new-horizons:authority")
			require(integer(weight, 1, 100), "Authority must be offerable to every captured class");
	}
}

int perkRequiredRank(std::string_view prerequisite)
{
	if(prerequisite == "basic")
		return 1;
	if(prerequisite == "advanced")
		return 2;
	if(prerequisite == "expert")
		return 3;
	throw std::runtime_error("Unknown New Horizons perk prerequisite");
}

std::optional<int> quickStudyRerollInterval(const JsonNode & rules)
{
	if(!rules.isStruct() || !rules.Struct().contains("connoisseurPolicy"))
		return std::nullopt;
	const auto & policy = rules["connoisseurPolicy"];
	fields(policy, {"version"});
	require(integer(policy["version"], 1, 1), "Connoisseur policy version");
	return 1;
}

void validatePassiveFactionPolicies(const JsonNode & rules)
{
	if(!rules.isStruct())
		return;
	for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
	{
		const auto found = rules.Struct().find(key);
		if(found == rules.Struct().end())
			continue;
		const auto & policy = found->second;
		const bool metamagic = std::string_view(key) == "metamagicPolicy";
		if(metamagic && policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& policy["version"].Integer() == 2)
			fields(policy, {"version", "adeptAdditionalMarks"});
		else if(metamagic)
			fields(policy, {"version", "adeptAdditionalPoints"});
		else
			fields(policy, {"version"});
		require(policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& (policy["version"].Integer() == 2 || (metamagic && policy["version"].Integer() == 3)),
			std::string(key) + " has an unsupported version");
		if(metamagic && policy.Struct().contains("adeptAdditionalPoints"))
			require(integer(policy["adeptAdditionalPoints"], 0, 12),
				"Metamagic adeptAdditionalPoints must be an integer in 0..12");
		if(metamagic && policy.Struct().contains("adeptAdditionalMarks"))
		{
			const auto & marks = policy["adeptAdditionalMarks"];
			require(marks.getType() == JsonNode::JsonType::DATA_INTEGER
				&& marks.Integer() >= 0 && marks.Integer() <= 5,
				"Metamagic adeptAdditionalMarks must be an integer in 0..5");
		}
	}
}

static void validatePerkRulesImpl(const JsonNode & rules, nativeRules::PreparedPilotPerks * collector);
void validatePerkRules(const JsonNode & rules)
{
	validatePerkRulesImpl(rules, nullptr);
}
std::shared_ptr<const nativeRules::PreparedPilotPerks> preparePilotPerkRules(const JsonNode & rules)
{
	auto result = std::make_shared<nativeRules::PreparedPilotPerks>();
	validatePerkRulesImpl(rules, result.get());
	return result;
}

static void validatePerkRulesImpl(const JsonNode & rules, nativeRules::PreparedPilotPerks * collector)
{
	if(!usesPerkRules(rules))
		return;
	fields(rules, {"schemaVersion", "rulesetVersion", "sourceDocument", "sourceSha256",
		"maxSkillChoices", "maxPerkChoices", "maxPerksPerSkill", "skills", "offerWeightPolicyVersion",
		"perkOfferNormalization", "perkProgressionPolicyVersion", "necromancyPolicy", "sylvanTacticsPolicy", "commandPolicyVersion", "elementalAttunementPolicy", "connoisseurPolicy",
		"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy", "commandAuthorityPolicy", "warfarePolicyVersion"});
	validatePassiveFactionPolicies(rules);
	newHorizonsWarfare::validatePolicy(rules);
	quickStudyRerollInterval(rules);
	if(usesCommandAuthorityPolicy(rules))
	{
		require(!rules.Struct().contains("commandPolicyVersion"), "finite Command policy conflicts with Command/Authority");
		require(rules["necromancyPolicy"].isStruct()
			&& rules["necromancyPolicy"]["version"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& rules["necromancyPolicy"]["version"].Integer() == 3, "Command/Authority requires Necromancy policy3");
		require(rules["warfarePolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& rules["warfarePolicyVersion"].Integer() == 1, "warfarePolicyVersion");
	}
	else
	{
		require(!rules.Struct().contains("warfarePolicyVersion"), "Warfare requires Command/Authority policy");
		require(!rules["necromancyPolicy"].isStruct() || !rules["necromancyPolicy"]["version"].isNumber()
			|| rules["necromancyPolicy"]["version"].Integer() != 3, "Necromancy policy3 requires Command/Authority");
		require(rules["commandPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& rules["commandPolicyVersion"].Integer() == 1, "commandPolicyVersion");
	}
	newHorizonsElementalAttunement::validatePolicy(rules);
	usesSylvanTactics(rules);
	newHorizonsNecromancy::validatePolicy(rules);
	require(integer(rules["schemaVersion"], PERK_SCHEMA_VERSION, PERK_SCHEMA_VERSION), "schemaVersion");
	require(integer(rules["rulesetVersion"], PERK_MIN_RULESET_VERSION, PERK_RULESET_VERSION), "rulesetVersion");
	if(rules.Struct().contains("offerWeightPolicyVersion"))
		require(integer(rules["offerWeightPolicyVersion"], 1,
			PERK_OFFER_WEIGHT_POLICY_VERSION), "offerWeightPolicyVersion");
	if(rules["offerWeightPolicyVersion"].isNumber() && rules["offerWeightPolicyVersion"].Integer() == 2)
	{
		const auto & normalization = rules["perkOfferNormalization"];
		fields(normalization, {"maximumPreferenceRatio", "mathVersion"});
		const auto & ratio = normalization["maximumPreferenceRatio"];
		require(ratio.isNumber() && std::isfinite(ratio.Float()) && ratio.Float() >= 1.0,
			"finite maximumPreferenceRatio >= 1 required");
		require(integer(normalization["mathVersion"], PERK_OFFER_NORMALIZATION_MATH_VERSION,
			PERK_OFFER_NORMALIZATION_MATH_VERSION), "perk normalization mathVersion");
	}
	else
		require(!rules.Struct().contains("perkOfferNormalization"), "normalization requires policy2");
	if(rules.Struct().contains("perkProgressionPolicyVersion"))
	{
		require(rules["perkProgressionPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& rules["perkProgressionPolicyVersion"].Integer() == PERK_PROGRESSION_POLICY_VERSION,
			"perkProgressionPolicyVersion");
		require(usesR2PerkOffers(rules), "strict perk progression requires captured weight policy2");
	}
	require(rules["sourceDocument"].isString() && !rules["sourceDocument"].String().empty(), "sourceDocument");
	require(rules["sourceSha256"].isString() && rules["sourceSha256"].String().size() == 64, "sourceSha256");
	for(const auto c : rules["sourceSha256"].String())
		require(std::isxdigit(static_cast<unsigned char>(c)) && !std::isupper(static_cast<unsigned char>(c)), "sourceSha256");
	require(integer(rules["maxSkillChoices"], PERK_MAX_SKILL_CHOICES, PERK_MAX_SKILL_CHOICES), "maxSkillChoices");
	require(integer(rules["maxPerkChoices"], PERK_MAX_PERK_CHOICES, PERK_MAX_PERK_CHOICES), "maxPerkChoices");
	require(integer(rules["maxPerksPerSkill"], PERK_MAX_PERKS_PER_SKILL, PERK_MAX_PERKS_PER_SKILL),
		"maxPerksPerSkill");
	require(rules["skills"].isStruct() && rules["skills"].Struct().size() == canonicalSkillCount(rules),
		"complete skill coverage required");
	require(rules["skills"].Struct().contains("new-horizons:authority") == usesCommandAuthorityPolicy(rules),
		"Authority requires the Command/Authority policy");
	std::set<std::string> skills;
	std::set<std::string> identities;
	for(const auto & [key, node] : rules["skills"].Struct())
	{
		fields(node, {"id", "name", "domain", "availability", "description", "details", "ranks", "perks"});
		validateDetails(node, key);
		require(scopedId(key), "scoped skill key");
		require(node["id"].isString() && node["id"].String() == key, "skill key/id mismatch");
		require(skills.insert(key).second, "duplicate skill id");
		require(node["name"].isString() && !node["name"].String().empty(), "skill name");
		static const std::set<std::string> domains = {"Martial", "Magic", "Hybrid", "Strategic", "Faction"};
		static const std::set<std::string> availability = {"All heroes", "Might-exclusive", "Magic-exclusive",
			"Castle heroes only", "Rampart heroes only", "Tower heroes only", "Dungeon heroes only",
			"Inferno heroes only", "Necropolis heroes only", "Stronghold heroes only",
			"Fortress heroes only", "Conflux heroes only"};
		require(node["domain"].isString() && domains.contains(node["domain"].String()), "skill domain");
		require(node["availability"].isString() && availability.contains(node["availability"].String()),
			"skill availability");
		require(node["description"].isString() && !node["description"].String().empty(), "skill description");
		fields(node["ranks"], {"basic", "advanced", "expert"});
		for(const auto * rank : {"basic", "advanced", "expert"})
		{
			validateRank(node["ranks"][rank], key + " rank " + rank);
			nativeRules::validatePilotRankRow(key, rank, node["ranks"][rank], collector);
		}
		require(node["perks"].isVector() && node["perks"].Vector().size() == PERK_CANONICAL_POOL_SIZE,
			key + " must have ten perks");
		std::array<size_t, 3> counts{};
		for(size_t i = 0; i < node["perks"].Vector().size(); ++i)
		{
			validatePerk(node["perks"].Vector()[i], key, key + " perk " + std::to_string(i), identities);
			nativeRules::validatePilotPerkRow(key, node["perks"].Vector()[i], collector);
			const auto & prerequisite = node["perks"].Vector()[i]["requires"].String();
			if(prerequisite == "basic")
				++counts[0];
			else if(prerequisite == "advanced")
				++counts[1];
			else
				++counts[2];
		}
		require(counts[0] == PERK_BASIC_POOL_SIZE && counts[1] == PERK_ADVANCED_POOL_SIZE
			&& counts[2] == PERK_EXPERT_POOL_SIZE, key + " must have four basic, four advanced and two expert perks");
	}
}

bool usesWeightedPerkOffers(const JsonNode & rules)
{
	validatePerkRules(rules);
	return usesPerkRules(rules) && rules.Struct().contains("offerWeightPolicyVersion");
}

bool usesR2PerkOffers(const JsonNode & rules)
{
	return usesPerkRules(rules) && rules["offerWeightPolicyVersion"].isNumber()
		&& rules["offerWeightPolicyVersion"].Integer() == 2;
}

bool usesStrictPerkProgression(const JsonNode & rules)
{
	return usesPerkRules(rules)
		&& rules["perkProgressionPolicyVersion"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& rules["perkProgressionPolicyVersion"].Integer() == PERK_PROGRESSION_POLICY_VERSION;
}

void validateWeightedPerkSerialization(const JsonNode & rules, bool supported, bool r2Supported, bool strictSupported, bool optionalRerollSupported, bool passiveFactionSupported)
{
	validatePerkRules(rules);
	if(!passiveFactionSupported && rules.isStruct())
		for(const auto * key : {"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
			if(rules.Struct().contains(key))
				throw std::runtime_error("Cannot serialize passive faction skills to an older format");
	if(quickStudyRerollInterval(rules) && !optionalRerollSupported)
		throw std::runtime_error("Cannot serialize optional Quick Study policy to an older format");
	if(rules.isStruct() && rules.Struct().contains("perkProgressionPolicyVersion") && !strictSupported)
		throw std::runtime_error("Cannot serialize strict perk progression to an older format");
	if(usesPerkRules(rules) && rules.Struct().contains("offerWeightPolicyVersion") && !supported)
		throw std::runtime_error("Cannot serialize weighted perk offers to an older format");
	if(usesR2PerkOffers(rules) && !r2Supported)
		throw std::runtime_error("Cannot serialize R2 perk offers to an older format");
}

void validateWeightedPerkCapture(const JsonNode & development, const JsonNode & perks)
{
	if(!usesWeightedPerkOffers(perks))
		return;
	require(usesRules(development), "weighted perk capture requires hero rules");
	validateHeroRules(development, true);
	require(development["skillOfferWeights"].isStruct(), "weighted perk capture requires the full skill offer table");
	require(usesCommandAuthorityPolicy(development) == usesCommandAuthorityPolicy(perks),
		"hero and perk Command/Authority markers must agree");
}

std::optional<SkillDefinition> perkSkill(const JsonNode & rules, std::string_view skillId)
{
	validatePerkRules(rules);
	if(!usesPerkRules(rules))
		return std::nullopt;
	const auto it = rules["skills"].Struct().find(std::string(skillId));
	if(it == rules["skills"].Struct().end())
		return std::nullopt;
	SkillDefinition result;
	parseSkill(it->second, result);
	return result;
}

std::optional<PerkDefinition> perkDefinition(const JsonNode & rules, std::string_view skillId,
	std::string_view perkId)
{
	const auto skill = perkSkill(rules, skillId);
	if(!skill)
		return std::nullopt;
	const auto it = std::find_if(skill->perks.begin(), skill->perks.end(),
		[perkId](const auto & perk) { return perk.id == perkId; });
	if(it == skill->perks.end())
		return std::nullopt;
	return *it;
}

std::vector<PerkDefinition> perkOptions(const JsonNode & rules, std::string_view skillId)
{
	const auto skill = perkSkill(rules, skillId);
	return skill ? skill->perks : std::vector<PerkDefinition>();
}
}
