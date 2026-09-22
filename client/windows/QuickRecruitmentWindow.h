/*
 * QuickRecruitmentWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CWindowObject.h"

#include <array>
#include <optional>

class CGTownInstance;

class CButton;
class CreatureCostBox;
class CreaturePurchaseCard;
class CFilledTexture;
class CLabel;

class QuickRecruitmentWindow : public CWindowObject
{
public:
	int getAvailableCreatures();
	void updateAllSliders();
	QuickRecruitmentWindow(const CGTownInstance * townd, Rect startupPosition);
	void showAll(Canvas & to) override;

private:
	void initWindow(Rect startupPosition);

	void setButtons();
	void setCancelButton();
	void setBuyButton();
	void setMaxButton();

	void setCreaturePurchaseCards();

	void maxAllCards(std::vector<std::shared_ptr<CreaturePurchaseCard>> cards);
	void maxAllSlidersAmount(std::vector<std::shared_ptr<CreaturePurchaseCard>> cards);
	void purchaseUnits();

	const CGTownInstance * town;
	std::shared_ptr<CButton> maxButton;
	std::shared_ptr<CButton> buyButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CreatureCostBox> totalCost;
	std::vector<std::shared_ptr<CreaturePurchaseCard>> cards;
	std::shared_ptr<CFilledTexture> backgroundTexture;
	std::shared_ptr<CPicture> costBackground;
	std::array<std::shared_ptr<CLabel>, 3> categoryHeaders;
	std::array<std::optional<Rect>, 3> categoryGroupRects;
};
