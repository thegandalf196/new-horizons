/* New Horizons bug report draft. GPL-2.0-or-later; see license.txt. */
#include "../StdInc.h"
#include "BugReportDraft.h"

namespace bugReport
{
namespace
{
constexpr size_t maxIssueURLBytes = 7000;
constexpr size_t maxReviewedLogBytes = 65536;
constexpr size_t maxIssueBodyBytes = 65536;

std::string encode(const std::string & text)
{
	constexpr char HEX[] = "0123456789ABCDEF";
	std::string result;
	for(const unsigned char c : text)
	{
		if((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
			|| (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~')
			result += c;
		else
		{
			result += '%';
			result += HEX[c >> 4];
			result += HEX[c & 15];
		}
	}
	return result;
}

bool blank(const std::string & text)
{
	return text.find_first_not_of(" \t\r\n") == std::string::npos;
}
}

std::string issueBody(const Draft & draft)
{
	const std::array<std::string, 4> headings = {"What happened", "Steps to reproduce", "Expected result", "Actual result"};
	std::string result = "### Version\n" + draft.version + "\n\n";
	if(!draft.buildInfo.empty())
		result += draft.buildInfo + "\n\n";
	if(draft.kind == "unclean-exit")
	{
		result += "Previous session ended unexpectedly; crash cause is unconfirmed.\n\n";
		result += "Reporter build: New Horizons " + draft.reporterVersion + "\n\n";
	}
	for(size_t index = 0; index < headings.size(); ++index)
		result += "### " + headings[index] + "\n" + draft.fields[index + 1] + "\n\n";
	if(!draft.reviewedLog.empty())
	{
		// A delimiter longer than every backtick run keeps log text inside the code block.
		size_t longest = 0;
		size_t run = 0;
		for(const char c : draft.reviewedLog)
		{
			run = c == '`' ? run + 1 : 0;
			longest = std::max(longest, run);
		}
		const std::string fence(std::max(size_t(3), longest + 1), '`');
		result += "<details>\n<summary>Reviewed diagnostic log</summary>\n\n" + fence + "text\n"
			+ draft.reviewedLog + "\n" + fence + "\n</details>\n";
	}
	return result;
}

std::string issueURL(const Draft & draft, bool includeBody)
{
	return "https://github.com/thegandalf196/new-horizons/issues/new?title="
		+ encode(draft.fields[0]) + (includeBody ? "&body=" + encode(issueBody(draft)) : "");
}

std::string validationError(const Draft & draft, bool allowClipboard)
{
	if(blank(draft.fields[0]) || blank(draft.fields[1]))
		return "Enter a title and describe what happened.";
	if(draft.fields[0].size() > 120)
		return "Use a shorter title (120 bytes maximum).";
	for(size_t index = 1; index < draft.fields.size(); ++index)
		if(draft.fields[index].size() > 1200)
			return "Shorten the report fields (1200 bytes each maximum).";
	if(draft.reviewedLog.size() > maxReviewedLogBytes)
		return "Shorten the reviewed log (64 KiB maximum).";
	if(issueBody(draft).size() > maxIssueBodyBytes)
		return "Shorten the report or reviewed log (64 KiB total maximum).";
	if(!allowClipboard && issueURL(draft).size() > maxIssueURLBytes)
		return "Shorten the report before opening it in your browser.";
	return {};
}

std::string issueHandoffURL(const Draft & draft, const std::string & copiedBody)
{
	if(!validationError(draft, true).empty())
		return {};
	const auto direct = issueURL(draft);
	if(direct.size() <= maxIssueURLBytes)
		return direct;
	return copiedBody == issueBody(draft) ? issueURL(draft, false) : std::string();
}
}
