/*
 * CSplitWindow.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CWindowObject.h"
#include "../../lib/constants/EntityIdentifiers.h"
#include <functional>
#include <memory>
#include <string>

class CArmedInstance;
class CCreature;
class CButton;
class CLabel;
class CSlider;
class CCreaturePic;
class CTextInput;
class CAnimImage;
class CMultiLineLabel;
class LRClickableAreaWText;

/// Each endpoint retains its physical army position for permission and bonus previews.
struct CSplitWindowOwner
{
	const CArmedInstance * army = nullptr;
	std::string label;
	SlotID slot;
};

class CSplitWindow : public CWindowObject
{
	std::function<void(int, int)> callback;
	int leftAmount;
	int rightAmount;
	int initialLeftAmount;
	int initialRightAmount;
	const CCreature * splitCreature;
	const CArmedInstance * leftArmy;
	const CArmedInstance * rightArmy;
	SlotID leftSlot;
	SlotID rightSlot;
	int leftMin;
	int rightMin;
	std::shared_ptr<CLabel> title;
	std::shared_ptr<CSlider> slider;
	std::shared_ptr<CCreaturePic> animLeft;
	std::shared_ptr<CCreaturePic> animRight;
	std::shared_ptr<CButton> ok;
	std::shared_ptr<CButton> cancel;
	std::shared_ptr<CTextInput> leftInput;
	std::shared_ptr<CTextInput> rightInput;
	std::shared_ptr<CAnimImage> leftOwnerMarker;
	std::shared_ptr<CAnimImage> rightOwnerMarker;
	std::shared_ptr<LRClickableAreaWText> leftLeadershipHelp;
	std::shared_ptr<LRClickableAreaWText> rightLeadershipHelp;
	std::shared_ptr<CMultiLineLabel> leftOwnerLabel;
	std::shared_ptr<CMultiLineLabel> rightOwnerLabel;
	void setAmountText(std::string text, bool left);
	void setAmount(int value, bool left);
	void updateLeadershipReadback();
	void sliderMoved(int value);
	void apply();

public:
	CSplitWindow(const CCreature * creature, std::function<void(int, int)> callback, int leftMin, int rightMin,
		int leftAmount, int rightAmount, CSplitWindowOwner leftOwner = {}, CSplitWindowOwner rightOwner = {});
};
