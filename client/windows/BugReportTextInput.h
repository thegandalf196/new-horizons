/* New Horizons report text input. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../widgets/CTextInput.h"

// Uses the existing focus/IME lifecycle; adds bounded multiline report editing.
class BugReportTextInput final : public CTextInput
{
	size_t caret = 0;
	size_t capacity;
	bool multiline;
	void refresh();
	void onFocusGot() override;
	void onFocusLost() override;
	void clickPressed(const Point & point) override;
	void textInputted(const std::string & text) override;
	void textEdited(const std::string & text) override;
	void keyPressed(const std::string & key) override;
	void keyPressed(EShortcut key) override;
	bool captureThisKey(EShortcut key) override;
public:
	BugReportTextInput(const Rect & area, size_t capacity, bool multiline = true);
	void setValue(const std::string & text);
	void showAll(Canvas & to) override;
};
