/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../bugreport/BugReportData.h"
#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

#include <stdexcept>

namespace
{
void require(bool condition)
{
	if(!condition)
		throw std::runtime_error("Bug report sanitization assertion failed");
}
}

int main()
{
	const std::string sensitive = "[x] home /home/alice/game\\save C:\\Users\\alice\\game\\save\\n\n"
		"[x] https://host.invalid/private?token=hidden\n[x] password = secret\n"
		"[x] peer 192.168.1.2 mail alice@example.invalid\n"
		"unstructured potentially private continuation\n[x] ghp_private\n"
		"[x] C:\\Users\\Alice Smith\\game\\save\n[x] /home/Zoë Documents/private\n"
		"[x] path \"/opt/private name/data\"\n[x] Player joined name='Alice Smith'\n"
		"[x] Client 1 player: Zoë\n[x] uuid aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee\n"
		"[x] IPv6 fe80::1234\n";
	const auto cleaned = bugReport::sanitizeLog(sensitive);
	for(const auto & forbidden : {"alice", "hidden", "192.168", "secret", "host.invalid", "unstructured", "ghp_private", "Smith", "Zoë", "private name", "bbbb", "fe80"})
		require(cleaned.find(forbidden) == std::string::npos);
	require(bugReport::sanitizeLog("[x] Battle round 5\n").find("Battle round 5") != std::string::npos);
	require(bugReport::sanitizeLog("[x] " + std::string(20000, 'a')).size() < 100);
	require(bugReport::sanitizeLog("[2026-10-28 13:22:01.123] INFO global [MainGUI] - Battle round 5\n").find("Battle round 5") != std::string::npos);
	const auto root = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("nh-report-test-%%%%%%%%%%%%%%%%");
	boost::filesystem::create_directory(root);
	const auto log = root / "client.log";
	{
		boost::filesystem::ofstream out(log, std::ios::binary);
		out << std::string(3 * 1024 * 1024, 'a') << "\n[x] Battle round 6\n";
	}
	const auto tail = bugReport::readSanitizedLog(log);
	require(tail.find("truncated") != std::string::npos);
	require(tail.find("Battle round 6") != std::string::npos);
	const auto destination = root / "reviewed";
	require(bugReport::saveReviewedLog(destination, "reviewed text"));
	require(!bugReport::saveReviewedLog(destination, "overwrite"));
	boost::filesystem::ifstream input(destination / "reviewed-report.txt");
	std::string text;
	std::getline(input, text);
	require(text == "reviewed text");
	boost::filesystem::remove_all(root);
}
