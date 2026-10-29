/* New Horizons reviewed diagnostic editor. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "BugReportLogWindow.h"
#include "BugReportTextInput.h"
#include "CMessage.h"
#include "../gui/Shortcut.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"

BugReportLogWindow::BugReportLogWindow(std::string text, std::function<void(std::string)> callback)
	: CWindowObject(0), accept(std::move(callback))
{
	pos.w = 620;
	pos.h = 556;
	updateShadow();
	center();
	OBJECT_CONSTRUCTION;
	texture = std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h));
	controls.push_back(std::make_shared<CLabel>(310, 28, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Review diagnostic log"));
	controls.push_back(std::make_shared<CMultiLineLabel>(Rect(28, 49, 564, 46), FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::WHITE,
		"Automatic sanitization can miss private information. Read this text and remove anything you do not want to share. Nothing is uploaded here."));
	editor = std::make_shared<BugReportTextInput>(Rect(28, 100, 564, 376), 65536);
	editor->setValue(text);
	controls.push_back(std::make_shared<CLabel>(28, 486, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE,
		"Arrow keys / Home / End move the cursor; Backspace / Delete remove text."));
	auto button = [this](int x, const std::string & title, std::function<void()> action, EShortcut key = EShortcut::NONE)
	{
		auto result = std::make_shared<CButton>(Point(x, 512), AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip(title), std::move(action), key);
		result->setTextOverlay(title, FONT_SMALL, Colors::WHITE);
		controls.push_back(result);
	};
	button(28, "Clear", [this] { editor->setValue(""); });
	button(425, "Use log", [this] { const auto text = editor->getText(); const auto callback = accept; close(); callback(text); });
	button(517, "Cancel", [this] { close(); }, EShortcut::GLOBAL_CANCEL);
	editor->giveFocus();
}

void BugReportLogWindow::showAll(Canvas & to)
{
	CWindowObject::showAll(to);
	CMessage::drawBorder(PlayerColor(1), to, pos.w, pos.h, pos.x, pos.y);
}
