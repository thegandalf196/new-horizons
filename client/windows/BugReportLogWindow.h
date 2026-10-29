/* New Horizons reviewed diagnostic editor. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "CWindowObject.h"

class BugReportTextInput;
class CFilledTexture;

class BugReportLogWindow final : public CWindowObject
{
	std::shared_ptr<CFilledTexture> texture;
	std::shared_ptr<BugReportTextInput> editor;
	std::vector<std::shared_ptr<CIntObject>> controls;
	std::function<void(std::string)> accept;
public:
	BugReportLogWindow(std::string text, std::function<void(std::string)> accept);
	void showAll(Canvas & to) override;
};
