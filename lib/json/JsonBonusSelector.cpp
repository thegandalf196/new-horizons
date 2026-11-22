/*
 * JsonBonusSelector.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "JsonBonus.h"
#include "JsonBonusIdentifiers.h"

#include "../GameLibrary.h"
#include "../bonuses/BonusSelector.h"
#include "../modding/IdentifierStorage.h"

namespace
{
/// Type and subtype a selector compares bonuses against. Both are identifiers that may only resolve
/// once library loading has finished, so the selector and every pending identifier request own this
/// together: a late request never writes into storage that ended with the parsing call, and the
/// selector sees each value once it resolves. Until then, or if it never does, nothing matches.
struct SelectedBonusType
{
	BonusType type = BonusType::NONE;
	BonusSubtypeID subtype;
	bool typeResolved = false;
	bool subtypeNamed = false;
	bool subtypeResolved = false;

	bool matches(const Bonus & bonus) const
	{
		if(!typeResolved || bonus.type != type)
			return false;

		return !subtypeNamed || (subtypeResolved && bonus.subtype == subtype);
	}
};

/// Source instance a selector compares bonuses against, shared for the same reason as the type.
struct SelectedBonusSource
{
	BonusSourceID instance;
	bool resolved = false;
};

CSelector typeSelector(const JsonNode & typeNode, const JsonNode & subtypeNode)
{
	auto selected = std::make_shared<SelectedBonusType>();
	selected->subtypeNamed = !subtypeNode.isNull();

	// what a subtype names depends on the bonus type, so it can only be asked for once the type is
	// known. The node is copied - by then the json this selector was parsed from may be gone
	LIBRARY->identifiers()->requestIdentifier("bonus", typeNode, [selected, subtypeNode](int32_t bonusID)
	{
		selected->type = static_cast<BonusType>(bonusID);
		selected->typeResolved = true;

		if(!selected->subtypeNamed)
			return;

		JsonBonusIdentifiers::requestSubtype(selected->type, subtypeNode, [selected](const BonusSubtypeID & subtype)
		{
			selected->subtype = subtype;
			selected->subtypeResolved = true;
		});
	});

	return [selected = std::shared_ptr<const SelectedBonusType>(selected)](const Bonus * bonus)
	{
		return selected->matches(*bonus);
	};
}

CSelector sourceSelector(BonusSource source, const JsonNode & instanceNode)
{
	if(instanceNode.isNull())
		return Selector::sourceTypeSel(source);

	auto selected = std::make_shared<SelectedBonusSource>();

	JsonBonusIdentifiers::requestSourceInstance(source, instanceNode, [selected](const BonusSourceID & instance)
	{
		selected->instance = instance;
		selected->resolved = true;
	});

	return [source, selected = std::shared_ptr<const SelectedBonusSource>(selected)](const Bonus * bonus)
	{
		return selected->resolved && bonus->source == source && bonus->sid == selected->instance;
	};
}

/// allOf, anyOf and noneOf, each a list of nested selectors
CSelector compoundSelector(const JsonNode & ability)
{
	CSelector ret = Selector::all;

	const auto & allOf = ability["allOf"];
	if(allOf.isVector())
	{
		for(const auto & nested : allOf.Vector())
			ret = ret.And(JsonUtils::parseSelector(nested));
	}

	for(const bool inverted : {false, true})
	{
		const auto & list = ability[inverted ? "noneOf" : "anyOf"];
		if(!list.isVector())
			continue;

		CSelector base = Selector::none;
		for(const auto & nested : list.Vector())
			base = base.Or(JsonUtils::parseSelector(nested));

		ret = ret.And(inverted ? base.Not() : base);
	}
	return ret;
}

template<typename T>
std::optional<T> findMapped(const std::map<std::string, T> & map, const JsonNode & node)
{
	if(!node.isString())
		return std::nullopt;

	const auto it = map.find(node.String());
	if(it == map.end())
		return std::nullopt;

	return it->second;
}

/// Fields compared against plain values, which never need identifier resolution
CSelector valueSelector(const JsonNode & ability)
{
	CSelector ret = Selector::all;

	if(const auto targetSource = findMapped(bonusSourceMap, ability["targetSourceType"]))
		ret = ret.And(Selector::targetSourceType()(*targetSource));
	if(const auto valueType = findMapped(bonusValueMap, ability["valueType"]))
		ret = ret.And(Selector::valueType(*valueType));
	if(const auto effectRange = findMapped(bonusLimitEffect, ability["effectRange"]))
		ret = ret.And(Selector::effectRange()(*effectRange));

	const auto & lastsTurns = ability["lastsTurns"];
	if(lastsTurns.isNumber())
		ret = ret.And(Selector::turns(static_cast<int>(lastsTurns.Integer())));
	const auto & lastsDays = ability["lastsDays"];
	if(lastsDays.isNumber())
		ret = ret.And(Selector::days(static_cast<int>(lastsDays.Integer())));

	return ret;
}
}

CSelector JsonUtils::parseSelector(const JsonNode & ability)
{
	CSelector ret = compoundSelector(ability);

	const auto & typeNode = ability["type"];
	const auto & subtypeNode = ability["subtype"];
	if(typeNode.isString())
		ret = ret.And(typeSelector(typeNode, subtypeNode));
	else if(!subtypeNode.isNull())
		logMod->warn("Bonus selector names subtype %s but no bonus type, so the subtype is ignored", subtypeNode.toCompactString());

	if(const auto source = findMapped(bonusSourceMap, ability["sourceType"]))
		ret = ret.And(sourceSelector(*source, ability["sourceID"]));

	return ret.And(valueSelector(ability));
}
