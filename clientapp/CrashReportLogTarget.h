/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "../lib/logging/CLogger.h"
#include <boost/filesystem/path.hpp>
#include <boost/filesystem/fstream.hpp>

/// Two bounded recent segments. Never reads a segment while recording.
class CrashReportLogTarget final : public ILogTarget
{
public:
	explicit CrashReportLogTarget(const boost::filesystem::path & directory);
	void write(const LogRecord & record) override;
private:
	static constexpr std::size_t segmentBytes = 1024 * 1024;
	boost::filesystem::path directory;
	boost::filesystem::ofstream output;
	std::size_t bytes = 0;
	CLogFormatter formatter;
	std::mutex mutex;
	void rotate();
};
