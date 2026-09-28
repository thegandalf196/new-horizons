/*
 * StackInfoPanelHoverStateTest.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../battle/StackInfoPanelHoverState.h"
#include "../battle/StackInfoStatusPresentation.h"

#include <cstdlib>
#include <iostream>
#include <vector>

namespace
{
void require(bool condition, const char * message)
{
	if(!condition)
	{
		std::cerr << message << '\n';
		std::exit(EXIT_FAILURE);
	}
}
}

int main()
{
	newHorizonsBattleStatus::StackInfoPanelHoverRetention retention;

	// Pointer path: inspect a stack, cross empty battlefield hexes, then reach
	// its side panel. The actual retention policy must keep the same panel alive.
	retention.stackInspected();
	require(retention.retain(450, false), "Panel closes on first empty battlefield hex");
	require(retention.retain(450, false), "Panel closes while crossing empty battlefield");
	require(retention.retain(450, false), "Panel closes before reaching distant side panel");
	require(retention.retain(16, true), "Panel closes when pointer reaches its badge");

	// Leaving the panel starts another travel window; lingering elsewhere closes it.
	require(retention.retain(1800, false), "Panel closes during ordinary travel away from its surface");
	require(!retention.retain(2000, false), "Panel remains pinned after pointer rests elsewhere");

	retention.stackInspected();
	require(!retention.retain(2000, false), "Panel does not outlive the grace window without panel hover");

	using newHorizonsBattleStatus::makePhysicalPoisonStatus;
	const auto appliedPoison = makePhysicalPoisonStatus(10, 3, 10);
	const auto secondActivation = makePhysicalPoisonStatus(10, 2, 15);
	const auto thirdActivation = makePhysicalPoisonStatus(10, 1, 20);
	const auto curedPoison = makePhysicalPoisonStatus(0, 0, 0);
	const auto strongerRefresh = makePhysicalPoisonStatus(14, 3, 14);
	const auto equalRefreshAfterTick = makePhysicalPoisonStatus(10, 3, 10);
	const newHorizonsBattleStatus::PhysicalPoisonStatus noPoison;
	require(appliedPoison.active(), "Fresh physical Poison status is not active");
	require(appliedPoison.activationsRemaining == 3, "Fresh physical Poison does not show three activations");
	require(appliedPoison != secondActivation && secondActivation != thirdActivation,
		"Poison activation ticks do not change the status refresh snapshot");
	require(curedPoison == noPoison && !curedPoison.active(), "Cure and expiry do not clear the physical Poison snapshot");
	require(appliedPoison != strongerRefresh, "Stronger physical Poison refresh does not change the snapshot");
	require(secondActivation != equalRefreshAfterTick, "Equal physical Poison refresh does not restore its activation count");

	using newHorizonsBattleStatus::StackStatusIconKind;
	const std::vector<StackStatusIconKind> poisonedStatuses = {
		StackStatusIconKind::ORDINARY,
		StackStatusIconKind::FOCUS_OR_ARCANE,
		StackStatusIconKind::PHYSICAL_POISON,
		StackStatusIconKind::SPELL_LOCK,
		StackStatusIconKind::TIME_STOP
	};
	const auto poisonedPlan = newHorizonsBattleStatus::stackStatusDisplayPlan(poisonedStatuses, 5);
	require(poisonedPlan.overflow && !poisonedPlan.ellipsisUsesSlot,
		"Poisoned overflow reserves the status slots instead of replacing Poison with an ellipsis");
	require(poisonedPlan.visibleEntryIndices.size() == 3,
		"Poisoned overflow does not keep three priority statuses visible");
	require(poisonedStatuses[poisonedPlan.visibleEntryIndices[0]] == StackStatusIconKind::TIME_STOP
		&& poisonedStatuses[poisonedPlan.visibleEntryIndices[1]] == StackStatusIconKind::SPELL_LOCK
		&& poisonedStatuses[poisonedPlan.visibleEntryIndices[2]] == StackStatusIconKind::PHYSICAL_POISON,
		"Time Stop, Spell Lock, and physical Poison lost their compact-panel priority");
	const std::vector<StackStatusIconKind> ordinaryStatuses = {
		StackStatusIconKind::ORDINARY,
		StackStatusIconKind::FOCUS_OR_ARCANE,
		StackStatusIconKind::ORDINARY,
		StackStatusIconKind::ORDINARY
	};
	const auto ordinaryPlan = newHorizonsBattleStatus::stackStatusDisplayPlan(ordinaryStatuses, 4);
	require(ordinaryPlan.overflow && ordinaryPlan.ellipsisUsesSlot && ordinaryPlan.visibleEntryIndices.size() == 2,
		"Unpoisoned overflow no longer reserves its third slot for the existing ellipsis");
	std::cout << "Stack info panel hover retention PASS\n";
}
