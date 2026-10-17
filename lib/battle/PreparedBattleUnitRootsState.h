/*
 * PreparedBattleUnitRootsState.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include "PreparedBattleUnitRoots.h"
#include "BattleUnitRootOrigin.h"
#include "ProjectedBattleWorldArenaState.h"
#include "PreparedStackStateTransfer.h"

namespace battle
{
/// Private lifetime storage: plans die BEFORE the borrowed arena/battle.
struct PreparedBattleUnitRoots::Data
{
	using Slot = ProjectedBattleWorldArena::Data::Slot;
	struct Occurrence
	{
		Slot * slot;
		CStack * original;
		CUnitStateDetached * state;
	};
	struct Added
	{
		Occurrence occurrence;
		const BattleUnitRootIdentity * creationIdentity;
		std::unique_ptr<PreparedStackStateTransfer> values;
	};
	BattleInfo & destination;
	ProjectedBattleWorldArena::Data & arena;
	const BattleUnitRootOrigin & origin;
	std::vector<Occurrence> occurrences;
	std::vector<Added> added;
	// Null slots are allocated before any live write. After exchange this holds
	// the OLD allocation, with no CStack ownership: retire only with the plan.
	std::vector<std::unique_ptr<CStack>> replacement;
	bool consumed = false;
	Data(BattleInfo & destination, ProjectedBattleWorldArena::Data & arena);
	bool rootsUnchanged() const noexcept;
	bool occurrencesUnchanged() const noexcept;
};
}
