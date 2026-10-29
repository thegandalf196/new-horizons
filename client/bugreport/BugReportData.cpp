/*
 * BugReportData.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "../StdInc.h"
#include "BugReportData.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/interprocess/sync/file_lock.hpp>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>

namespace bugReport
{
std::string sanitizeLog(const std::string & text)
{
	// Drop whole potentially sensitive records, including full settings dumps.
	const std::regex sensitive("token|password|passwd|secret|authorization|cookie|bearer|gh[pousr]_|github_pat_|private.key|api[_ -]?key|settings[ ]*=|environment|username|user_name|email|player|lobby|uuid|hostname|host.name|client[ ]+[0-9]+|[0-9a-f]{8}-[0-9a-f]{4}-|[0-9a-f]{32,}", std::regex::icase);
	const std::regex pathOrNetwork(R"((?:[A-Za-z]:[\\/]|/)[^\s\"<>]*|\\\\[^\s\"<>]+|[A-Za-z][A-Za-z0-9+.-]*://[^\s]+|[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+|\b(?:[0-9]{1,3}\.){3}[0-9]{1,3}\b|\b[0-9A-Fa-f]{0,4}:[0-9A-Fa-f:]+\b)");
	const char * login = std::getenv("USER");
	if(!login)
		login = std::getenv("USERNAME");
	const std::regex timestamp(R"(^\[[0-9]{4}-[0-9]{2}-[0-9]{2} [0-9]{2}:[0-9]{2}:[0-9]{2}\.[0-9]{3}\])");
	const std::regex hostname(R"(\b[A-Za-z][A-Za-z0-9-]*(?:\.[A-Za-z0-9-]+)+\b)");
	std::istringstream input(text);
	std::ostringstream output;
	std::string line;
	while(std::getline(input, line))
	{
		if(output.tellp() >= 60 * 1024)
		{
			output << "[sanitized preview truncated]\n";
			break;
		}
		const auto content = std::regex_replace(line, timestamp, std::string());
		if(line.size() > 4096 || (!line.empty() && line.front() != '[')
			|| std::regex_search(content, sensitive) || std::regex_search(content, pathOrNetwork)
			|| std::regex_search(content, hostname)
			|| (login && *login && line.find(login) != std::string::npos))
			output << "[potentially sensitive record removed]\n";
		else
			output << line << '\n';
	}
	return output.str();
}

static std::string readTail(const boost::filesystem::path & file)
{
	constexpr std::size_t maxBytes = 60 * 1024;
	if(boost::filesystem::is_symlink(file) || !boost::filesystem::is_regular_file(file))
		throw std::runtime_error("Report log is unavailable");
	boost::filesystem::ifstream input(file, std::ios::binary);
	input.seekg(0, std::ios::end);
	const std::streamoff size = input.tellg();
	if(size < 0)
		throw std::runtime_error("Report log could not be read");
	const auto offset = size > static_cast<std::streamoff>(maxBytes) ? size - static_cast<std::streamoff>(maxBytes) : std::streamoff(0);
	input.seekg(offset);
	std::string text(maxBytes, '\0');
	input.read(text.data(), text.size());
	text.resize(input.gcount());
	if(offset != 0)
	{
		const auto newline = text.find('\n');
		text = newline == std::string::npos ? std::string() : text.substr(newline + 1);
		text.insert(0, "[log truncated to final 60 KiB]\n");
	}
	return text;
}

std::string readSanitizedLog(const boost::filesystem::path & file)
{
	constexpr std::size_t maxBytes = 60 * 1024;
	auto text = readTail(file);
	const auto previous = file.parent_path() / "client.previous.log";
	if(file.filename() == "client.log" && text.size() < maxBytes && boost::filesystem::is_regular_file(previous))
		text = readTail(previous) + text;
	if(text.size() > maxBytes)
		text.erase(0, text.size() - maxBytes);
	return sanitizeLog(text);
}

bool dismissSession(const boost::filesystem::path & directory)
{
	const auto pending = directory / "pending";
	const auto lockPath = directory / "active.lock";
	if(boost::filesystem::is_symlink(directory) || boost::filesystem::is_symlink(lockPath)
		|| !boost::filesystem::is_regular_file(pending) || !boost::filesystem::is_regular_file(lockPath))
		return false;
	boost::interprocess::file_lock lock(lockPath.string().c_str());
	if(!lock.try_lock())
		return false;
	try
	{
		boost::filesystem::rename(pending, directory / "dismissed");
		lock.unlock();
		return true;
	}
	catch(...)
	{
		lock.unlock();
		throw;
	}
}

bool saveReviewedLog(const boost::filesystem::path & file, const std::string & text)
{
	// Caller owns destination selection; C++ fstreams lack portable exclusive
	// creation. Reserve a NEW directory atomically, then write a fixed filename.
	if(!boost::filesystem::create_directory(file))
		return false;
	boost::filesystem::permissions(file, boost::filesystem::owner_all);
	boost::filesystem::ofstream output(file / "reviewed-report.txt", std::ios::binary);
	output << text;
	output.close();
	return static_cast<bool>(output);
}
}
