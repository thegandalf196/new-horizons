/* New Horizons bug report form. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "BugReportWindow.h"
#include "BugReportLogWindow.h"
#include "BugReportTextInput.h"
#include "CMessage.h"
#include "../GameEngine.h"
#include "../bugreport/BugReportData.h"
#include "../gui/Shortcut.h"
#include "../gui/WindowHandler.h"
#include "../widgets/Buttons.h"
#include "../widgets/Images.h"
#include "../widgets/TextControls.h"
#include "events/InputHandler.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/json/JsonNode.h"
#include <boost/filesystem/fstream.hpp>

BugReportWindow::BugReportWindow(bugReport::Draft initial, boost::filesystem::path source, std::function<void()> onClose)
	: CWindowObject(0), draft(std::move(initial)), logSource(std::move(source)), afterClose(std::move(onClose))
{
	pos.w = 620;
	pos.h = 556;
	updateShadow();
	center();
	OBJECT_CONSTRUCTION;
	texture = std::make_shared<CFilledTexture>(ImagePath::builtin("DiBoxBck"), Rect(0, 0, pos.w, pos.h));
	decorations.push_back(std::make_shared<CLabel>(310, 28, FONT_BIG, ETextAlignment::CENTER, Colors::YELLOW, "Report a bug"));
	decorations.push_back(std::make_shared<CLabel>(310, 48, FONT_SMALL, ETextAlignment::CENTER, Colors::WHITE,
		(draft.kind == "unclean-exit" ? "Reporter: New Horizons " : "New Horizons ") + draft.reporterVersion + " | Public GitHub issue"));
	const std::array<std::string, 5> titles = {"Title", "What happened?", "Steps to reproduce", "Expected result", "Actual result"};
	const std::array<int, 5> tops = {83, 132, 209, 300, 363};
	const std::array<int, 5> heights = {24, 52, 66, 38, 38};
	for(size_t i = 0; i < inputs.size(); ++i)
	{
		decorations.push_back(std::make_shared<CLabel>(28, tops[i] - 17, FONT_SMALL,
			ETextAlignment::TOPLEFT, Colors::YELLOW, titles[i]));
		inputs[i] = std::make_shared<BugReportTextInput>(Rect(28, tops[i], 564, heights[i]), i ? 1200 : 120, i != 0);
		inputs[i]->setValue(draft.fields[i]);
	}
	includeLog = std::make_shared<CToggleButton>(Point(28, 413), AnimationPath::builtin("SYSOPCHK"),
		CButton::tooltip("Include a sanitized log"),
		[this](bool enabled)
		{
			reviewButton->block(!enabled);
			addLogButton->block(true);
			copyButton->block(enabled);
			reviewed = false;
			reviewedLog.clear();
			draft.reviewedLog.clear();
			copiedBody.clear();
			status->setText("No diagnostic log will be included.");
			if(enabled)
				reviewLog();
		});
	includeLog->setSelectedSilent(false);
	decorations.push_back(std::make_shared<CLabel>(68, 417, FONT_SMALL, ETextAlignment::TOPLEFT,
		Colors::WHITE, "Include a sanitized log (optional)"));
	auto button = [this](Point position, const std::string & title, std::function<void()> action, EShortcut key = EShortcut::NONE)
	{
		auto result = std::make_shared<CButton>(position, AnimationPath::builtin("settingsWindow/button80"),
			CButton::tooltip(title), std::move(action), key);
		result->setTextOverlay(title, FONT_SMALL, Colors::WHITE);
		decorations.push_back(result);
		return result;
	};
	reviewButton = button(Point(28, 445), "Review log", [this] { reviewLog(); });
	addLogButton = std::make_shared<CButton>(Point(120, 445), AnimationPath::builtin("settingsWindow/button190"),
		CButton::tooltip("Add log to issue description"), [this] { addLog(); });
	addLogButton->setTextOverlay("Add log to issue description", FONT_SMALL, Colors::WHITE);
	decorations.push_back(addLogButton);
	copyButton = std::make_shared<CButton>(Point(322, 445), AnimationPath::builtin("settingsWindow/button190"),
		CButton::tooltip("Copy full report", "For long reports, copy everything here and paste it into the GitHub issue description."),
		[this] { copyReport(); });
	copyButton->setTextOverlay("Copy full report", FONT_SMALL, Colors::WHITE);
	decorations.push_back(copyButton);
	reviewButton->block(true);
	addLogButton->block(true);
	copyButton->block(false);
	status = std::make_shared<CMultiLineLabel>(Rect(28, 480, 564, 32), FONT_SMALL,
		ETextAlignment::TOPLEFT, Colors::YELLOW, "No diagnostic log will be included.");
	decorations.push_back(std::make_shared<CLabel>(28, 521, FONT_TINY, ETextAlignment::TOPLEFT, Colors::WHITE,
		"thegandalf196/new-horizons"));
	button(Point(425, 512), "Continue", [this] { openGitHub(); });
	button(Point(517, 512), "Cancel", [this] { dismiss(); }, EShortcut::GLOBAL_CANCEL);
	inputs[0]->giveFocus();
}

void BugReportWindow::reviewLog()
{
	if(!includeLog->isSelected())
		return;
	try
	{
		const auto text = reviewed ? reviewedLog : bugReport::readSanitizedLog(logSource);
		if(text.empty() || text.size() > 65536)
		{
			status->setText("No reviewable log is available (64 KiB maximum).");
			return;
		}
		ENGINE->windows().createAndPushWindow<BugReportLogWindow>(text, [this](std::string text)
		{
			reviewedLog = std::move(text);
			reviewed = !reviewedLog.empty();
			draft.reviewedLog.clear();
			copiedBody.clear();
			copyButton->block(includeLog->isSelected());
			addLogButton->block(!reviewed || !includeLog->isSelected());
			status->setText(reviewed ? "Reviewed log ready. Add it to the issue description." : "Log cleared; no log will be included.");
		});
	}
	catch(const std::exception &)
	{
		status->setText("The diagnostic log could not be read. You can still report the bug.");
	}
}

void BugReportWindow::addLog()
{
	if(!includeLog->isSelected() || !reviewed || reviewedLog.empty())
		return;
	draft.reviewedLog = reviewedLog;
	copyButton->block(false);
	copiedBody.clear();
	status->setText("Log added to the description. Long reports use Copy full report, then paste on GitHub.");
}

void BugReportWindow::readFields()
{
	for(size_t index = 0; index < inputs.size(); ++index)
		draft.fields[index] = inputs[index]->getText();
}

void BugReportWindow::copyReport()
{
	readFields();
	const auto error = bugReport::validationError(draft, true);
	if(!error.empty())
	{
		status->setText(error);
		return;
	}
	const auto body = bugReport::issueBody(draft);
	if(!ENGINE->input().copyToClipBoard(body))
	{
		copiedBody.clear();
		status->setText("Could not copy the report. Your text remains here; try again.");
		return;
	}
	copiedBody = body;
	status->setText("Full report copied. Continue, then paste it into GitHub's issue description.");
}

void BugReportWindow::openGitHub()
{
	if(includeLog->isSelected() && draft.reviewedLog.empty())
	{
		status->setText("Review and add the optional log first, or uncheck log inclusion.");
		return;
	}
	readFields();
	const auto error = bugReport::validationError(draft, true);
	if(!error.empty())
	{
		status->setText(error);
		return;
	}
	const auto url = bugReport::issueHandoffURL(draft, copiedBody);
	if(url.empty())
	{
		status->setText("This report is too long for a browser link. Copy full report, then Continue and paste it on GitHub.");
		return;
	}
	if(!ENGINE->openExternalURL(url))
		status->setText("The browser could not open. Your report remains here; try again.");
	else
		status->setText(url == bugReport::issueURL(draft) ? "Review and submit in GitHub. This form has not posted an issue."
			: "Paste the copied full report into GitHub's issue description, then review and submit.");
}

void BugReportWindow::dismiss()
{
	const auto callback = afterClose;
	close();
	if(callback)
		callback();
}

void BugReportWindow::showAll(Canvas & to)
{
	CWindowObject::showAll(to);
	CMessage::drawBorder(PlayerColor(1), to, pos.w, pos.h, pos.x, pos.y);
}

void BugReportWindow::openSession(const std::string & directory, std::function<void()> onClose)
{
	bugReport::Draft initial;
	initial.version = "Unknown";
	try
	{
		const JsonNode version(JsonPath::builtin("config/newHorizonsVersion.json"));
		if(version["version"].isString() && version["version"].String().size() <= 128
			&& !version["version"].String().empty())
			initial.version = version["version"].String();
	}
	catch(const std::exception &)
	{
		// A broken product version must not prevent a report about startup failure.
	}
	initial.reporterVersion = initial.version;
	initial.kind = directory.empty() ? "manual" : "unclean-exit";
	const auto source = directory.empty() ? VCMIDirs::get().userLogsPath() / "VCMI_Client_log.txt"
		: boost::filesystem::path(directory) / "client.log";
	if(!directory.empty())
	{
		initial.fields[0] = "Unexpected session exit";
		initial.fields[1] = "The previous session ended unexpectedly. Crash cause is unconfirmed.";
		initial.version = "Unknown (previous product version not recorded)";
		try
		{
			boost::filesystem::ifstream file(boost::filesystem::path(directory) / "report.json", std::ios::binary);
			std::array<char, 4097> buffer{};
			file.read(buffer.data(), buffer.size());
			const auto count = file.gcount();
			if(count > 0 && count <= 4096)
			{
				const JsonNode metadata(buffer.data(), count, "bug report metadata");
				const auto read = [&metadata](const char * key)
				{
					const auto & value = metadata[key];
					return value.isString() && value.String().size() <= 128 ? value.String() : std::string();
				};
				if(const auto captured = read("productVersion"); !captured.empty())
					initial.version = captured;
				initial.buildInfo = "Engine: " + read("engineVersion") + "\nSource: " + read("sourceId") + "\nOS: " + read("os");
			}
		}
		catch(const std::exception &)
		{
			// Reporting remains available even when capture metadata is incomplete.
		}
	}
	auto callback = [directory, onClose = std::move(onClose)]
	{
		if(!directory.empty())
		{
			try
			{
				bugReport::dismissSession(directory);
			}
			catch(const std::exception &)
			{
				logGlobal->warn("Bug report dismissal could not be recorded; the pending report is preserved.");
			}
		}
		if(onClose)
			onClose();
	};
	ENGINE->windows().createAndPushWindow<BugReportWindow>(std::move(initial), source, std::move(callback));
}
