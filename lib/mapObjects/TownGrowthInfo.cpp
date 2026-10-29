/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../entities/building/CBuilding.h"
#include "CGTownInstance.h"
#include "NewHorizonsRecruitmentGrowth.h"
#include "../GameLibrary.h"
#include "../entities/faction/CTownHandler.h"
#include "../texts/MetaString.h"
#include <limits>
#include <stdexcept>

GrowthInfo::Entry::Entry(const std::string &format, int _count)
	: count(_count)
{
	MetaString formatter;
	formatter.appendRawString(format);
	formatter.replacePositiveNumber(count);

	description = formatter.toString(LIBRARY->staticTexts());
}

GrowthInfo::Entry::Entry(int subID, const BuildingID & building, int _count): count(_count)
{
	MetaString formatter;
	formatter.appendRawString("%s %+d");
	formatter.replaceTextID(FactionID(subID).toFaction()->town->buildings.at(building)->getNameTextID());
	formatter.replacePositiveNumber(count);

	description = formatter.toString(LIBRARY->staticTexts());
}

GrowthInfo::Entry::Entry(int _count, std::string fullDescription):
	count(_count),
	description(std::move(fullDescription))
{
}

int GrowthInfo::unscaledGrowth() const
{
	int64_t subtotal = 0;
	for(const Entry & entry : entries)
		subtotal += entry.count;
	if(subtotal < 0 || handicapPercentage < 0
		|| (handicapPercentage != 0 && subtotal > std::numeric_limits<int64_t>::max() / handicapPercentage))
		throw std::overflow_error("Town growth subtotal exceeds supported count range");
	// Preserve the existing handicap ceiling before the final recruitment modifier.
	const auto product = subtotal * handicapPercentage;
	const auto result = product / 100 + (product % 100 != 0);
	if(result > std::numeric_limits<int>::max())
		throw std::overflow_error("Town growth subtotal exceeds supported count range");
	return static_cast<int>(result);
}

int GrowthInfo::totalGrowth() const
{
	const auto count = newHorizonsRecruitmentGrowth::scale(unscaledGrowth(), recruitmentGrowthPercentage);
	if(count > std::numeric_limits<int>::max())
		throw std::overflow_error("Town recruitment growth exceeds supported count range");
	return static_cast<int>(count);
}
