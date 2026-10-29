/*
 * CrashReportSession.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <boost/filesystem/path.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <memory>
#include <string>
#include <vector>

/// No signal handlers: unfinished sessions are evidence of an unclean exit only.
class CrashReportSession
{
public:
	explicit CrashReportSession(const boost::filesystem::path & logsRoot);
	void attachLogTarget() const;
	void recordProductIdentity(const std::string & productVersion);
	void finishCleanly();
	const boost::filesystem::path & directory() const;
	static std::vector<boost::filesystem::path> inactiveSessions(const boost::filesystem::path & logsRoot);
private:
	boost::filesystem::path sessionDirectory;
	std::unique_ptr<boost::interprocess::file_lock> activeLock;
};
