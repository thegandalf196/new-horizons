/*
 * BugReportData.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include <boost/filesystem/path.hpp>
#include <string>

namespace bugReport
{
/// Best-effort conservative filtering; the user must review before sharing.
std::string sanitizeLog(const std::string & text);
/// Read only on explicit attachment consent. Bounded to the final 60 KiB.
std::string readSanitizedLog(const boost::filesystem::path & file);
/// Atomically create a NEW destination directory; write reviewed-report.txt
/// inside it. Parent must exist. Never overwrite an existing directory/file.
bool dismissSession(const boost::filesystem::path & directory);
bool saveReviewedLog(const boost::filesystem::path & file, const std::string & text);
}
