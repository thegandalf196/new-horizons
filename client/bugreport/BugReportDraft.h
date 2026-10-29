/* New Horizons bug report draft. GPL-2.0-or-later; see license.txt. */
#pragma once

#include <array>
#include <string>

namespace bugReport
{
struct Draft
{
	std::array<std::string, 5> fields;
	std::string version;
	std::string kind;
	std::string reporterVersion;
	std::string buildInfo;
	// Only explicitly reviewed, consented text belongs here; never a log path.
	std::string reviewedLog = {};
};

// Contains reviewed report text, including the optional user-approved log.
std::string issueBody(const Draft & draft);
std::string issueURL(const Draft & draft, bool includeBody = true);
std::string validationError(const Draft & draft, bool allowClipboard = false);
// Large reports require an exact, successfully copied body before title-only handoff.
std::string issueHandoffURL(const Draft & draft, const std::string & copiedBody);
}
