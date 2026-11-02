/* Optional level-up query observations, part of VCMI / New Horizons; GPL v2.0 or later. */
#include "../StdInc.h"
#include "LevelUpOfferState.h"

namespace NK2AI
{
LevelUpOfferState::Event LevelUpOfferState::observe(ObjectInstanceID hero, QueryID query, QueryID replaces, bool available)
{
	if(hero < ObjectInstanceID(0) || query < QueryID(0))
		return Event::IGNORE;
	std::lock_guard lock(mutex);
	const auto found = latest.find(hero);
	if(found != latest.end())
	{
		auto & pending = found->second;
		if(query == pending.query)
		{
			if(replaces == query && pending.phase == Phase::REROLL_REQUESTED)
			{
				pending.phase = Phase::READY;
				pending.mayReroll = false; // This AI already attempted; server eligibility is unchanged.
				return Event::CONFIRMED_REJECTION;
			}
			return Event::IGNORE;
		}
		if(query < pending.query || (replaces >= QueryID(0)
			&& (replaces != pending.query || pending.phase != Phase::REROLL_REQUESTED)))
			return Event::IGNORE;
	}
	else if(replaces >= QueryID(0))
		return Event::IGNORE;
	latest.insert_or_assign(hero, Pending{query, Phase::READY, available && replaces < QueryID(0)});
	return replaces >= QueryID(0) ? Event::REPLACEMENT : Event::INITIAL;
}

bool LevelUpOfferState::transition(ObjectInstanceID hero, QueryID query, Phase next)
{
	std::lock_guard lock(mutex);
	const auto found = latest.find(hero);
	if(found == latest.end() || found->second.query != query || found->second.phase != Phase::READY
		|| (next == Phase::REROLL_REQUESTED && !found->second.mayReroll))
		return false;
	found->second.phase = next;
	found->second.mayReroll = false;
	return true;
}

bool LevelUpOfferState::reroll(ObjectInstanceID hero, QueryID query)
{
	return transition(hero, query, Phase::REROLL_REQUESTED);
}

bool LevelUpOfferState::select(ObjectInstanceID hero, QueryID query)
{
	return transition(hero, query, Phase::CHOICE_SENT);
}
}
