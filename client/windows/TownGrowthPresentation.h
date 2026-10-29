/*
 * TownGrowthPresentation.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <string>

struct GrowthInfo;
class CreatureID;
class ITranslator;

namespace townGrowthPresentation
{
/// Describe the town's calculated weekly output; never re-scale stored stock.
std::string description(const GrowthInfo & growth, CreatureID creature, const ITranslator * translator);
}
