/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsPerkRules.h"
#include "NewHorizonsRuleValidation.h"
#include "../../spells/NewHorizonsMetamagicDoubleCast.h"
#include "../../spells/NewHorizonsMetamagicResolution.h"
#include <array>

namespace newHorizonsHeroes
{
namespace
{
using detail::fields;
using detail::integer;
using detail::require;

void validateConsecratedArms(const JsonNode & policy)
{
	// Version 4 (Chosen Prayers) differs only by its explicit chosen-prayer marker.
	if(policy["version"].Integer() == 4)
	{
		fields(policy, {"version", "potencyFaithBonusByRank", "ferventInvocationAdditionalFaith", "chosen"});
		require(policy["chosen"].isBool() && policy["chosen"].Bool(),
			"Consecrated Arms version 4 requires chosen prayers");
	}
	else
		fields(policy, {"version", "potencyFaithBonusByRank", "ferventInvocationAdditionalFaith"});
	const auto & ranks = policy["potencyFaithBonusByRank"];
	static constexpr std::array<int, 4> expected = {0, 0, 50, 100};
	require(ranks.isVector() && ranks.Vector().size() == expected.size(),
		"Consecrated Arms requires four captured rank Faith bonuses");
	for(size_t index = 0; index < expected.size(); ++index)
		require(ranks.Vector()[index].getType() == JsonNode::JsonType::DATA_INTEGER
			&& ranks.Vector()[index].Integer() == expected[index],
			"Consecrated Arms has an unsupported rank Faith bonus");
	require(policy["ferventInvocationAdditionalFaith"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& policy["ferventInvocationAdditionalFaith"].Integer() == 100,
		"Consecrated Arms requires its captured Fervent Invocation bonus");
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
		require(policy.isStruct(), std::string(key) + " must be an object");
		const bool metamagic = std::string_view(key) == "metamagicPolicy";
		if(metamagic && policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& (policy["version"].Integer() >= 4 && policy["version"].Integer() <= 6))
		{
			require(newHorizonsMetamagic::resolutionPolicy(rules).has_value(),
				"Metamagic requires its complete supported captured profile");
			continue;
		}
		if(metamagic && newHorizonsMetamagic::claimsDoubleCastPolicy(rules))
		{
			require(newHorizonsMetamagic::doubleCastPolicy(rules).has_value(),
				"Metamagic requires its complete supported Double Cast profile");
			continue;
		}
		if(std::string_view(key) == "divineMandatePolicy"
			&& policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
			&& (policy["version"].Integer() == 3 || policy["version"].Integer() == 4))
		{
			validateConsecratedArms(policy);
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
