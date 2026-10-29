/* New Horizons report text input. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "BugReportTextInput.h"

#include "CMessage.h"
#include "../gui/Shortcut.h"
#include "render/Canvas.h"
#include "../widgets/TextControls.h"
#include "../../lib/texts/TextOperations.h"

BugReportTextInput::BugReportTextInput(const Rect & area, size_t limit, bool multipleLines)
	: CTextInput(area), capacity(limit), multiline(multipleLines)
{
	OBJECT_CONSTRUCTION;
	// A continuous recessed field over the ordinary dialog material.
	background.reset();
	label = std::make_shared<CMultiLineLabel>(Rect(4, 3, area.w - 8, area.h - 6),
		FONT_SMALL, ETextAlignment::TOPLEFT, Colors::WHITE);
	setRedrawParent(true);
	addUsedEvents(KEY_NAME);
	refresh();
}

void BugReportTextInput::setValue(const std::string & text)
{
	if(text.size() > capacity)
		return;
	currentText = text;
	caret = currentText.size();
	composedText.clear();
	refresh();
}

void BugReportTextInput::refresh()
{
	auto wrapped = std::static_pointer_cast<CMultiLineLabel>(label);
	std::string shown = currentText;
	if(hasFocus())
		shown.insert(caret, composedText + "|");
	wrapped->setText(shown);
	const auto prefixLines = CMessage::breakText(shown.substr(0, caret), label->pos.w, FONT_SMALL);
	const int lineHeight = CMessage::guessHeight("A", label->pos.w, FONT_SMALL);
	const int caretBottom = static_cast<int>(prefixLines.size()) * lineHeight;
	wrapped->scrollTextTo(std::max(0, caretBottom - label->pos.h));
}

void BugReportTextInput::onFocusGot() { refresh(); }
void BugReportTextInput::onFocusLost() { composedText.clear(); refresh(); }
void BugReportTextInput::clickPressed(const Point & point) { giveFocus(); }

void BugReportTextInput::textInputted(const std::string & text)
{
	if(!hasFocus() || currentText.size() + text.size() > capacity)
		return;
	std::string inserted = text;
	if(!multiline)
		std::replace_if(inserted.begin(), inserted.end(), [](char c) { return c == '\r' || c == '\n'; }, ' ');
	currentText.insert(caret, inserted);
	caret += inserted.size();
	composedText.clear();
	refresh();
}

void BugReportTextInput::textEdited(const std::string & text)
{
	if(hasFocus())
	{
		composedText = text;
		refresh();
	}
}

bool BugReportTextInput::captureThisKey(EShortcut key)
{
	return hasFocus() && (key == EShortcut::GLOBAL_ACCEPT || key == EShortcut::GLOBAL_BACKSPACE
		|| key == EShortcut::GLOBAL_MOVE_FOCUS || key == EShortcut::MOVE_LEFT
		|| key == EShortcut::MOVE_RIGHT || key == EShortcut::MOVE_FIRST || key == EShortcut::MOVE_LAST);
}

void BugReportTextInput::keyPressed(EShortcut key)
{
	if(!hasFocus())
		return;
	if(key == EShortcut::GLOBAL_MOVE_FOCUS)
	{
		moveFocus();
		return;
	}
	if(key == EShortcut::GLOBAL_ACCEPT && multiline)
		textInputted("\n");
	else if((key == EShortcut::GLOBAL_BACKSPACE || key == EShortcut::MOVE_LEFT) && caret)
	{
		auto prefix = currentText.substr(0, caret);
		TextOperations::trimRightUnicode(prefix);
		if(key == EShortcut::GLOBAL_BACKSPACE)
			currentText.erase(prefix.size(), caret - prefix.size());
		caret = prefix.size();
	}
	else if(key == EShortcut::MOVE_RIGHT && caret < currentText.size())
		caret += TextOperations::getUnicodeCharacterSize(currentText[caret]);
	else if(key == EShortcut::MOVE_FIRST)
		caret = 0;
	else if(key == EShortcut::MOVE_LAST)
		caret = currentText.size();
	composedText.clear();
	refresh();
}

void BugReportTextInput::keyPressed(const std::string & key)
{
	if(hasFocus() && key == "Delete" && caret < currentText.size())
	{
		currentText.erase(caret, TextOperations::getUnicodeCharacterSize(currentText[caret]));
		refresh();
	}
}

void BugReportTextInput::showAll(Canvas & to)
{
	to.drawColorBlended(pos, ColorRGBA(0, 0, 0, 100));
	to.drawBorder(pos, ColorRGBA(0, 0, 0, 220));
	const auto topLeft = pos.topLeft();
	const auto bottomRight = Point(pos.x + pos.w - 1, pos.y + pos.h - 1);
	const auto highlight = hasFocus() ? ColorRGBA(180, 146, 85) : ColorRGBA(100, 80, 60);
	to.drawLine(Point(topLeft.x, bottomRight.y), bottomRight, highlight, highlight);
	to.drawLine(Point(bottomRight.x, topLeft.y), bottomRight, highlight, highlight);
	CTextInput::showAll(to);
}
