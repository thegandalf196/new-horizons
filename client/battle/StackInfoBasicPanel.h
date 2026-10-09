/*
 * StackInfoBasicPanel.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "BattleSidePanel.h"
#include "NewHorizonsBattleStatus.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

class CStack;
class CPlayerBattleCallback;

class CLabel;
class CMultiLineLabel;
class CAnimImage;
class CPicture;
class LRClickableAreaWText;

class StackInfoBasicPanel : public BattleSidePanel
{
private:
	std::shared_ptr<CPicture> background;
	std::shared_ptr<CPicture> background2;
	std::vector<std::shared_ptr<CLabel>> labels;
	std::vector<std::shared_ptr<CMultiLineLabel>> labelsMultiline;
	std::vector<std::shared_ptr<CAnimImage>> icons;
	std::vector<std::shared_ptr<CPicture>> temporaryCreatureIcons;
	std::vector<std::shared_ptr<CIntObject>> physicalStatusIcons;
	std::vector<std::shared_ptr<LRClickableAreaWText>> statusTooltips;
	std::shared_ptr<CPlayerBattleCallback> battleCallback;
	newHorizonsBattleStatus::StackInfoStatusSnapshot displayedStatus;
	newHorizonsBattleStatus::BattleMoraleReadback displayedMoraleReadback;
	newHorizonsBattleStatus::BattleLuckReadback displayedLuckReadback;
	std::string displayedSoulChainSignature;
	int displayedMorale = 0;
	std::optional<newHorizonsBattleStatus::BattleBonusDescriptionCacheKey> bonusDescriptionCacheKey;
	std::vector<std::string> cachedLuckBonusDescriptions;
	std::vector<std::string> cachedMoraleBonusDescriptions;
	void refreshBonusDescriptionCache(const CStack * stack);

public:
	StackInfoBasicPanel(
		const CStack * stack, std::shared_ptr<CPlayerBattleCallback> battleCallback, bool initializeBackground);

	void initializeData(const CStack * stack);
	void update(const CStack * updatedInfo);
	void refreshDefendStatus(const CStack * updatedInfo);
	bool containsPoint(const Point & point) const;
};
