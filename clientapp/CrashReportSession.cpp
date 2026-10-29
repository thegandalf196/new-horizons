/*
 * CrashReportSession.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "CrashReportSession.h"
#include "../Version.h"
#include "../lib/logging/CLogger.h"
#include "CrashReportLogTarget.h"
#include "../lib/json/JsonNode.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <cstdlib>
#include <regex>
#include <boost/interprocess/sync/scoped_lock.hpp>

namespace
{
void writeMarker(const boost::filesystem::path & path, const std::string & text)
{
	boost::filesystem::ofstream stream(path, std::ios::binary | std::ios::trunc);
	stream << text;
	stream.close();
	if(!stream)
		throw std::runtime_error("Could not preserve report session metadata");
}

int64_t sessionCreatedAt(const boost::filesystem::path & directory)
{
	int64_t created = boost::filesystem::last_write_time(directory) * int64_t(1000000000);
	boost::filesystem::ifstream stamp(directory / "created");
	if(stamp)
		stamp >> created;
	return created;
}

bool newestFirst(const std::pair<int64_t, boost::filesystem::path> & left,
	const std::pair<int64_t, boost::filesystem::path> & right)
{
	return left.first != right.first ? left.first > right.first : left.second < right.second;
}

bool reviewAvailable(const boost::filesystem::path & directory)
{
	const auto review = directory / "review.lock";
	if(!boost::filesystem::exists(review))
		return true;
	if(boost::filesystem::is_symlink(review) || !boost::filesystem::is_regular_file(review))
		return false;
	boost::interprocess::file_lock lock(review.string().c_str());
	if(!lock.try_lock())
		return false;
	lock.unlock();
	return true;
}

}

namespace
{
void pruneRetainedSessions(const boost::filesystem::path & root)
{
	constexpr std::size_t maxInactive = 4; // New run can become the fifth unclean run.
	constexpr std::size_t maxActive = 1;
	std::size_t active = 0;
	std::vector<std::pair<int64_t, boost::filesystem::path>> inactive;
	for(const auto & entry : boost::filesystem::directory_iterator(root))
	{
		if(!std::regex_match(entry.path().filename().string(), std::regex("run-[0-9a-f]{16,32}"))
			|| boost::filesystem::is_symlink(entry.path()) || !boost::filesystem::is_directory(entry.path()))
			continue;
		const auto lockPath = entry.path() / "active.lock";
		if(boost::filesystem::is_symlink(lockPath) || !boost::filesystem::is_regular_file(lockPath))
			throw std::runtime_error("Report store contains incomplete session metadata");
		boost::interprocess::file_lock sessionLock(lockPath.string().c_str());
		if(sessionLock.try_lock())
		{
			sessionLock.unlock();
			if(!reviewAvailable(entry.path()))
			{
				++active;
				continue;
			}
			const auto created = sessionCreatedAt(entry.path());
			inactive.emplace_back(created, entry.path());
		}
		else
			++active;
	}
	if(active >= maxActive)
		throw std::runtime_error("Report capture already has an active session");
	std::sort(inactive.begin(), inactive.end(), newestFirst);
	for(std::size_t i = maxInactive; i < inactive.size(); ++i)
	{
		{
			boost::interprocess::file_lock sessionLock((inactive[i].second / "active.lock").string().c_str());
			if(!sessionLock.try_lock())
				throw std::runtime_error("A retained report became active during cleanup");
			sessionLock.unlock();
		} // Close the Windows lock handle before removing its directory.
		boost::filesystem::remove_all(inactive[i].second);
	}
}
}

CrashReportSession::CrashReportSession(const boost::filesystem::path & logsRoot)
{
	const auto root = boost::filesystem::absolute(logsRoot / "crash-reports").lexically_normal();
	const char * assigned = std::getenv("NH_CRASH_SESSION_DIR");
	boost::filesystem::path assignedDirectory;
	if(assigned)
	{
		const auto requested = boost::filesystem::absolute(assigned).lexically_normal();
		if(requested.parent_path() != root || !std::regex_match(requested.filename().string(), std::regex("run-[0-9a-f]{16,32}")))
			throw std::runtime_error("Report session must belong to the configured logs root");
		assignedDirectory = requested;
	}
	if(boost::filesystem::is_symlink(root))
		throw std::runtime_error("Report store must be an ordinary directory");
	boost::filesystem::create_directories(root);
	boost::filesystem::permissions(root, boost::filesystem::owner_all);
	const auto storeLock = root / ".store.lock";
	if(boost::filesystem::is_symlink(storeLock))
		throw std::runtime_error("Report store lock must be an ordinary file");
	{
		boost::filesystem::ofstream createLock(storeLock, std::ios::app);
	}
	boost::interprocess::file_lock lock(storeLock.string().c_str());
	boost::interprocess::scoped_lock guard(lock);
	pruneRetainedSessions(root);

	for(int attempt = 0; attempt < 8; ++attempt)
	{
		sessionDirectory = assigned ? assignedDirectory : root / boost::filesystem::unique_path("run-%%%%%%%%%%%%%%%%");
		if(boost::filesystem::create_directory(sessionDirectory))
			break;
		sessionDirectory.clear();
		if(assigned)
			break;
	}
	if(sessionDirectory.empty())
		throw std::runtime_error("Could not allocate report session");
	boost::filesystem::permissions(sessionDirectory, boost::filesystem::owner_all);
	writeMarker(sessionDirectory / "active.lock", "");
	activeLock = std::make_unique<boost::interprocess::file_lock>((sessionDirectory / "active.lock").string().c_str());
	activeLock->lock();
	writeMarker(sessionDirectory / "pending", "1\n");
	writeMarker(sessionDirectory / "review.lock", "");
	const auto created = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
	writeMarker(sessionDirectory / "created", std::to_string(created));
	recordProductIdentity(std::string());
}

void CrashReportSession::recordProductIdentity(const std::string & productVersion)
{
	JsonNode metadata;
	metadata["schemaVersion"].Integer() = 1;
	metadata["reportKind"].String() = "unclean-exit";
	metadata["logFilename"].String() = "client.log";
	metadata["engineVersion"].String() = GameConstants::VCMI_VERSION;
	metadata["sourceId"].String() = GameConstants::GIT_SHA1;
	// Empty means capture was not yet possible; reporter must label it unknown.
	metadata["productVersion"].String() = productVersion.substr(0, 128);
	metadata["captureLimitBytes"].Integer() = 2 * 1024 * 1024;
	metadata["retainedSessionLimit"].Integer() = 5;
#ifdef VCMI_WINDOWS
	metadata["os"].String() = "Windows";
#elif defined(__linux__)
	metadata["os"].String() = "Linux";
#elif defined(VCMI_MAC)
	metadata["os"].String() = "macOS";
#else
	metadata["os"].String() = "Other";
#endif
	writeMarker(sessionDirectory / "report.json", metadata.toString());
}

void CrashReportSession::attachLogTarget() const
{
	CLogger::getGlobalLogger()->addTarget(std::make_unique<CrashReportLogTarget>(sessionDirectory));
}

void CrashReportSession::finishCleanly()
{
	// Called explicitly after ordinary teardown, never from a destructor while
	// unwinding an exception. Close logger targets before removing the clean run.
	boost::filesystem::remove(sessionDirectory / "pending");
	activeLock->unlock();
	activeLock.reset();
	boost::filesystem::remove_all(sessionDirectory);
}

const boost::filesystem::path & CrashReportSession::directory() const
{
	return sessionDirectory;
}

std::vector<boost::filesystem::path> CrashReportSession::inactiveSessions(const boost::filesystem::path & logsRoot)
{
	std::vector<boost::filesystem::path> result;
	const auto root = logsRoot / "crash-reports";
	if(!boost::filesystem::is_directory(root))
		return result;
	for(const auto & entry : boost::filesystem::directory_iterator(root))
	{
		if(boost::filesystem::is_symlink(entry.path()) || !boost::filesystem::is_directory(entry.path()))
			continue;
		const auto pending = entry.path() / "pending";
		const auto lockPath = entry.path() / "active.lock";
		if(!boost::filesystem::is_regular_file(pending) || boost::filesystem::is_symlink(lockPath)
			|| !boost::filesystem::is_regular_file(lockPath))
			continue;
		boost::interprocess::file_lock lock(lockPath.string().c_str());
		if(lock.try_lock())
		{
			lock.unlock();
			if(reviewAvailable(entry.path()))
				result.push_back(entry.path());
		}
	}
	std::sort(result.begin(), result.end(), [](const auto & left, const auto & right)
	{
		return newestFirst({sessionCreatedAt(left), left}, {sessionCreatedAt(right), right});
	});
	return result;
}
