/*
 * HeroInfoWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <optional>

#include "../windows/CWindowObject.h"

#include "BattleSidePanel.h"

class CLabel;
class CAnimImage;

struct InfoAboutHero;

class HeroInfoBasicPanel : public BattleSidePanel //extracted from InfoWindow to fit better as non-popup embed element
{
private:
	std::shared_ptr<CPicture> background;
	std::shared_ptr<CLabel> counterspellStatus;
	std::vector<std::shared_ptr<CLabel>> labels;
	std::vector<std::shared_ptr<CAnimImage>> icons;
	bool showCounterspellStatus = false;
	bool counterspellArmed = false;

public:
	HeroInfoBasicPanel(const InfoAboutHero & hero, const Point * position, bool initializeBackground = true,
		bool showCounterspellStatus = false, bool counterspellArmed = false);

	void initializeData(const InfoAboutHero & hero);
	void update(const InfoAboutHero & updatedInfo, std::optional<bool> counterspellArmed = std::nullopt);
	void setCounterspellStatus(bool armed);
};

class HeroInfoWindow : public CWindowObject
{
private:
	std::shared_ptr<HeroInfoBasicPanel> content;

public:
	HeroInfoWindow(const InfoAboutHero & hero, const Point * position);
};
