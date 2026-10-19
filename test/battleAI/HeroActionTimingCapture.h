/*
 * HeroActionTimingCapture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "../../lib/logging/CLogger.h"
#include <cstdlib>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace heroTimingTest
{
inline bool optedIn()
{
	const char * value = std::getenv("NH_RENDER_LOAD_TIMINGS");
	return value && std::string_view(value) == "1";
}

struct Messages
{
	std::mutex mutex;
	std::vector<std::string> rows;
	bool truncated = false;
};

class Target final : public ILogTarget
{
	std::weak_ptr<Messages> messages;
public:
	explicit Target(std::weak_ptr<Messages> messages) : messages(std::move(messages)) {}
	void write(const LogRecord & record) override
	{
		if(!record.message.starts_with("NH_HERO_ACTION_TIMING "))
			return;
		if(const auto target = messages.lock())
		{
			std::lock_guard lock(target->mutex);
			if(target->rows.size() < 32)
				target->rows.push_back(record.message);
			else
				target->truncated = true;
		}
	}
};

inline std::map<std::string, int64_t> fields(const std::string & row)
{
	std::map<std::string, int64_t> result;
	std::istringstream stream(row);
	std::string token;
	stream >> token;
	while(stream >> token)
	{
		const auto split = token.find('=');
		if(split != std::string::npos)
			result.emplace(token.substr(0, split), std::stoll(token.substr(split + 1)));
	}
	return result;
}

/// Same bounded weak-target lifecycle as the maintained WAIT diagnostic fixture.
class Capture final
{
	CLogger * global = CLogger::getGlobalLogger();
	CLogger * ai = CLogger::getLogger(CLoggerDomain("ai"));
	ELogLevel::ELogLevel oldGlobal = global->getLevel();
	ELogLevel::ELogLevel oldAi = ai->getLevel();
	std::shared_ptr<Messages> messages = std::make_shared<Messages>();
public:
	explicit Capture(ELogLevel::ELogLevel globalLevel = ELogLevel::DEBUG,
		ELogLevel::ELogLevel aiLevel = ELogLevel::DEBUG)
	{
		global->setLevel(globalLevel);
		ai->setLevel(aiLevel);
		global->addTarget(std::make_unique<Target>(messages));
	}
	~Capture()
	{
		ai->setLevel(oldAi);
		global->setLevel(oldGlobal);
		// No target removal API; weak expired buffers leave unrelated targets intact.
		messages.reset();
	}
	Capture(const Capture &) = delete;
	Capture & operator=(const Capture &) = delete;
	std::vector<std::string> rows() const
	{
		std::lock_guard lock(messages->mutex);
		EXPECT_FALSE(messages->truncated);
		return messages->rows;
	}
	std::vector<std::map<std::string, int64_t>> records() const
	{
		std::vector<std::map<std::string, int64_t>> result;
		for(const auto & row : rows())
			if(!row.starts_with("NH_HERO_ACTION_TIMING cap="))
				result.push_back(fields(row));
		return result;
	}
	void expectSubmission(int round, uint32_t stack, bool fullContinuation = false) const
	{
		const auto captured = records();
		if(!optedIn())
		{
			EXPECT_TRUE(captured.empty());
			return;
		}
		ASSERT_EQ(captured.size(), 1u);
		const auto & row = captured.front();
		EXPECT_EQ(row.at("battle"), 0);
		EXPECT_EQ(row.at("player"), 0);
		EXPECT_EQ(row.at("side"), 0);
		EXPECT_EQ(row.at("stack"), stack);
		EXPECT_EQ(row.at("round"), round);
		EXPECT_EQ(row.at("loop_joined"), 1);
		EXPECT_EQ(row.at("loop_threw"), 0);
		EXPECT_EQ(row.at("loop_calls"), 1);
		EXPECT_GT(row.at("candidates"), 0);
		EXPECT_EQ(row.at("submit_calls"), 1);
		EXPECT_EQ(row.at("submit_returned"), 1);
		EXPECT_EQ(row.at("submit_threw"), 0);
		if(fullContinuation)
		{
			EXPECT_GT(row.at("continuation_full_calls"), 0);
		}
	}
};
}
