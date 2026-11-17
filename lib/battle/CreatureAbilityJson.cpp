/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CreatureAbility.h"
#include "../bonuses/BonusEnum.h"
#include "../bonuses/BonusCustomTypes.h"
#include "../json/JsonNode.h"
#include "../GameLibrary.h"
#include "../CBonusTypeHandler.h"
#include <stdexcept>
#include <array>

std::string CreatureAbilityDescriptor::logicalIdentity() const
{
	validate();
	return std::visit([](const auto & id) -> std::string
	{
		using T = std::decay_t<decltype(id)>;
		if constexpr(std::is_same_v<T, SpellID>) return SpellID::encode(id.getNum());
		else if constexpr(std::is_same_v<T, ScriptID>) return ScriptID::encode(id.getNum());
		else if constexpr(std::is_same_v<T, BonusType>) return LIBRARY->bth->bonusToString(id);
		else
		{
			static constexpr std::array<const char *, 11> keys = {"physicalPoison", "new-horizons:yog:concussiveStun",
				"frozen", "serpentsEmbrace", "paralyzingGaze", "cripple", "graveDust", "scorchingImpact",
				"cruelOverseer", "ordinaryCreatureBurn", "coordinatedMark"};
			return keys.at(static_cast<size_t>(id));
		}
	}, identity);
}

JsonNode CreatureAbilityDescriptor::toJson() const
{
	validate();
	JsonNode result;
	result["family"].Integer() = static_cast<uint8_t>(family);
	result["categories"].Integer() = categories;
	result["interactions"].Integer() = interactions;
	result["resolution"].Integer() = static_cast<uint8_t>(resolution);
	result["sourceOwner"].Integer() = sourceOwner.getNum();
	std::visit([&result](const auto & id)
	{
		using T = std::decay_t<decltype(id)>;
		if constexpr(std::is_same_v<T, SpellID>) result["spell"].String() = SpellID::encode(id.getNum());
		else if constexpr(std::is_same_v<T, ScriptID>) result["script"].String() = ScriptID::encode(id.getNum());
		else if constexpr(std::is_same_v<T, BonusType>) result["trait"].String() = LIBRARY->bth->bonusToString(id);
		else result["native"].Integer() = static_cast<uint8_t>(id);
	}, identity);
	return result;
}

std::shared_ptr<const CreatureAbilityDescriptor> CreatureAbilityDescriptor::fromJson(const JsonNode & json)
{
	if(!json.isStruct() || json.Struct().size() != 6)
		throw std::runtime_error("Ability provenance requires exactly its five fields and one identity");
	const auto byte = [&json](const char * key) -> uint8_t
	{
		const auto & field = json[key];
		if(field.getType() != JsonNode::JsonType::DATA_INTEGER || field.Integer() < 0 || field.Integer() > 255)
			throw std::runtime_error("Ability provenance requires bounded integer fields");
		return static_cast<uint8_t>(field.Integer());
	};
	auto result = std::make_shared<CreatureAbilityDescriptor>();
	result->family = static_cast<creatureAbility::Family>(byte("family"));
	result->categories = byte("categories");
	result->interactions = byte("interactions");
	result->resolution = static_cast<creatureAbility::Resolution>(byte("resolution"));
	if(json["sourceOwner"].getType() != JsonNode::JsonType::DATA_INTEGER
		|| json["sourceOwner"].Integer() < -3 || json["sourceOwner"].Integer() > 7)
		throw std::runtime_error("Ability provenance requires a valid source owner");
	result->sourceOwner = PlayerColor(static_cast<int32_t>(json["sourceOwner"].Integer()));
	int identities = 0;
	for(const auto * key : {"spell", "script", "trait"})
	{
		if(!json.Struct().contains(key)) continue;
		if(json[key].getType() != JsonNode::JsonType::DATA_STRING)
			throw std::runtime_error("Ability backing identity must be a canonical identifier");
		++identities;
		if(std::string_view(key) == "spell") result->identity = SpellID(SpellID::decode(json[key].String()));
		else if(std::string_view(key) == "script") result->identity = ScriptID(ScriptID::decode(json[key].String()));
		else result->identity = BonusTypeID(BonusTypeID::decode(json[key].String())).toEnum();
	}
	if(json.Struct().contains("native"))
	{
		++identities;
		result->identity = static_cast<creatureAbility::Native>(byte("native"));
	}
	if(identities != 1) throw std::runtime_error("Ability provenance requires exactly one backing identity");
	result->validate();
	return result;
}
