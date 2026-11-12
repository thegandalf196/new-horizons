/*
 * HeroExperienceBar.h, part of VCMI engine / New Horizons
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "StdInc.h"
#include "../widgets/CComponent.h"

class Canvas;

/// Visual progress bar for hero experience within the current level
class HeroExperienceBar : public CIntObject
{
private:
	double progress = 0.0; // Clamped in [0.0, 1.0]

public:
	HeroExperienceBar(Point pos, Point size);

	void setProgress(double ratio);
	void show(Canvas & to) override;
	void showAll(Canvas & to) override;
};
