/*
 * StackInfoPanelHoverStateTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../battle/StackInfoPanelHoverState.h"

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
	newHorizonsBattleStatus::StackInfoPanelHoverRetention retention;

	// Pointer path: inspect a stack, cross empty battlefield hexes, then reach
	// its side panel. The actual retention policy must keep the same panel alive.
	retention.stackInspected();
	require(retention.retain(450, false), "Panel closes on first empty battlefield hex");
	require(retention.retain(450, false), "Panel closes while crossing empty battlefield");
	require(retention.retain(450, false), "Panel closes before reaching distant side panel");
	require(retention.retain(16, true), "Panel closes when pointer reaches its badge");

	// Leaving the panel starts another travel window; lingering elsewhere closes it.
	require(retention.retain(1800, false), "Panel closes during ordinary travel away from its surface");
	require(!retention.retain(2000, false), "Panel remains pinned after pointer rests elsewhere");

	retention.stackInspected();
	require(!retention.retain(2000, false), "Panel does not outlive the grace window without panel hover");
	std::cout << "Stack info panel hover retention PASS\n";
}
