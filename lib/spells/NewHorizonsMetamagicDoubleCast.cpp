/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMetamagicDoubleCast.h"
#include "../json/JsonNode.h"

namespace newHorizonsMetamagic
{
namespace
{
struct Field
{
	const char * key;
	int32_t DoubleCastPolicy::* member;
	int32_t minimum;
	int32_t maximum;
};

// Bounds are admission sanity limits, not balance values; authored numbers live in data.
constexpr std::array<Field, 12> scalarFields{{
	{"halonAdditionalUses", &DoubleCastPolicy::halonAdditionalUses, 0, 3},
	{"arcaneWardSpellPowerMultiplier", &DoubleCastPolicy::arcaneWardSpellPowerMultiplier, 0, 20},
	{"manaSiphonMana", &DoubleCastPolicy::manaSiphonMana, 0, 20},
	{"deepReservesAdditionalUses", &DoubleCastPolicy::deepReservesAdditionalUses, 0, 3},
	{"formulaReserveMana", &DoubleCastPolicy::formulaReserveMana, 0, 20},
	{"arcaneCycleRoundInterval", &DoubleCastPolicy::arcaneCycleRoundInterval, 1, 10},
	{"afterglowAdditionalRounds", &DoubleCastPolicy::afterglowAdditionalRounds, 0, 3},
	{"crescendoSpellPowerPerSpentUse", &DoubleCastPolicy::crescendoSpellPowerPerSpentUse, 0, 20},
	{"cleansingWeaveEffects", &DoubleCastPolicy::cleansingWeaveEffects, 0, 5},
	{"grandWeavingAdditionalUses", &DoubleCastPolicy::grandWeavingAdditionalUses, 0, 5},
	{"spellstormManaReductionPercent", &DoubleCastPolicy::spellstormManaReductionPercent, 0, 90},
	{"spellstormDoubleCastThreshold", &DoubleCastPolicy::spellstormDoubleCastThreshold, 1, 10},
}};

std::optional<int32_t> boundedInteger(const JsonNode & value, int32_t minimum, int32_t maximum)
{
	if(value.getType() != JsonNode::JsonType::DATA_INTEGER || value.Integer() < minimum || value.Integer() > maximum)
		return std::nullopt;
	return static_cast<int32_t>(value.Integer());
}

bool decodeUses(const JsonNode & values, std::array<int32_t, 4> & result)
{
	if(!values.isVector() || values.Vector().size() != result.size())
		return false;
	for(size_t rank = 0; rank < result.size(); ++rank)
	{
		const auto decoded = boundedInteger(values.Vector()[rank], rank == 0 ? 0 : 1, rank == 0 ? 0 : 5);
		if(!decoded || (rank > 1 && *decoded < result[rank - 1]))
			return false;
		result[rank] = *decoded;
	}
	return true;
}
}

bool claimsDoubleCastPolicy(const JsonNode & rules)
{
	const auto & policy = rules["metamagicPolicy"];
	return policy.isStruct() && policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& policy["version"].Integer() == DOUBLE_CAST_POLICY_VERSION;
}

std::optional<DoubleCastPolicy> doubleCastPolicy(const JsonNode & rules)
{
	if(!claimsDoubleCastPolicy(rules))
		return std::nullopt;
	const auto & policy = rules["metamagicPolicy"];
	if(policy.Struct().size() != scalarFields.size() + 3)
		return std::nullopt;
	DoubleCastPolicy result;
	if(!decodeUses(policy["usesPerCombatByRank"], result.usesPerCombatByRank) || !policy["followUpMustDiffer"].isBool())
		return std::nullopt;
	result.followUpMustDiffer = policy["followUpMustDiffer"].Bool();
	for(const auto & field : scalarFields)
	{
		const auto found = policy.Struct().find(field.key);
		if(found == policy.Struct().end())
			return std::nullopt;
		const auto decoded = boundedInteger(found->second, field.minimum, field.maximum);
		if(!decoded)
			return std::nullopt;
		result.*field.member = *decoded;
	}
	return result;
}
}
