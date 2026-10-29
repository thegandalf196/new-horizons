/* New Horizons local report notice. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "BugReportNoticeWindow.h"
#include "CMessage.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"

BugReportNoticeWindow::BugReportNoticeWindow(const std::string & message)
	: CWindowObject(0)
{
	pos.w = 620;
	pos.h = 250;
	updateShadow();
	center();
	OBJECT_CONSTRUCTION;
	texture = std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h));
	controls.push_back(std::make_shared<CLabel>(310, 28, FONT_BIG,
		ETextAlignment::CENTER, Colors::YELLOW, "Diagnostic attachment saved"));
	controls.push_back(std::make_shared<CMultiLineLabel>(Rect(28, 55, 564, 140),
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE, message));
	auto closeButton = std::make_shared<CButton>(Point(270, 202), AnimationPath::builtin("settingsWindow/button80"),
		CButton::tooltip("Back"), [this] { close(); }, EShortcut::GLOBAL_RETURN);
	closeButton->setTextOverlay("Back", FONT_SMALL, Colors::WHITE);
	controls.push_back(closeButton);
}

void BugReportNoticeWindow::showAll(Canvas & to)
{
	CWindowObject::showAll(to);
	CMessage::drawBorder(PlayerColor(1), to, pos.w, pos.h, pos.x, pos.y);
}
