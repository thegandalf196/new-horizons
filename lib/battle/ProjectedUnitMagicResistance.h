/*
 * ProjectedUnitMagicResistance.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once

#include "UnitMagicResistanceQuery.h"

class JsonNode;
class ProjectedBonusRoster;

namespace battle
{
/// Complete resistance-query domain, NOT a partial IUnitEnvironment with silent
/// default callbacks. Future world owner must compose all remaining callbacks.
/// Borrows complete ordered roster, its graph-bound units and immutable captured
/// magic rules for the caller's existing exclusive lifetime interval. No lease
/// is created here. Queries see current sequential state, not copied live units.
class DLL_LINKAGE ProjectedUnitMagicResistance final : public UnitMagicResistanceQuery
{
	const ProjectedBonusRoster & roster;
	const JsonNode & magicRules;
	void requireSubject(const Unit & unit) const override;
	Units adjacentUnits(const Unit & unit) const override;
	bool newHorizonsRulesActive() const override;
public:
	ProjectedUnitMagicResistance(const ProjectedBonusRoster & roster, const JsonNode & magicRules);
};
}
