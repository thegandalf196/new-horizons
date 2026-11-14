/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../../lib/constants/EntityIdentifiers.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <memory>
#include <string>
#include <vector>

class CSelectableComponent;

namespace levelUpPresentation
{
struct PerkRow
{
	std::string id;
	std::string name;
	std::string icon;
};

std::shared_ptr<CSelectableComponent> skillCard(SecondarySkill skill, int offeredRank,
	const std::string & skillName, const std::string & rankText, const std::vector<PerkRow> & perks);
std::shared_ptr<CSelectableComponent> perkCard(SecondarySkill skill, bool hasSkill,
	const std::string & name, const AnimationPath & icon);
}
