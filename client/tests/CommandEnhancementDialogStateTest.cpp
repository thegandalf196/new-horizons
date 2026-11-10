/*
 * CommandEnhancementDialogStateTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "../windows/CommandEnhancementDialogState.h"
#include <stdexcept>

namespace
{
void require(bool value)
{
	if(!value)
		throw std::runtime_error("Command enhancement lifecycle regression");
}
}

int main()
{
	CommandEnhancementDialogState first(31), next(32);
	require(!first.beginSubmission());
	require(!first.select(-1) && !first.select(9));
	for(int option = 0; option < 9; ++option)
		require(first.select(option) && first.selected() == option);
	require(first.beginSubmission());
	require(!first.select(0) && !first.beginSubmission());
	first.submitted(-1);
	require(!first.isWaiting() && first.selected() == -1);
	require(first.select(8) && first.beginSubmission());
	first.submitted(100);
	require(!first.requestApplied(99, false) && first.isWaiting());
	require(!first.requestApplied(100, true) && first.isWaiting());
	require(first.requestApplied(100, false) && !first.isWaiting());
	require(first.select(3) && first.beginSubmission());
	first.submitted(101);
	require(!first.requestApplied(100, false) && first.isWaiting());
	first.queryResolved(32);
	require(!first.canClose(true));
	first.queryResolved(31); // Resolution while covered must not pop the help window.
	require(!first.canClose(false) && first.canClose(true));
	require(!first.select(5) && !first.beginSubmission());
	require(!first.requestApplied(101, false));
	require(!next.canClose(true) && next.select(0));
	return 0;
}
