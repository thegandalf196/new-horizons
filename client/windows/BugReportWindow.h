/* New Horizons bug report form. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <boost/filesystem/path.hpp>
#include "CWindowObject.h"
#include "../bugreport/BugReportDraft.h"

class BugReportTextInput;
class CButton;
class CToggleButton;
class CLabel;
class CMultiLineLabel;
class CFilledTexture;

class BugReportWindow final : public CWindowObject
{
	bugReport::Draft draft;
	std::array<std::shared_ptr<BugReportTextInput>, 5> inputs;
	std::vector<std::shared_ptr<CIntObject>> decorations;
	std::shared_ptr<CFilledTexture> texture;
	std::shared_ptr<CToggleButton> includeLog;
	std::shared_ptr<CButton> reviewButton;
	std::shared_ptr<CButton> addLogButton;
	std::shared_ptr<CButton> copyButton;
	std::shared_ptr<CMultiLineLabel> status;
	boost::filesystem::path logSource;
	std::string reviewedLog;
	bool reviewed = false;
	std::string copiedBody;
	std::function<void()> afterClose;

	void reviewLog();
	void addLog();
	void copyReport();
	void readFields();
	void openGitHub();
	void dismiss();
public:
	BugReportWindow(bugReport::Draft draft, boost::filesystem::path logSource, std::function<void()> afterClose);
	// Empty session directory opens a manual report for the current client log.
	static void openSession(const std::string & sessionDirectory, std::function<void()> onClose = nullptr);
	void showAll(Canvas & to) override;
};
