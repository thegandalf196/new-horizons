/*
 * ProjectedUnitRemoval.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#pragma once
#include <cstdint>
#include <vector>

class CStack;
class ProjectedBonusGraph;
class ProjectedBonusRoster;
namespace battle
{
class CUnitStateDetached;

/// Executable REMOVE value/edge kernel, not a packet/world operation. Caller
/// retains all descriptors, states, graph and complete ordered roster under its
/// existing ownership interval. On failure discard ALL private values and graph.
/// The world owner MUST still apply the completion-dependent reduced-activation
/// and Protect stages, then packet-level Bloodrage/Crisis and replay preparation.
class DLL_LINKAGE ProjectedUnitRemoval final
{
public:
	struct Binding { CStack * original; CUnitStateDetached * state; };
	struct Result { bool completed; uint32_t missingId; };
	ProjectedUnitRemoval(ProjectedBonusGraph & graph, const ProjectedBonusRoster & roster,
		std::vector<Binding> ordered);
	/// Missing root/clone is the legacy early return, NOT a rejection/exception.
	Result removeValuesAndEdges(uint32_t id);
private:
	ProjectedBonusGraph & graph;
	const ProjectedBonusRoster & roster;
	std::vector<Binding> ordered;
	bool failed = false;
	void validate() const;
	Binding * find(uint32_t id);
};
}
