/*
 * TemporalFieldWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "TemporalFieldWindow.h"

#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../render/Colors.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"

namespace
{
constexpr int WINDOW_WIDTH = 450;
constexpr int WINDOW_HEIGHT = 246;
}

TemporalFieldWindow::TemporalFieldWindow(TemporalFieldContext context_)
	: CWindowObject(SHADOW_DISABLED), context(std::move(context_))
{
	pos.w = WINDOW_WIDTH;
	pos.h = WINDOW_HEIGHT;
	moveTo(context.anchor + Point(18, 18));
	fitToScreen(4);

	OBJECT_CONSTRUCTION;
	decoration.push_back(std::make_shared<TransparentFilledRectangle>(
		Rect(0, 0, WINDOW_WIDTH, WINDOW_HEIGHT),
		ColorRGBA(24, 29, 35, 255), ColorRGBA(156, 132, 85, 255)));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 23, FONT_BIG,
		ETextAlignment::CENTER, Colors::YELLOW, "Temporal Field"));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 47, FONT_SMALL,
		ETextAlignment::CENTER, Colors::WHITE, "Choose how to cast Slow"));

	targetLabel = std::make_shared<CMultiLineLabel>(Rect(18, 62, WINDOW_WIDTH - 36, 50),
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, "");
	costLabel = std::make_shared<CLabel>(18, 119, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "");
	magnitudeLabel = std::make_shared<CLabel>(18, 139, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "Mass magnitude: 60% of ordinary Slow (reduced Initiative)");
	stateLabel = std::make_shared<CLabel>(18, 160, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "");

	ordinaryButton = std::make_shared<CButton>(Point(54, 198), AnimationPath::builtin("settingsWindow/button100"),
		CButton::tooltip("Ordinary Slow", "Continue with Slow's normal target and magnitude."),
		[this] { confirmOrdinary(); });
	ordinaryButton->setTextOverlay("Ordinary", FONT_SMALL, Colors::WHITE);
	ordinaryButton->setHoverable(true);

	massButton = std::make_shared<CButton>(Point(174, 198), AnimationPath::builtin("settingsWindow/button100"),
		CButton::tooltip("Mass Slow", "Affect every eligible enemy stack once this combat at 60% magnitude."),
		[this] { confirmMass(); });
	massButton->setTextOverlay("Mass", FONT_SMALL, Colors::WHITE);
	massButton->setHoverable(true);

	cancelButton = std::make_shared<CButton>(Point(294, 198), AnimationPath::builtin("settingsWindow/button100"),
		CButton::tooltip("Cancel", "Return without spending Mana or the Hero Action."),
		[this] { cancel(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
	cancelButton->setHoverable(true);

	refresh();
}

TemporalFieldValues TemporalFieldWindow::values() const
{
	if(context.evaluate)
		return context.evaluate();
	return context.initial;
}

void TemporalFieldWindow::refresh()
{
	const auto current = values();

	targetLabel->setText(current.eligibleEnemyDescription.empty()
		? "Eligible enemies: none"
		: current.eligibleEnemyDescription);
	costLabel->setText("Mana: Ordinary " + std::to_string(current.ordinaryMana)
		+ "   Mass 3x (" + std::to_string(current.massMana) + ")"
		+ "   Available " + std::to_string(current.availableMana));

	const std::string stateText = current.remaining
		? (current.massAffordable
			? "Temporal Field remaining: 1 cast this combat."
			: "Temporal Field remaining: 1 cast, but Mass Slow is not currently legal.")
		: "Temporal Field is no longer available in this combat. Choose Ordinary or Cancel.";
	stateLabel->setText(statusOverride.empty() ? stateText : statusOverride);

	ordinaryButton->block(false);
	massButton->block(!current.remaining || current.eligibleEnemyCount == 0 || !current.massAffordable);
}

void TemporalFieldWindow::confirmOrdinary()
{
	if(!context.confirmOrdinary || context.confirmOrdinary())
		close();
	else
	{
		statusOverride = "The battle state changed. Choose again or Cancel.";
		refresh();
	}
}

void TemporalFieldWindow::confirmMass()
{
	const auto current = values();
	if(!current.remaining || current.eligibleEnemyCount == 0 || !current.massAffordable)
	{
		statusOverride = "Mass Slow is no longer legal. Choose Ordinary or Cancel.";
		refresh();
		return;
	}

	if(!context.confirmMass || context.confirmMass())
		close();
	else
	{
		statusOverride = "The battle state changed. Choose again or Cancel.";
		refresh();
	}
}

void TemporalFieldWindow::cancel()
{
	if(context.cancel)
		context.cancel();
	close();
}

void TemporalFieldWindow::show(Canvas & to)
{
	refresh();
	CWindowObject::show(to);
}

void TemporalFieldWindow::showAll(Canvas & to)
{
	refresh();
	CWindowObject::showAll(to);
}
