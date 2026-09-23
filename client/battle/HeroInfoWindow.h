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
class TransparentFilledRectangle;

struct InfoAboutHero;

namespace HeroInfoPanelLayout
{
constexpr int height = 200;
constexpr int width = 77;
constexpr int backgroundInset = 1;
constexpr int compactPanelOffsetY = 195;
constexpr int luckIconY = 143;
constexpr int luckIconHeight = 22;
constexpr int effectAreaLeft = 4;
constexpr int effectAreaTop = 204;
constexpr int effectAreaWidth = 70;
constexpr int effectAreaHeight = 26;
constexpr int effectAreaTextLineAllowance = 14;
constexpr int effectAreaTextMargin = 3;
constexpr int effectAreaIconSize = 22;
constexpr int counterspellStatusY = effectAreaTop + effectAreaHeight / 2;
constexpr int compactAttackerEffectAreaLeft = 5;
constexpr int compactDefenderEffectAreaLeft = 725;
constexpr int spellPointsLabelY = 174;
constexpr int spellPointsValueY = 186;
constexpr int outsideStackPanelOffsetY = effectAreaTop + effectAreaHeight + 3;
}

/// Small hero-side effect/status row. The row is created only while it has an active entry.
class HeroCounterspellStatusArea : public CIntObject
{
	std::shared_ptr<TransparentFilledRectangle> background;
	std::shared_ptr<CLabel> label;
	bool armed = false;
	bool renderDuringShow = true;

public:
	HeroCounterspellStatusArea(const Point & position, bool armed);
	void setArmed(bool value);
	void setRenderDuringShow(bool value);
	void show(Canvas & to) override;
};

class HeroInfoBasicPanel : public BattleSidePanel //extracted from InfoWindow to fit better as non-popup embed element
{
private:
	std::shared_ptr<CPicture> background;
	std::shared_ptr<HeroCounterspellStatusArea> counterspellStatus;
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
