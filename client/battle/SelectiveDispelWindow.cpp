/*
 * SelectiveDispelWindow.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "SelectiveDispelWindow.h"

#include "../GameEngine.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/GraphicalPrimitiveCanvas.h"
#include "../widgets/TextControls.h"
#include "../render/Colors.h"

namespace
{
constexpr int WINDOW_WIDTH = 430;
constexpr int WINDOW_HEIGHT = 190;
}

SelectiveDispelWindow::SelectiveDispelWindow(SelectiveDispelContext context_)
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
		ETextAlignment::CENTER, Colors::YELLOW, "Dispel"));
	decoration.push_back(std::make_shared<CLabel>(WINDOW_WIDTH / 2, 48, FONT_SMALL,
		ETextAlignment::CENTER, Colors::WHITE, "Choose how to remove temporary magical effects"));

	targetLabel = std::make_shared<CMultiLineLabel>(Rect(18, 63, WINDOW_WIDTH - 36, 38),
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, context.targetDescription);
	stateLabel = std::make_shared<CLabel>(18, 112, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::YELLOW, "Full removes all effects. Selective preserves favorable effects.");

	fullButton = std::make_shared<CButton>(Point(70, 142), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Full Dispel", "Remove every dispellable temporary magical effect from the target."),
		[this] { confirm(false); });
	fullButton->setTextOverlay("Full", FONT_SMALL, Colors::WHITE);
	fullButton->setHoverable(true);

	selectiveButton = std::make_shared<CButton>(Point(174, 142), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Selective Dispel", "On an ally remove only hostile effects; on an enemy remove only beneficial effects."),
		[this] { confirm(true); });
	selectiveButton->setTextOverlay("Selective", FONT_SMALL, Colors::WHITE);
	selectiveButton->setHoverable(true);

	cancelButton = std::make_shared<CButton>(Point(278, 142), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Cancel", "Return without spending Mana or the Hero Action."),
		[this] { cancel(); }, EShortcut::GLOBAL_CANCEL);
	cancelButton->setTextOverlay("Cancel", FONT_SMALL, Colors::WHITE);
	cancelButton->setHoverable(true);
}

void SelectiveDispelWindow::confirm(bool selective)
{
	if(!context.confirm || context.confirm(selective))
		close();
	else
		stateLabel->setText("The battle state changed. Choose again or Cancel.");
}

void SelectiveDispelWindow::cancel()
{
	if(context.cancel)
		context.cancel();
	close();
}
