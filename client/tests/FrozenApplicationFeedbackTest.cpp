/*
 * FrozenApplicationFeedbackTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later
 */
#include "../battle/FrozenApplicationFeedback.h"

#include <cstdlib>
#include <iostream>

namespace
{
void require(bool value, const char * message)
{
	if(!value)
	{
		std::cerr << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}
}

int main()
{
	newHorizonsFrozenPresentation::ApplicationFeedback feedback;
	feedback.seed(7, 3);
	require(!feedback.record(7, 3), "Existing Frozen on resume must not sound");
	require(!feedback.record(7, 2), "A stale application must not sound");
	require(feedback.record(8, 3), "A newly accepted recipient must sound");
	require(!feedback.record(8, 3), "Duplicate packet/UI callbacks must not repeat sound");
	require(!feedback.record(8, 3), "Redraw/reopen cannot manufacture a new application");
	require(feedback.record(7, 4), "A later legal application after Cure/thaw must sound");
	require(!feedback.record(7, 4), "The new application must still sound only once");
	require(feedback.record(9, 0), "Application round zero is valid");
	require(!feedback.record(10, -1), "Missing application provenance must not sound");
	feedback.seed(11, 6);
	feedback.seed(11, 5);
	require(!feedback.record(11, 6), "An older initial snapshot cannot rewind the ledger");
	newHorizonsFrozenPresentation::ApplicationFeedback nextBattle;
	require(nextBattle.record(7, 0), "A different battle owns an independent presentation ledger");
	std::cout << "Frozen application feedback PASS\n";
}
