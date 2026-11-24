/*
 * NewHorizonsPerkBrowser.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "CWindowObject.h"

#include <memory>
#include <string>
#include <vector>

class CGHeroInstance;
class CButton;

/// Read-only catalogue of the saved perk definitions for one Skill, learned or
/// not; the hero's learned perks are highlighted and browsing changes nothing.
class NewHorizonsPerkBrowser : public CWindowObject
{
	std::vector<std::shared_ptr<CIntObject>> elements;
	std::shared_ptr<CButton> closeButton;

public:
	NewHorizonsPerkBrowser(const CGHeroInstance & hero, const std::string & skillId);
};
