/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include <memory>

/// The concrete shared restart/quit sequence. The callback is only the existing
/// disconnect/receive-retirement step, not a task scheduler. A yielding step may
/// destroy/replace the unique client: never continue on that replacement.
template<typename Client, typename BeforeDestruction>
bool retireClientGameplay(std::unique_ptr<Client> & owner, BeforeDestruction beforeDestruction)
{
	if(!owner)
		return false;
	const auto source = owner->retainGameplaySource();
	const auto current = [&] { return owner && owner->matchesGameplaySource(source); };
	owner->endNetwork();
	if(!current() || !owner->finishGameplay() || !current())
		return false;
	if(!beforeDestruction() || !current())
		return false;
	if(!owner->endGame() || !owner || !owner->matchesRetiredGameplaySource(source))
		return false;
	owner.reset();
	return true;
}
