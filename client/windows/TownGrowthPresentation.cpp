/*
 * TownGrowthPresentation.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "TownGrowthPresentation.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/texts/MetaString.h"

namespace townGrowthPresentation
{
std::string description(const GrowthInfo & growth, CreatureID creature, const ITranslator * translator)
{
	const int total = growth.totalGrowth();
	MetaString text;
	text.appendTextID("core.genrltxt.589");
	text.replaceNameSingular(creature);
	text.replaceNumber(total);
	for(const auto & entry : growth.entries)
	{
		text.appendEOL();
		text.appendRawString(entry.description);
	}
	if(growth.recruitmentGrowthPercentage != 100)
	{
		auto modifier = MetaString::createFromTextID("new-horizons.growth.recruitmentModifier");
		modifier.replaceNumber(growth.unscaledGrowth());
		modifier.replaceNumber(growth.recruitmentGrowthPercentage);
		modifier.replaceNumber(total);
		text.appendEOL();
		text.appendRawString(modifier.toString(translator));
	}
	return text.toString(translator);
}
}
