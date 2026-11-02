/* Optional level-up query observations, part of VCMI / New Horizons; GPL v2.0 or later. */
#pragma once
#include "../../../lib/constants/EntityIdentifiers.h"
#include <map>
#include <mutex>

namespace NK2AI
{
/// Ephemeral command deduplication only; this is not a view of server query state.
class DLL_EXPORT LevelUpOfferState
{
public:
	enum class Event { IGNORE, INITIAL, REPLACEMENT, CONFIRMED_REJECTION };
	Event observe(ObjectInstanceID hero, QueryID query, QueryID replaces, bool available);
	bool reroll(ObjectInstanceID hero, QueryID query);
	bool select(ObjectInstanceID hero, QueryID query);
private:
	enum class Phase { READY, REROLL_REQUESTED, CHOICE_SENT };
	struct Pending { QueryID query; Phase phase; bool mayReroll; };
	std::map<ObjectInstanceID, Pending> latest;
	std::mutex mutex;
	bool transition(ObjectInstanceID hero, QueryID query, Phase next);
};
}
