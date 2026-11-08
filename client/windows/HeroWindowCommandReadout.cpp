/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CHeroWindow.h"
#include "../CPlayerInterface.h"
#include "../GameEngine.h"
#include "../render/IRenderHandler.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "../widgets/Images.h"
#include "../battle/BattleInterface.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

void CHeroWindow::refreshCommandReadout()
{
	// This helper returns a detached capacity calculation; it never initializes
	// or replenishes a battle. Only authoritative battle acceptance spends it.
	const auto capacity = newHorizonsCommand::initialState(curHero);
	commandValue->setText(capacity.initialized ? std::to_string(capacity.maximum) : "--");
	commandArea->text = capacity.initialized
		? "Maximum Command per battle: " + std::to_string(capacity.maximum)
			+ ". A new battle starts with this capacity. Each Order costs 1 Command and the shared Hero Action. Command does not regenerate during combat."
		: "No saved Command rules are available for this hero.";
	const auto battleInterface = CPlayerInterface::battleInt;
	const auto callback = battleInterface ? battleInterface->getBattle() : nullptr;
	if(!callback || !callback->getBattle())
		return;
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		const auto * fightingHero = callback->battleGetFightingHero(side);
		if(!fightingHero || fightingHero->id != curHero->id)
			continue;
		const auto & pool = callback->getBattle()->getCommandResources(side);
		if(pool.initialized)
		{
			commandValue->setText(std::to_string(pool.remaining) + "/" + std::to_string(pool.maximum));
			commandArea->text = "Command remaining / maximum in this battle: "
				+ std::to_string(pool.remaining) + "/" + std::to_string(pool.maximum)
				+ ". Each Order costs 1 Command and the shared Hero Action. Spent Command is not replenished during combat.";
		}
		return;
	}
}

void CHeroWindow::alignAttributeReadouts()
{
	const auto centerGroup = [this](const std::shared_ptr<CLabel> & base,
		const std::shared_ptr<CLabel> & growth, int center, int y)
	{
		const int baseWidth = static_cast<int>(base->getWidth());
		const int growthWidth = static_cast<int>(growth->getWidth());
		const int gap = growthWidth ? 4 : 0;
		const int left = center - (baseWidth + gap + growthWidth) / 2;
		base->moveTo(pos.topLeft() + Point(left + baseWidth / 2, y));
		growth->moveTo(pos.topLeft() + Point(left + baseWidth + gap + growthWidth / 2, y));
	};
	for(size_t i = 0; i < primSkillValues.size(); ++i)
		centerGroup(primSkillValues[i], growthValues[i], 277 + static_cast<int>(i) * 82, 80);
	if(leadershipValue && leadershipGrowthValue)
		centerGroup(leadershipValue, leadershipGrowthValue, 246, 118);
}

void CHeroWindow::addCommandIcon()
{
	OBJECT_CONSTRUCTION_TARGETED(this);
	// The selected48x36 foreground has wide transparent margins. Crop only
	// that padding into an owned image before uniformly fitting the native slot;
	// never scale or edit the shared source used by the Orders button.
	auto source = ENGINE->renderHandler().loadImage(AnimationPath::builtin("NH_orders_command"),
		0, 0, EImageBlitMode::COLORKEY);
	auto foreground = ENGINE->renderHandler().createImage(Point(27, 31), CanvasScalingPolicy::AUTO);
	{
		auto canvas = foreground->getCanvas();
		canvas.applyTransparency(true);
		canvas.draw(source, Point(0, 0), Rect(10, 2, 27, 31));
	}
	foreground->scaleTo(Point(37, 42), EScalingAlgorithm::BILINEAR);
	capabilityIcons.push_back(std::make_shared<CPicture>(foreground, Point(297, 133)));
}
