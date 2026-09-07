/*
 * HeroDevelopmentNavigationTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 * Standalone UI-state test; no renderer/game/serialization acceptance implied.
 */
#include "../windows/HeroDevelopmentNavigation.h"

#include <cstdlib>
#include <iostream>

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
}

int main()
{
	using Section = HeroDevelopmentSection;
	HeroDevelopmentNavigation navigation;
	require(!navigation.selected(), "No default section without a saved view");
	require(!navigation.select(Section::MASTERIES), "Absent masteries cannot be opened");

	// All combinations are defensive UI coverage, not a claim every ruleset can
	// produce them. Leadership and siege currently share a saved capability gate.
	for(unsigned mask = 0; mask < 16; ++mask)
	{
		HeroDevelopmentNavigation::Availability available{};
		for(std::size_t index = 0; index < available.size(); ++index)
			available[index] = (mask & (1u << index)) != 0;
		navigation.refresh(available);
		require(navigation.selected().has_value() == (mask != 0), "Selection matches actual availability");
		for(std::size_t index = 0; index < available.size(); ++index)
		{
			const auto section = static_cast<Section>(index);
			const auto before = navigation.selected();
			require(navigation.select(section) == available[index], "Only available sections selectable");
			if(available[index])
			{
				require(navigation.selected() == section, "Explicit selection is retained");
				navigation.refresh(available);
				require(navigation.selected() == section, "Same-context refresh does not reset tab");
			}
			else
				require(navigation.selected() == before, "Unavailable selection leaves current tab unchanged");
		}
	}

	navigation.refresh({true, true, true, true});
	require(navigation.select(Section::MASTERIES), "Select saved mastery readback");
	navigation.refresh({true, false, false, true});
	require(navigation.selected() == Section::MASTERIES, "Removing other families preserves selection");
	navigation.refresh({false, true, true, false});
	require(navigation.selected() == Section::LEADERSHIP, "Removed selected family falls back to FIRST of multiple available views");
	navigation.refresh({false, false, false, false});
	navigation.refresh({false, true, true, true});
	require(navigation.selected() == Section::LEADERSHIP, "Absent selection initializes to FIRST of multiple available views");
	navigation.refresh({true, false, false, false});
	require(navigation.selected() == Section::GROWTH, "Removed selected family falls back to available growth");
	navigation.refresh({false, false, true, false});
	require(navigation.selected() == Section::SIEGE, "Capability-only view never fabricates primary growth");
	require(!navigation.select(Section::COUNT), "Sentinel cannot select a section");
	require(!navigation.select(static_cast<Section>(-1)), "Invalid section cannot index availability");
	navigation.refresh({false, false, false, false});
	require(!navigation.selected(), "All absent clears selection");
	std::cout << "Hero development navigation state PASS\n";
}
