/*
 * ShadowGiftWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ShadowGiftWindow.h"

#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"
#include "render/Canvas.h"
#include "render/CanvasImage.h"
#include "render/IImage.h"
#include "render/IRenderHandler.h"

#include <algorithm>
#include <cstdlib>
#include <iterator>

namespace
{
constexpr int WINDOW_WIDTH = 340;
constexpr int WINDOW_HEIGHT = 252;
constexpr int SACRIFICE_CHOICES[] = {10, 20, 30};

std::string formatSignedBasisPoints(int32_t basisPoints)
{
	const bool positive = basisPoints >= 0;
	const int64_t absolute = std::abs(static_cast<int64_t>(basisPoints));
	std::string result = positive ? "+" : "-";
	result += std::to_string(absolute / 100);
	if(absolute % 100 != 0)
	{
		std::string fraction = std::to_string(absolute % 100 + 100).substr(1);
		if(fraction.back() == '0')
			fraction.pop_back();
		result += "." + fraction;
	}
	return result + "%";
}
}

ShadowGiftWindow::ShadowGiftWindow(ShadowGiftContext context_)
	: CWindowObject(BORDERED), context(std::move(context_)), selectedPercent(context.initial.sacrificePercent)
{
	pos.w = WINDOW_WIDTH;
	pos.h = WINDOW_HEIGHT;
	center();

	OBJECT_CONSTRUCTION;
	auto leather = ENGINE->renderHandler().createImage(Point(WINDOW_WIDTH, WINDOW_HEIGHT), CanvasScalingPolicy::AUTO);
	auto canvas = leather->getCanvas();
	canvas.fillTexture(ENGINE->renderHandler().loadImage(
		ImageLocator(ImagePath::builtin("DiBoxBck"), EImageBlitMode::OPAQUE)));
	background = std::make_shared<CPicture>(std::static_pointer_cast<IImage>(leather), Point(0, 0));

	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 18, FONT_MEDIUM,
		ETextAlignment::CENTER, Colors::YELLOW, "Shadow Gift"));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 35, FONT_TINY,
		ETextAlignment::CENTER, Colors::WHITE, "Choose the stack's sacrifice"));

	targetLabel = std::make_shared<CMultiLineLabel>(Rect(12, 42, WINDOW_WIDTH - 24, 24),
		FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	costLabel = std::make_shared<CMultiLineLabel>(Rect(12, 68, WINDOW_WIDTH - 24, 45),
		FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	damageLabel = std::make_shared<CLabel>(12, 117, FONT_SMALL, ETextAlignment::TOPLEFT, Colors::YELLOW, "");
	stateLabel = std::make_shared<CLabel>(12, 137, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE, "");

	for(int index = 0; index < 3; ++index)
	{
		const int percent = SACRIFICE_CHOICES[index];
		const int x = 14 + index * 108;
		auto choice = std::make_shared<CButton>(Point(x, 164), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip(std::to_string(percent) + "% sacrifice",
				"Exchange this share of the friendly stack's vitality for three rounds of Shadow damage."),
			[this, percent] { select(percent); });
		choice->setHoverable(true);
		choices.push_back(std::move(choice));
	}

	confirmButton = std::make_shared<CButton>(Point(72, 208), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Confirm Shadow Gift", "Submit this target and sacrifice choice through the normal hero spell request."),
		[this] { confirm(); });
	confirmButton->setHoverable(true);
	confirmButton->setTextOverlay("Confirm", FONT_SMALL, Colors::WHITE);

	cancelButton = std::make_shared<CButton>(Point(176, 208), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Cancel", "Discard the choice without spending Mana, HP, or the Hero Action."),
		[this] { cancel(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setHoverable(true);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);

	refresh();
}

ShadowGiftValues ShadowGiftWindow::values() const
{
	if(context.evaluate)
		return context.evaluate(selectedPercent);

	auto result = context.initial;
	result.sacrificePercent = selectedPercent;
	return result;
}

void ShadowGiftWindow::select(int percent)
{
	if(std::ranges::find(std::begin(SACRIFICE_CHOICES), std::end(SACRIFICE_CHOICES), percent)
		== std::end(SACRIFICE_CHOICES))
		return;
	selectedPercent = percent;
	refresh();
}

void ShadowGiftWindow::refresh()
{
	const auto current = values();
	if(targetLabel)
		targetLabel->setText("Target: " + current.targetDescription);

	const auto currentLoss = std::max<int64_t>(0, current.currentHealth - current.currentHealthAfter);
	const auto capLoss = std::max<int64_t>(0, current.maximumHealth - current.maximumHealthAfter);
	costLabel->setText("Current creature HP: " + std::to_string(current.currentHealth) + " -> "
		+ std::to_string(current.currentHealthAfter) + "  (-" + std::to_string(currentLoss) + ")"
		+ "\nMaximum creature HP: " + std::to_string(current.maximumHealth) + " -> "
		+ std::to_string(current.maximumHealthAfter) + "  (-" + std::to_string(capLoss) + ")");
	damageLabel->setText(formatSignedBasisPoints(current.damageBonusBasisPoints)
		+ " Shadow damage for 3 rounds");
	stateLabel->setText(current.legal ? "Temporary HP is preserved."
		: "Temporary HP is preserved; this sacrifice is no longer legal.");

	for(std::size_t index = 0; index < choices.size(); ++index)
	{
		const int percent = SACRIFICE_CHOICES[index];
		choices[index]->setTextOverlay(std::to_string(percent) + "%" + (percent == selectedPercent ? " *" : ""),
			FONT_SMALL, percent == selectedPercent ? Colors::YELLOW : Colors::WHITE);
	}
	confirmButton->block(!current.legal || !context.confirm);
}

void ShadowGiftWindow::confirm()
{
	const auto current = values();
	if(!current.legal || !context.confirm)
	{
		refresh();
		return;
	}

	if(context.confirm(selectedPercent))
		close();
	else
	{
		stateLabel->setText("The battle changed. Cancel and choose the spell again.");
		confirmButton->block(true);
	}
}

void ShadowGiftWindow::cancel()
{
	if(context.cancel)
		context.cancel();
	close();
}

void ShadowGiftWindow::show(Canvas & to)
{
	CWindowObject::show(to);
}

void ShadowGiftWindow::showAll(Canvas & to)
{
	CWindowObject::showAll(to);
}
