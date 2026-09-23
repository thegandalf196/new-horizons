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

#include "../windows/CWindowObject.h"

#include "BattleSidePanel.h"
#include "../../lib/battle/AlternatingHeroActionState.h"

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
constexpr int effectAreaRowHeight = 26;
constexpr int effectAreaHeight = effectAreaRowHeight * 2;
constexpr int effectAreaIconSize = 16;
constexpr int compactAttackerEffectAreaLeft = 5;
constexpr int compactDefenderEffectAreaLeft = 725;
constexpr int spellPointsLabelY = 174;
constexpr int spellPointsValueY = 186;
constexpr int outsideStackPanelOffsetY = effectAreaTop + effectAreaHeight + 3;
}

/// Compact read-only indicators for active Counterspell and Warcasting battle state.
class HeroBattleStatusArea : public CIntObject
{
	std::vector<std::shared_ptr<TransparentFilledRectangle>> backgrounds;
	std::shared_ptr<CPicture> warcastingIcon;
	std::vector<std::shared_ptr<CLabel>> labels;
	bool counterspellArmed = false;
	bool hasVisibleStatus = false;
	AlternatingHeroActionState warcastingState;
	int currentRound = 0;
	std::string statusbarText;
	std::string helpText;
	bool renderDuringShow = true;

	void refreshContents();

public:
	HeroBattleStatusArea(const Point & position);
	void setStatus(bool counterspellIsArmed, const AlternatingHeroActionState & warcasting,
		int round);
	void setRenderDuringShow(bool value);
	void hover(bool on) override;
	void showPopupWindow(const Point & cursorPosition) override;
	void showAll(Canvas & to) override;
	void show(Canvas & to) override;
};

class HeroInfoBasicPanel : public BattleSidePanel //extracted from InfoWindow to fit better as non-popup embed element
{
private:
	std::shared_ptr<CPicture> background;
	std::shared_ptr<HeroBattleStatusArea> battleStatus;
	std::vector<std::shared_ptr<CLabel>> labels;
	std::vector<std::shared_ptr<CAnimImage>> icons;
	bool showBattleStatus = false;

public:
	HeroInfoBasicPanel(const InfoAboutHero & hero, const Point * position, bool initializeBackground = true,
		bool showBattleStatus = false);

	void initializeData(const InfoAboutHero & hero);
	void update(const InfoAboutHero & updatedInfo);
	void setBattleStatus(bool counterspellIsArmed, const AlternatingHeroActionState & warcasting,
		int round);
	void setBattleStatusRenderDuringShow(bool value);
};

class HeroInfoWindow : public CWindowObject
{
private:
	std::shared_ptr<HeroInfoBasicPanel> content;

public:
	HeroInfoWindow(const InfoAboutHero & hero, const Point * position);
};
