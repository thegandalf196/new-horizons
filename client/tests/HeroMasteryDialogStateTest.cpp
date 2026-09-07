/*
 * Standalone client presentation regression; no engine/SDL/assets required.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../windows/HeroMasteryDialogState.h"
#include <stdexcept>
#include <initializer_list>

static void require(bool value)
{
	if(!value)
		throw std::runtime_error("Mastery dialog lifecycle regression");
}

int main()
{
	HeroMasteryDialogState first(11), other(12);
	require(!first.beginSubmission()); // No automatic first option.
	require(!first.select(-1) && !first.select(3));
	require(first.select(2) && first.beginSubmission());
	first.submitted(-1);
	require(!first.isWaiting() && first.selected() == -1);
	require(first.select(1) && first.beginSubmission());
	first.submitted(41);
	require(!first.select(0) && !first.beginSubmission());

	// Route to every mandatory dialog while a help popup is topmost. Only the
	// exact submitted request consumes a failure; the popup is not dismissed.
	for(auto * window : {&first, &other})
		window->requestApplied(40, false);
	require(first.isWaiting());
	for(auto * window : {&first, &other})
		window->requestApplied(41, false);
	require(!first.isWaiting() && first.selected() == -1);
	require(!first.canClose(false) && !first.canClose(true));
	require(!other.isResolved());
	require(first.select(0) && first.beginSubmission());
	first.submitted(42);
	require(!first.requestApplied(41, false)); // Late ACK cannot unlock retry.
	require(!first.requestApplied(42, true)); // Success ACK alone is not completion.
	require(first.isWaiting() && !first.isResolved());

	for(auto * window : {&first, &other})
		window->queryResolved(99);
	require(!first.isResolved() && !other.isResolved());
	for(auto * window : {&first, &other})
		window->queryResolved(11);
	require(first.isResolved() && !other.isResolved());
	require(!first.canClose(false)); // Covered: never pop someone else's window.
	require(first.canClose(true)); // Popup dismissed: normal update can close it.
	require(!first.select(2) && !first.beginSubmission());
	require(!first.requestApplied(42, false)); // Late failure cannot resurrect it.
	return 0;
}
