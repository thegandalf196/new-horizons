/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkRules.h"
#include "NewHorizonsRuleValidation.h"
#include <array>

namespace newHorizonsHeroes
{
namespace
{
using detail::fields;
using detail::integer;
using detail::require;

void validateMetamagicV4(const JsonNode & policy)
{
	fields(policy, {"version", "removalChancePercent", "damagePerMana", "restorationPerMana", "halonRemovalChanceBonusPercent"});
	require(policy.Struct().size() == 5, "Metamagic v4 requires its complete captured profile");
	const auto & chances = policy["removalChancePercent"];
	require(chances.isVector() && chances.Vector().size() == 4, "Metamagic removal chances require four ranks");
	constexpr std::array expected{0, 20, 30, 40};
	for(size_t rank = 0; rank < expected.size(); ++rank)
		require(chances.Vector()[rank].getType() == JsonNode::JsonType::DATA_INTEGER
			&& chances.Vector()[rank].Integer() == expected[rank], "Unsupported Metamagic removal chance");
	for(const auto & [key, expectedValue] : std::array<std::pair<const char *, int>, 3>{{
		{"damagePerMana", 10}, {"restorationPerMana", 15}, {"halonRemovalChanceBonusPercent", 5}}})
		require(policy[key].getType() == JsonNode::JsonType::DATA_INTEGER
			&& policy[key].Integer() == expectedValue, std::string("Unsupported Metamagic ") + key);
}
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
			&& policy["version"].Integer() == 4)
		{
			validateMetamagicV4(policy);
			continue;
		}
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

}
