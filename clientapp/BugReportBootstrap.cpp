/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#include "BugReportBootstrap.h"
#include "CrashReportSession.h"
#include "../client/ClientCommandManager.h"
#include "../lib/CConsoleHandler.h"
#include "../lib/VCMIDirs.h"
#include "../lib/logging/CBasicLogConfigurator.h"
#include "../lib/CConfigHandler.h"
#include "../lib/json/JsonNode.h"
#include <boost/program_options.hpp>

void BugReportBootstrap::addOptions(boost::program_options::options_description & options)
{
	options.add_options()
		("logLocation", boost::program_options::value<std::string>(), "new location for log files")
		("report-bug", boost::program_options::value<std::string>(), "open a separate report form for a retained session");
}

BugReportBootstrap::BugReportBootstrap(const boost::program_options::variables_map & options)
	: logFile(VCMIDirs::get().userLogsPath() / "VCMI_Client_log.txt")
	, reporting(options.count("report-bug") != 0)
{
	if(options.count("logLocation"))
		logFile = boost::filesystem::path(options["logLocation"].as<std::string>()) / "VCMI_Client_log.txt";
	if(reporting)
	{
		requestedSession = options["report-bug"].as<std::string>();
		logFile = logFile.parent_path() / "VCMI_Report_log.txt";
	}
	else
	{
		try
		{
			session = std::make_unique<CrashReportSession>(logFile.parent_path());
		}
		catch(const std::exception &)
		{
			std::cerr << "Crash log preservation is unavailable for this session.\n";
		}
	}
#ifndef VCMI_IOS
	if(!reporting)
	{
		console = std::make_unique<CConsoleHandler>([](const std::string & text, bool inGame)
		{
			ClientCommandManager manager;
			manager.processCommand(text, inGame);
		});
		console->start();
	}
#endif
	logger = std::make_unique<CBasicLogConfigurator>(logFile, console.get());
}

BugReportBootstrap::~BugReportBootstrap()
{
	logger->deconfigure();
}

bool BugReportBootstrap::isReporter() const { return reporting; }
const boost::filesystem::path & BugReportBootstrap::logPath() const { return logFile; }

void BugReportBootstrap::configureDefault()
{
	logger->configureDefault();
	if(session)
		session->attachLogTarget();
}

void BugReportBootstrap::configure()
{
	logger->configure();
	if(session)
		session->attachLogTarget();
}

void BugReportBootstrap::deconfigure()
{
	logger->deconfigure();
}

void BugReportBootstrap::captureProductIdentity()
{
	if(!session)
		return;
	try
	{
		const JsonNode identity(JsonPath::builtin("config/newHorizonsVersion.json"));
		session->recordProductIdentity(identity["version"].String());
	}
	catch(const std::exception &)
	{
		logGlobal->warn("Report product identity could not be captured; version remains unknown");
	}
}

void BugReportBootstrap::finishCleanly()
{
	deconfigure();
	if(!session)
		return;
	try
	{
		session->finishCleanly();
	}
	catch(const std::exception &)
	{
		std::cerr << "Clean report-session cleanup was incomplete.\n";
	}
}
