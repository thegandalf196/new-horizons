/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "StdInc.h"
#include "CrashReportLogTarget.h"
#include <boost/filesystem.hpp>

CrashReportLogTarget::CrashReportLogTarget(const boost::filesystem::path & directory)
	: directory(directory)
{
	const auto path = directory / "client.log";
	bytes = boost::filesystem::exists(path) ? boost::filesystem::file_size(path) : 0;
	output.open(path, std::ios::binary | std::ios::app);
	formatter.setPattern("[%c] %l %n [%t] - %m");
}

void CrashReportLogTarget::rotate()
{
	output.close();
	const auto previous = directory / "client.previous.log";
	boost::filesystem::remove(previous);
	boost::filesystem::rename(directory / "client.log", previous);
	output.open(directory / "client.log", std::ios::binary | std::ios::trunc);
	bytes = 0;
}

void CrashReportLogTarget::write(const LogRecord & record)
{
	// Formatting is existing logger work; bound the retained individual record.
	auto text = formatter.format(record);
	if(text.size() > 64 * 1024)
		text = "[oversized crash-log record truncated]\n" + text.substr(text.size() - 64 * 1024);
	text += '\n';
	std::lock_guard lock(mutex);
	try
	{
		if(bytes + text.size() > segmentBytes)
			rotate();
		output << text;
		output.flush();
		bytes += text.size();
	}
	catch(const std::exception &)
	{
		// The ordinary primary target remains authoritative for diagnostics.
		// Do not recursively log, change application exit, or attempt recovery IO.
		output.close();
	}
}
