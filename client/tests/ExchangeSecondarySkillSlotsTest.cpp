/*
 * ExchangeSecondarySkillSlotsTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 * Pure production refresh-model regression; no renderer acceptance implied.
 */
#include "../windows/ExchangeSecondarySkillSlots.h"

#include <array>
#include <cstdlib>
#include <iostream>
#include <utility>
#include <vector>

namespace
{
void require(bool condition, const char * message)
{
	if(!condition)
	{
		std::cerr << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}

struct ExchangeModel
{
	using Skill = std::pair<int, int>;
	std::array<Skill, EXCHANGE_SECONDARY_SKILL_SLOTS> widgets;
	bool overflow = false;

	explicit ExchangeModel(const std::vector<Skill> & skills)
	{
		refresh(skills);
	}

	void refresh(const std::vector<Skill> & skills)
	{
		std::size_t refreshed = 0;
		overflow = refreshExchangeSecondarySkillSlots(skills,
			[this, &refreshed](std::size_t index, const Skill * skill)
		{
			widgets.at(index) = skill ? *skill : Skill{-1, 0};
			++refreshed;
		});
		require(refreshed == widgets.size(), "Every fixed widget must be refreshed");
	}
};
}

int main()
{
	std::vector<ExchangeModel::Skill> skills{{11, 1}, {22, 2}};
	ExchangeModel exchange(skills);
	require(exchange.widgets[2] == ExchangeModel::Skill{-1, 0}, "Unoccupied opening slot is empty");

	// The exchange stays open under a level-up; the next garrison refresh must
	// populate the newly acquired third skill without resizing the widget array.
	skills.emplace_back(33, 1);
	exchange.refresh(skills);
	require(exchange.widgets[2] == skills[2], "New skill after opening must appear safely");
	skills[0].second = 3;
	exchange.refresh(skills);
	require(exchange.widgets[0].second == 3, "Rank changes must refresh existing widgets");

	for(int id = 4; skills.size() < EXCHANGE_SECONDARY_SKILL_SLOTS; ++id)
		skills.emplace_back(id, 1);
	exchange.refresh(skills);
	require(!exchange.overflow && exchange.widgets.back() == skills.back(), "Exactly eight skills show the eighth");
	skills.emplace_back(99, 2);
	exchange.refresh(skills);
	require(exchange.overflow && exchange.widgets.back() == ExchangeModel::Skill{-1, 0}, "Overflow hides the eighth skill");
	exchange.refresh(skills);
	require(exchange.overflow, "Repeated overflow refresh preserves overflow state");
	skills.pop_back();
	exchange.refresh(skills);
	require(!exchange.overflow && exchange.widgets.back() == skills.back(), "Returning to eight clears overflow and restores the eighth");

	skills.resize(1);
	exchange.refresh(skills);
	for(std::size_t index = 1; index < exchange.widgets.size(); ++index)
		require(exchange.widgets[index] == ExchangeModel::Skill{-1, 0}, "Shrinking clears every stale skill");
	skills.clear();
	exchange.refresh(skills);
	require(!exchange.overflow && exchange.widgets.front() == ExchangeModel::Skill{-1, 0}, "Empty skill list clears all widgets and overflow");
	return EXIT_SUCCESS;
}
