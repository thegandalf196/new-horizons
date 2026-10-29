/* CPU-only bug report policy regression. GPL-2.0-or-later; see license.txt. */
#include "../../lib/StdInc.h"
#include "../bugreport/BugReportDraft.h"

#include <iostream>

namespace
{
void require(bool condition)
{
	if(!condition)
		throw std::runtime_error("Bug report policy assertion failed");
}
}

int main()
{
	try
	{
		bugReport::Draft draft{{"Title & #?", "A unit stopped", "Move\nWait", "Moves", "Stops"}, "0.88.0", "manual", "", ""};
		require(bugReport::validationError(draft).empty());
		const auto url = bugReport::issueURL(draft);
		require(url.starts_with("https://github.com/thegandalf196/new-horizons/issues/new?title=Title%20%26%20%23%3F&body="));
		require(url.find("Move%0AWait") != std::string::npos);
		require(url.find(' ') == std::string::npos);
		require(url.find('#') == std::string::npos);
		require(bugReport::issueBody(draft).find("crash") == std::string::npos);
		draft.kind = "unclean-exit";
		require(bugReport::issueBody(draft).find("crash cause is unconfirmed") != std::string::npos);
		draft.fields[0] = " \n\t";
		require(!bugReport::validationError(draft).empty());
		draft.fields[0] = "Good title";
		draft.fields[1] = "  ";
		require(!bugReport::validationError(draft).empty());
		draft.fields[1] = std::string(1201, 'x');
		require(!bugReport::validationError(draft).empty());
		draft.fields[1] = "Mágico";
		require(bugReport::issueURL(draft).find("M%C3%A1gico") != std::string::npos);
		for(size_t index = 1; index < draft.fields.size(); ++index)
			draft.fields[index] = std::string(1200, '#');
		require(!bugReport::validationError(draft).empty());
		draft.fields = {"Log report", "A unit stopped", "Move", "Moves", "Stops"};
		draft.reviewedLog = "text\n```\n</details>\n````\nstill log text";
		const auto fenced = bugReport::issueBody(draft);
		require(fenced.find("`````text\n" + draft.reviewedLog + "\n`````\n</details>") != std::string::npos);
		require(bugReport::issueURL(draft).find("Reviewed%20diagnostic%20log") != std::string::npos);
		draft.reviewedLog.clear();
		require(bugReport::issueBody(draft).find("diagnostic log") == std::string::npos);
		// ASCII padding changes the encoded URL by one byte: test the exact handoff boundary.
		draft.reviewedLog = "x";
		const auto padding = 7000 - bugReport::issueURL(draft).size();
		draft.reviewedLog.append(padding, 'x');
		require(bugReport::issueURL(draft).size() == 7000);
		require(bugReport::issueHandoffURL(draft, "") == bugReport::issueURL(draft));
		draft.reviewedLog += 'x';
		require(bugReport::issueURL(draft).size() == 7001);
		require(bugReport::issueHandoffURL(draft, "").empty());
		require(bugReport::issueHandoffURL(draft, "copy failed").empty());
		const auto copied = bugReport::issueBody(draft);
		require(bugReport::issueHandoffURL(draft, copied) == bugReport::issueURL(draft, false));
		require(bugReport::issueHandoffURL(draft, copied).find("&body=") == std::string::npos);
		draft.fields[1] += " changed after copy";
		require(bugReport::issueHandoffURL(draft, copied).empty());
		require(bugReport::validationError(draft, true).empty());
		draft.reviewedLog.assign(65537, 'x');
		require(!bugReport::validationError(draft, true).empty());
		// Bound the complete wrapped body, not just the log or browser URL.
		draft.reviewedLog = "x";
		draft.reviewedLog.append(65536 - bugReport::issueBody(draft).size(), 'x');
		require(bugReport::issueBody(draft).size() == 65536);
		require(bugReport::validationError(draft, true).empty());
		draft.reviewedLog += 'x';
		require(!bugReport::validationError(draft, true).empty());
		require(bugReport::issueHandoffURL(draft, bugReport::issueBody(draft)).empty());
		std::cout << "Bug report draft checks passed\n";
		return 0;
	}
	catch(const std::exception & error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
