/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleStacksController.h"
#include "BattleFieldController.h"
#include "BattleInterface.h"
#include "ShieldPresentation.h"
#include "render/Canvas.h"
#include "render/Colors.h"
#include "render/EFont.h"
#include "render/IFont.h"
#include "render/IImage.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/texts/TextOperations.h"

void BattleStacksController::showStackAmountBox(Canvas & canvas, const CStack * stack)
{
	auto amountBG = getDisplayedStackAmountBox(stack);

	bool doubleWide = stack->doubleWide();
	bool turnedRight = facingRight(stack);
	bool attacker = stack->unitSide() == BattleSide::ATTACKER;

	BattleHex stackPos = stack->getPosition();

	// double-wide unit turned around - use opposite hex for stack label
	if (doubleWide && turnedRight != attacker)
		stackPos = stack->occupiedHex();

	BattleHex frontPos = turnedRight ?
		stackPos.cloneInDirection(BattleHex::RIGHT) :
		stackPos.cloneInDirection(BattleHex::LEFT);

	bool moveInside = !owner.fieldController->stackCountOutsideHex(frontPos);

	Point boxPosition;

	if (moveInside)
	{
		boxPosition = owner.fieldController->hexPositionLocal(stackPos).center() + Point(-15, 1);
	}
	else
	{
		if (turnedRight)
			boxPosition = owner.fieldController->hexPositionLocal(frontPos).center() + Point (-22, 1);
		else
			boxPosition = owner.fieldController->hexPositionLocal(frontPos).center() + Point(-8, -14);
	}

	Point textPosition = Point(amountBG->dimensions().x/2 + boxPosition.x, boxPosition.y + amountBG->dimensions().y/2);

	int32_t displayedCount = getDisplayedStackAmount(stack);

	if(settings["battle"]["showHealthBar"].Bool())
	{
		double healthMaxType = stack->unitType()->getMaxHealth();
		double healthMaxStack = stack->getMaxHealth();
		double healthMaxRatio = std::min(healthMaxStack / healthMaxType, 1.0);
		double healthRemaining = std::max(getDisplayedStackHealth(stack) - (displayedCount - 1) * healthMaxStack, .0) * healthMaxRatio;
		Rect r(boxPosition.x, boxPosition.y - 3, amountBG->width(), 4);
		canvas.drawColor(r, Colors::RED);
		canvas.drawColor(Rect(r.x, r.y, (r.w / healthMaxStack) * healthRemaining, r.h), Colors::GREEN);
		canvas.drawBorder(r, Colors::YELLOW);
	}
	const auto shieldBar = shieldPresentation::bar(getDisplayedStackShields(stack), boxPosition, amountBG->width());
	if(shieldBar.fillWidth > 0)
	{
		canvas.drawColor(shieldBar.bounds, Colors::BLACK);
		canvas.drawColor(Rect(shieldBar.bounds.x, shieldBar.bounds.y, shieldBar.fillWidth, shieldBar.bounds.h), Colors::WHITE);
	}
	canvas.draw(amountBG, boxPosition);
	canvas.drawText(textPosition, EFonts::FONT_TINY, Colors::WHITE, ETextAlignment::CENTER, TextOperations::formatMetric(displayedCount, 4));

	showLeadershipEfficiency(canvas, stack, boxPosition, amountBG->width(), amountBG->height(), displayedCount);
}

void BattleStacksController::lockStackAmountBox(const CStack * stack)
{
	displayedStackSnapshot.try_emplace(stack->unitId(), DisplayedStackSnapshot{stack->getCount(), stack->getAvailableHealth(), getStackAmountBox(stack), battle::shieldSnapshot(*stack)});
}

void BattleStacksController::unlockStackAmountBox(uint32_t stackID)
{
	displayedStackSnapshot.erase(stackID);
}

int32_t BattleStacksController::getDisplayedStackAmount(const CStack * stack) const
{
	auto it = displayedStackSnapshot.find(stack->unitId());
	if(it != displayedStackSnapshot.end())
		return it->second.count;
	return stack->getCount();
}

int64_t BattleStacksController::getDisplayedStackHealth(const CStack * stack) const
{
	auto it = displayedStackSnapshot.find(stack->unitId());
	if(it != displayedStackSnapshot.end())
		return it->second.availableHealth;
	return stack->getAvailableHealth();
}

std::shared_ptr<IImage> BattleStacksController::getDisplayedStackAmountBox(const CStack * stack)
{
	auto it = displayedStackSnapshot.find(stack->unitId());
	if(it != displayedStackSnapshot.end())
		return it->second.amountBox;
	return getStackAmountBox(stack);
}

battle::ShieldSnapshot BattleStacksController::getDisplayedStackShields(const CStack * stack) const
{
	const auto it = displayedStackSnapshot.find(stack->unitId());
	if(it != displayedStackSnapshot.end())
		return it->second.shields;
	return battle::shieldSnapshot(*stack);
}
