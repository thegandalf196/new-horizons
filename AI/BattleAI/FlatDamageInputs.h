/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include "../../lib/combatScripts/NativeDamageFacts.h"
#include <map>
#include <memory>
#include <span>

class HypotheticBattle;

/// Typed numeric input frame for ONE immutable ordinary target-selector batch.
/// Health/count, aura, controller, source and position changes require a new
/// frame, including virtual changes. This is not a second battle simulation.
class DLL_LINKAGE FlatDamageInputs
{
	const HypotheticBattle & source;
	bool shooting;
	std::map<const battle::Unit *, nativeDamage::PreparedUnit> units;
	std::map<const battle::Unit *, nativeDamage::PreparedUnit> rangedUnits;
public:
	FlatDamageInputs(const HypotheticBattle & battle, bool shooting);
	/// All-or-nothing canonical native getter/source admission. Unknown custom
	/// sources retain the reference path. No incomplete facts are published.
	bool prepare(const battle::Unit & actor, std::span<const battle::Unit * const> targets);
	/// Shared fixed-pair pressure queries use both combat modes. Unsupported
	/// units retain ordinary capture independently. Every roster
	/// participant is materialized before this immutable preparation begins.
	bool prepareRoster(HypotheticBattle & branch);
	const nativeDamage::PreparedUnit * find(const battle::Unit & unit, bool shot) const;
	class DLL_LINKAGE Scope
	{
		HypotheticBattle & battle;
		const FlatDamageInputs * previous;
	public:
		Scope(HypotheticBattle & battle, const FlatDamageInputs * inputs);
		~Scope();
		Scope(const Scope &) = delete;
		Scope & operator=(const Scope &) = delete;
	};
};
