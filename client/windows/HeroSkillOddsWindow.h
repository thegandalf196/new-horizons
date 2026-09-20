/*
 * HeroSkillOddsWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CWindowObject.h"

class CGHeroInstance;

/// Read-only class weights; actual offers also depend on the hero and level-up rules.
class HeroSkillOddsWindow : public CWindowObject
{
	std::vector<std::shared_ptr<CIntObject>> elements;

public:
	explicit HeroSkillOddsWindow(const CGHeroInstance & hero);
};
