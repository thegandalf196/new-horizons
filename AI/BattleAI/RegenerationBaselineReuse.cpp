/* RegenerationBaselineReuse.cpp, VCMI / New Horizons, GPL v2.0 or later. */
#include "StdInc.h"
#include "RegenerationBaselineReuse.h"
#include "VinesCandidateValueReuse.h"
#include "../../lib/battle/Unit.h"
#include <stdexcept>

RegenerationBaselineReuse::Session RegenerationBaselineReuse::prepare(
	const Environment * environment, const CBattleInfoCallback & before, const std::vector<battle::Units> & order,
	const DamageCache & damage, BattleSide side, PlayerColor player)
{
	if(control && !control->enabled)
		return {};
	// Reuse existing attestation. Unknown/custom or failed capability queries keep the original path.
	try
	{
		if(!VinesCandidateValueReuse::scoringScriptsSupported(before))
			return {};
	}
	catch(const std::exception &)
	{
		return {};
	}
	std::vector<std::vector<uint32_t>> queue;
	for(const auto & round : order)
	{
		auto & ids = queue.emplace_back();
		for(const auto * unit : round)
			ids.push_back(unit->unitId());
	}
	if(key && key->environment == environment && key->before == &before && key->queue == queue && key->side == side
		&& key->player == player && damage.matchesSnapshot(key->damage))
	{
		if(control)
			++control->hits;
		return {&observations, std::nullopt};
	}
	return {nullptr, Key{environment, &before, std::move(queue), side, player, damage.captureSnapshot()}};
}

void RegenerationBaselineReuse::complete(Session && session, std::vector<Observation> completed)
{
	if(!session.recording)
		return;
	size_t expected = 0;
	for(const auto & round : session.recording->queue)
		for(const auto id : round)
		{
			if(expected >= completed.size() || completed[expected].unit != id)
				throw std::logic_error("Incomplete Regeneration baseline observation sequence");
			++expected;
		}
	if(expected != completed.size())
		throw std::logic_error("Extra Regeneration baseline observations");
	key = std::move(session.recording);
	observations = std::move(completed);
	if(control)
		++control->completed;
}
