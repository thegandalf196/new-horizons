/*
 * Limiters.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "Limiters.h"
#include "BonusList.h"
#include "BonusSelector.h"
#include "../CBonusTypeHandler.h"
#include "../GameLibrary.h"
#include "../json/JsonNode.h"
#include <boost/container/static_vector.hpp>
#include <boost/format.hpp>
#include <limits>

HasAnotherBonusLimiter::HasAnotherBonusLimiter( BonusType bonus )
	: type(bonus), isSubtypeRelevant(false), isSourceRelevant(false), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter( BonusType bonus, BonusSubtypeID _subtype )
	: type(bonus), subtype(_subtype), isSubtypeRelevant(true), isSourceRelevant(false), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter(BonusType bonus, BonusSource src)
	: type(bonus), source(src), isSubtypeRelevant(false), isSourceRelevant(true), isSourceIDRelevant(false)
{
}

HasAnotherBonusLimiter::HasAnotherBonusLimiter(BonusType bonus, BonusSubtypeID _subtype, BonusSource src)
	: type(bonus), subtype(_subtype), isSubtypeRelevant(true), source(src), isSourceRelevant(true), isSourceIDRelevant(false)
{
}

CSelector HasAnotherBonusLimiter::dependencySelector() const
{
	boost::container::static_vector<CSelector, 4> selectorSegments;

	if (type != BonusType::NONE)
		selectorSegments.push_back(Selector::type()(type));
	if(isSubtypeRelevant)
		selectorSegments.push_back(Selector::subtype()(subtype));
	if(isSourceRelevant && isSourceIDRelevant)
		selectorSegments.push_back(Selector::source(source, sid));
	else if (isSourceRelevant)
		selectorSegments.push_back(Selector::sourceTypeSel(source));

	auto mySelector = selectorSegments.empty() ? Selector::none : selectorSegments[0];

	for (size_t i = 1; i <selectorSegments.size(); ++i)
		mySelector = mySelector.And(selectorSegments[i]);

	return mySelector;
}

ILimiter::EDecision HasAnotherBonusLimiter::limit(const BonusLimitationContext &context) const
{
	const auto mySelector = dependencySelector();

	//if we have a bonus of required type accepted, limiter should accept also this bonus
	if(context.alreadyAccepted.getFirst(mySelector))
	{
		if ( minValue == std::numeric_limits<int32_t>::min() &&
			 maxValue == std::numeric_limits<int32_t>::max())
			return ILimiter::EDecision::ACCEPT;

		// can't determine final bonus value yet
		if(context.stillUndecided.getFirst(mySelector))
			return ILimiter::EDecision::NOT_SURE;

		int bonusValue = context.alreadyAccepted.valOfBonuses(mySelector);

		if (bonusValue >= minValue && bonusValue <= maxValue)
			return ILimiter::EDecision::ACCEPT;
		else
			return ILimiter::EDecision::DISCARD;
	}

	//if there are no matching bonuses pending, we can (and must) reject right away
	if(!context.stillUndecided.getFirst(mySelector))
		return ILimiter::EDecision::DISCARD;

	//do not accept for now but it may change if more bonuses gets included
	return ILimiter::EDecision::NOT_SURE;
}

std::string HasAnotherBonusLimiter::toString() const
{
	std::string typeName = LIBRARY->bth->bonusToString(type);
	if(isSubtypeRelevant)
	{
		boost::format fmt("HasAnotherBonusLimiter(type=%s, subtype=%s)");
		fmt % typeName % subtype.toString();
		return fmt.str();
	}
	else
	{
		boost::format fmt("HasAnotherBonusLimiter(type=%s)");
		fmt % typeName;
		return fmt.str();
	}
}

JsonNode HasAnotherBonusLimiter::toJsonNode() const
{
	JsonNode root;
	std::string typeName = LIBRARY->bth->bonusToString(type);
	auto sourceTypeName = vstd::findKey(bonusSourceMap, source);

	root["type"].String() = "HAS_ANOTHER_BONUS_LIMITER";
	root["parameters"].Vector().emplace_back(typeName);
	if(isSubtypeRelevant)
		root["parameters"].Vector().emplace_back(subtype.toString());
	if(isSourceRelevant)
		root["parameters"].Vector().emplace_back(sourceTypeName);

	return root;
}
