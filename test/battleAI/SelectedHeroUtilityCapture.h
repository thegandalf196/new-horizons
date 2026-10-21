/*
 * SelectedHeroUtilityCapture.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "HeroActionTimingCapture.h"
#include "../../lib/battle/BattleAction.h"

namespace selectedHeroTest
{
class Target final : public ILogTarget
{
	std::weak_ptr<heroTimingTest::Messages> messages;
public:
	explicit Target(std::weak_ptr<heroTimingTest::Messages> value) : messages(std::move(value)) {}
	void write(const LogRecord & record) override
	{
		if(record.message.find("Best hero action is ") == std::string::npos
			|| record.message.find("(value ") == std::string::npos)
			return;
		if(const auto state = messages.lock())
		{
			std::lock_guard lock(state->mutex);
			if(state->rows.size() < 8 && record.message.size() < 4096)
				state->rows.push_back(record.message);
			else
				state->truncated = true;
		}
	}
};

class Capture final
{
	// Reuse the maintained scope/restoration and weak-target lifecycle; only
	// the AI domain is raised, and only for the existing test callback attempt.
	heroTimingTest::Capture levels{CLogger::getGlobalLogger()->getLevel(), ELogLevel::TRACE};
	std::shared_ptr<heroTimingTest::Messages> messages = std::make_shared<heroTimingTest::Messages>();
public:
	Capture() { CLogger::getGlobalLogger()->addTarget(std::make_unique<Target>(messages)); }
	std::string describe(const std::vector<BattleAction> & actions) const
	{
		std::ostringstream out;
		for(const auto & action : actions)
		{
			out << " type=" << static_cast<int>(action.actionType) << " spell=" << action.spell.getNum()
				<< " overcharge=" << action.spellOvercharge << " command=" << heroCommands::key(action.command)
				<< " side=" << static_cast<int>(action.side) << " stack=" << action.stackNumber;
			for(const auto & destination : action.target)
				out << " target=(" << destination.unitValue << ',' << destination.hexValue.toInt() << ')';
		}
		std::lock_guard lock(messages->mutex);
		out << " selected-utility-lines=" << ::testing::PrintToString(messages->rows)
			<< " truncated=" << messages->truncated;
		return out.str();
	}
	bool hasSelectedUtility() const
	{
		std::lock_guard lock(messages->mutex);
		return messages->rows.size() == 1 && !messages->truncated;
	}
};
}
