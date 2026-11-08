/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NativeJsonParameters.h"
#include "../json/JsonBonus.h"
#include "../spells/NewHorizonsDivineRetribution.h"

namespace nativeRules
{
JsonNode capturedParameters(const JsonNode & source)
{
	JsonNode result;
	if(source.isStruct())
	{
		for(const auto & [key, value] : source.Struct())
			if(!value.isNull()) result[key] = capturedParameters(value);
	}
	else if(source.isVector())
	{
		for(size_t index = 0; index < source.Vector().size(); ++index)
			if(!source.Vector()[index].isNull())
			{
				result.Vector().resize(index + 1);
				result.Vector()[index] = capturedParameters(source.Vector()[index]);
			}
	}
	else
	{
		if(source.isNumber()) result.Float() = source.Float();
		else if(source.isString()) result.String() = source.String();
		else if(source.isBool()) result.Bool() = source.Bool();
		result.setModScope("game", false);
	}
	return result;
}

Bonus bonusFromDescriptor(const JsonNode & source)
{
	JsonNode result;
	result["val"].Integer() = static_cast<int32_t>(source["val"].Float());
	result["turns"].Integer() = static_cast<int32_t>(source["turns"].Float());
	if(source["hidden"].Bool()) result["hidden"].Bool() = true;
	for(const auto * key : {"stacking", "description", "icon", "statusIdentity"})
		if(!source[key].String().empty()) result[key].String() = source[key].String();
	for(const auto * key : {"type", "subtype", "valueType", "effectRange", "duration", "sourceType", "sourceID",
		"targetSourceType", "addInfo", "statusTags", "limiters", "propagator", "updater", "propagationUpdater"})
		if(!source[key].isNull())
		{
			auto value = capturedParameters(source[key]);
			if(!value.isNull()) result[key] = std::move(value);
		}
	Bonus bonus;
	JsonUtils::parseBonus(result, &bonus);
	newHorizonsDivineRetribution::canonicalizeScriptPayoutRatio(bonus);
	return bonus;
}
}
