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
#include "../../lib/ResourceSet.h"

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
	int getWeeklyGrowth(int recruitmentLevel) const;
	void updateAllSliders();
	bool isForTown(const CGTownInstance * value) const { return town == value; }
	QuickRecruitmentWindow(const CGTownInstance * townd, Rect startupPosition);

private:
	void initWindow(Rect startupPosition);

	void setButtons();
	void setCancelButton();
	void setBuyButton();
	void setMaxButton();
	void setMusterButton();

	void setCreaturePurchaseCards();

	void maxAllCards(std::vector<std::shared_ptr<CreaturePurchaseCard>> cards);
	void maxAllSlidersAmount(std::vector<std::shared_ptr<CreaturePurchaseCard>> cards);
	void purchaseUnits();
	void updateCompactTotalCost(const TResources & resources);

	const CGTownInstance * town;
	bool categorizedLayout = false;
	std::shared_ptr<CButton> maxButton;
	std::shared_ptr<CButton> musterButton;
	std::shared_ptr<CButton> buyButton;
	std::shared_ptr<CButton> cancelButton;
	std::shared_ptr<CreatureCostBox> totalCost;
	std::vector<std::shared_ptr<CreaturePurchaseCard>> cards;
	std::shared_ptr<CFilledTexture> backgroundTexture;
	std::shared_ptr<CPicture> costBackground;
	std::vector<std::shared_ptr<CIntObject>> compactTotalCostWidgets;
	std::array<std::shared_ptr<CLabel>, 3> categoryHeaders;
};
