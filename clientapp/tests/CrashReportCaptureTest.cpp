/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../StdInc.h"
#include "../CrashReportSession.h"
#include "../CrashReportLogTarget.h"
#include <boost/filesystem.hpp>
#if BOOST_VERSION >= 108600
#include <boost/process/v1/child.hpp>
namespace reportProcess = boost::process::v1;
#else
#include <boost/process/child.hpp>
namespace reportProcess = boost::process;
#endif
#include <cstdlib>

namespace
{
void require(bool condition, const char * message)
{
	if(!condition)
		throw std::runtime_error(message);
}

void generate(const boost::filesystem::path & root)
{
	CrashReportSession session(root);
	CrashReportLogTarget target(session.directory());
	const CLoggerDomain domain("report-test");
	for(int i = 0; i < 10000; ++i)
		target.write(LogRecord(domain, ELogLevel::INFO, std::to_string(i) + std::string(300, 'x')));
	target.write(LogRecord(domain, ELogLevel::ERROR, "LAST_CRASH_RECORD"));
	std::_Exit(17); // Test unclean process exit without changing real application handlers.
}
}

int main(int argc, char ** argv)
{
	if(argc == 3 && std::string(argv[1]) == "--generate")
		generate(boost::filesystem::path(argv[2]));
	if(argc == 3 && std::string(argv[1]) == "--hold")
	{
		const boost::filesystem::path logs(argv[2]);
		CrashReportSession session(logs);
		boost::filesystem::ofstream ready(logs / "ready");
		ready << session.directory().string();
		ready.close();
		std::this_thread::sleep_for(std::chrono::seconds(10));
		std::_Exit(18);
	}
	const auto root = boost::filesystem::temp_directory_path() / boost::filesystem::unique_path("nh-crash-test-%%%%%%%%%%%%%%%%");
	boost::filesystem::create_directory(root);
	try
	{
		{
			reportProcess::child holder(boost::filesystem::absolute(argv[0]), std::vector<std::string>{"--hold", root.string()});
			for(int poll = 0; poll < 500 && !boost::filesystem::exists(root / "ready"); ++poll)
				std::this_thread::sleep_for(std::chrono::milliseconds(10));
			require(boost::filesystem::exists(root / "ready"), "active child must become ready");
			require(CrashReportSession::inactiveSessions(root).empty(), "active process must not be offered as crashed");
			bool rejected = false;
			try { CrashReportSession forbidden(root); }
			catch(const std::exception &) { rejected = true; }
			require(rejected, "second capture must not exceed active store bound");
			holder.terminate();
			holder.wait();
		}
		std::vector<boost::filesystem::path> generated;
		for(int i = 0; i < 7; ++i)
		{
			reportProcess::child child(boost::filesystem::absolute(argv[0]), std::vector<std::string>{"--generate", root.string()});
			child.wait();
			require(child.exit_code() == 17, "unclean fixture must retain its actual exit status");
			for(const auto & path : CrashReportSession::inactiveSessions(root))
				if(std::find(generated.begin(), generated.end(), path) == generated.end())
					generated.push_back(path);
		}
		const auto inactive = CrashReportSession::inactiveSessions(root);
		require(inactive.size() == 5, "retained session count must be bounded to five");
		require(inactive.front() == generated.back(), "fallback must select the newest crash, not random token order");
		for(const auto & path : inactive)
		{
			boost::filesystem::ofstream stamp(path / "created");
			stamp << 42; // Equal creation times must have deterministic path ordering.
		}
		const auto tied = CrashReportSession::inactiveSessions(root);
		require(std::is_sorted(tied.begin(), tied.end()), "equal timestamps require a stable path tie-break");
		for(auto it = generated.end() - 5; it != generated.end(); ++it)
			require(std::find(inactive.begin(), inactive.end(), *it) != inactive.end(), "most recent five crashes must survive retention");
		std::size_t bytes = 0;
		for(const auto & directory : inactive)
		{
			for(const auto & name : {"client.log", "client.previous.log"})
			{
				const auto file = directory / name;
				require(boost::filesystem::file_size(file) <= 1024 * 1024, "segment must not exceed cap");
				bytes += boost::filesystem::file_size(file);
			}
			boost::filesystem::ifstream input(directory / "client.log");
			const std::string text(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>{});
			require(text.find("LAST_CRASH_RECORD") != std::string::npos, "recent fatal record must survive rotation");
		}
		require(bytes <= 10 * 1024 * 1024, "raw retained total must remain bounded");
		{
			CrashReportSession session(root);
			const auto directory = session.directory();
			session.finishCleanly();
			require(!boost::filesystem::exists(directory), "clean session must be removed");
		}
		boost::filesystem::remove_all(root);
		return 0;
	}
	catch(...)
	{
		boost::filesystem::remove_all(root);
		throw;
	}
}
