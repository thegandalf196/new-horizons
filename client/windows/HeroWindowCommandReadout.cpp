/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CHeroWindow.h"
#include "CommandPresentation.h"
#include "../GameEngine.h"
#include "../render/IRenderHandler.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "../widgets/Images.h"
#include "../widgets/MiscWidgets.h"
#include "../widgets/TextControls.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/texts/CGeneralTextHandler.h"
#include <boost/algorithm/string/replace.hpp>

void CHeroWindow::refreshCommandPresentation()
{
	refreshHero(isActive());
}

void CHeroWindow::refreshLegacyCommandReadout()
{
	const auto command = curHero->getArmyCommand();
	legacyCommandArea->setEnabled(command.has_value());
	legacyLeadershipLabel->setEnabled(command.has_value());
	legacyLeadershipImage->setEnabled(command.has_value());
	if(command)
	{
		legacyCommandArea->text = commandPresentation::commandHelp(*curHero);
		auto label = LIBRARY->generaltexth->translate("new-horizons.command.value");
		boost::replace_all(label, "%COMMAND%", std::to_string(command->effective()));
		legacyCommandArea->hoverText = label;
		legacyLeadershipLabel->setText(label);
	}
}

void CHeroWindow::refreshCommandReadout()
{
	const auto value = curHero->getArmyCommand();
	commandArea->text = commandPresentation::commandHelp(*curHero);
	commandArea->hoverText = commandArea->text;
	commandValue->setText(value ? std::to_string(value->effective()) : "--");
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
	capabilityIcons.push_back(std::make_shared<CPicture>(foreground, Point(157, 89)));
}
