/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <boost/filesystem/path.hpp>
#include <boost/program_options/variables_map.hpp>
#include <memory>
#include <string>
namespace boost::program_options { class options_description; }
class CBasicLogConfigurator;
class CConsoleHandler;
class CrashReportSession;

/// Logging lifetime and reporting-only startup; owns no simulation or game state.
class BugReportBootstrap
{
public:
	static void addOptions(boost::program_options::options_description & options);
	explicit BugReportBootstrap(const boost::program_options::variables_map & options);
	~BugReportBootstrap();
	bool isReporter() const;
	const boost::filesystem::path & logPath() const;
	void configureDefault();
	void configure();
	void deconfigure();
	void captureProductIdentity();
	void finishCleanly();
	int runReporter();
private:
	boost::filesystem::path logFile;
	std::string requestedSession;
	bool reporting = false;
	std::unique_ptr<CrashReportSession> session;
	std::unique_ptr<CConsoleHandler> console;
	std::unique_ptr<CBasicLogConfigurator> logger;
};
