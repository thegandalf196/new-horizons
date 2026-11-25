/*
 * ProjectedAdjacencyRoster.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedAdjacencyRoster.h"
#include "ProjectedAdjacencyBonuses.h"
#include "HypotheticBattle.h"
#include "StackWithBonuses.h"
#include <cstdlib>
#include <string_view>

namespace
{
bool rosterEnvironmentIs(const char * name, std::string_view value)
{
	const char * actual = std::getenv(name);
	return actual && std::string_view(actual) == value;
}

bool rosterReuseDisabled()
{
	static const bool disabled = rosterEnvironmentIs("NH_AI_ADJACENCY_ROSTER", "0");
	return disabled;
}

bool rosterCheckerEnabled()
{
	static const bool enabled = rosterEnvironmentIs("NH_AI_ADJACENCY_ROSTER_CHECK", "1");
	return enabled;
}

void rosterMix(uint64_t & digest, uint64_t value) noexcept
{
	uint64_t z = digest ^ (value + 0x9e3779b97f4a7c15ULL);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	digest = z ^ (z >> 31);
}

void rosterMixPointer(uint64_t & digest, const void * pointer) noexcept
{
	rosterMix(digest, static_cast<uint64_t>(reinterpret_cast<uintptr_t>(pointer)));
}

std::shared_ptr<const ProjectedAdjacencyRoster> freshRoster(const HypotheticBattle & branch)
{
	auto result = std::make_shared<ProjectedAdjacencyRoster>();
	for(const auto * unit : branch.battleGetUnitsIf([](const battle::Unit *) { return true; }))
	{
		result->units.push_back(unit);
		result->states.push_back(dynamic_cast<const battle::CUnitState *>(unit));
	}
	return result;
}

void verifyRoster(const ProjectedAdjacencyRoster & reused, const ProjectedAdjacencyRoster & fresh)
{
	if(reused.units == fresh.units && reused.states == fresh.states)
		return;
	logAi->error("Projected adjacency roster mismatch: reused %d units, fresh %d units",
		reused.units.size(), fresh.units.size());
	std::abort();
}
}

std::shared_ptr<const ProjectedAdjacencyRoster> ProjectedAdjacencyBonuses::cachedRoster(const HypotheticBattle & branch)
{
	const auto & slot = branch.sourceBinding.adjacencyRoster;
	if(rosterReuseDisabled())
		return freshRoster(branch);
	uint64_t digest = 0;
	std::vector<ProjectedAdjacencyRosterSlot::Level> levels;
	bool sameLevels = true;
	const HypotheticBattle * level = &branch;
	for(size_t depth = 0; level; ++depth)
	{
		// Other routings enumerate owned or exported units with side effects.
		if(depth >= 64 || level->compactForecast || level->compactReadSession || level->ownedFrameBatch)
			return freshRoster(branch);
		rosterMixPointer(digest, level);
		rosterMix(digest, level->stackStates.size());
		for(const auto & [id, unit] : level->stackStates)
		{
			rosterMix(digest, id);
			rosterMixPointer(digest, unit.get());
		}
		const CBattleInfoCallback * subject = level->subject.get();
		rosterMixPointer(digest, subject);
		ProjectedAdjacencyRosterSlot::Level current{subject, nullptr};
		sameLevels = sameLevels && depth < slot.levels.size() && slot.levels[depth].subject == subject;
		if(sameLevels)
			current.parent = slot.levels[depth].parent;
		else
		{
			if(depth > 0 && level->initialExportSubject())
				return freshRoster(branch);
			current.parent = dynamic_cast<const HypotheticBattle *>(subject);
		}
		levels.push_back(current);
		if(!current.parent)
		{
			// The native root's roster changes only with its bonus tree (units
			// added, removed, revived or turned to ghosts all advance it).
			const auto * bearer = subject ? subject->getBonusBearer() : nullptr;
			rosterMix(digest, bearer ? static_cast<uint32_t>(bearer->getTreeVersion()) : 0);
		}
		level = current.parent;
	}
	if(sameLevels && slot.roster && slot.digest == digest && levels.size() == slot.levels.size())
	{
		if(rosterCheckerEnabled())
			verifyRoster(*slot.roster, *freshRoster(branch));
		return slot.roster;
	}
	auto fresh = freshRoster(branch);
	slot.roster = fresh;
	slot.digest = digest;
	slot.levels = std::move(levels);
	return fresh;
}
