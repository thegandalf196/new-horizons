/*
 * NewHorizonsCrisisCommand.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 */
#pragma once

#include "BattleAction.h"
#include "HeroActionAllowanceState.h"
#include "../json/JsonNode.h"
#include <array>
#include <vector>

class CBattleInfoCallback;
class CGHeroInstance;
class IBattleInfo;
struct SeizeInitiativeState;

namespace newHorizonsCrisisCommand
{
inline constexpr const char * SKILL = "new-horizons:command";
inline constexpr const char * PERK = "new-horizons:command.crisisCommand";
inline constexpr uint32_t NO_UNIT = HeroOrderState::INVALID_UNIT_ID;

enum class Phase : uint8_t { CHOICE, GRANTED_EXTRA };
enum class ReturnKind : uint8_t { ACTION, NEXT_STACK };

/// A suspended *completed* action. Its damage and EndAction have already been
/// applied. Only its ordinary completion/continuation may be resumed.
struct DLL_LINKAGE ReturnFrame
{
	BattleSide responder = BattleSide::NONE;
	uint32_t anchor = NO_UNIT;
	uint32_t grant = 0;
	int32_t round = -1;
	int32_t originalActor = -1;
	Phase phase = Phase::CHOICE;
	ReturnKind kind = ReturnKind::NEXT_STACK;
	BattleAction action;
	bool masterGate = false;
	bool pursuit = false;
	bool ranged = false;
	uint32_t suspendedSeizeActive = NO_UNIT;
	bool suspendedSeizeActiveNormal = false;
	bool sameContinuation(const ReturnFrame & other) const;

	template<typename Handler> void serialize(Handler & h)
	{
		h & responder; h & anchor; h & grant; h & round; h & originalActor;
		h & phase; h & kind; h & action;
		h & masterGate; h & pursuit; h & ranged;
		h & suspendedSeizeActive; h & suspendedSeizeActiveNormal;
	}
};

struct DLL_LINKAGE DeathReceipt
{
	BattleSide side = BattleSide::NONE;
	uint32_t unit = NO_UNIT;
	bool operator==(const DeathReceipt &) const = default;
	template<typename Handler> void serialize(Handler & h) { h & side; h & unit; }
};

struct DLL_LINKAGE State
{
	std::array<bool, 2> used{};
	std::vector<DeathReceipt> pending;
	/// Two once-per-combat heroes bound nesting; never recursive hit windows.
	std::vector<ReturnFrame> returns;
	bool meaningful() const { return used[0] || used[1] || !pending.empty() || !returns.empty(); }
	bool choice() const { return !returns.empty() && returns.back().phase == Phase::CHOICE; }
	BattleSide chooser() const { return choice() ? returns.back().responder : BattleSide::NONE; }
	bool allowsGrant(BattleSide side, const HeroActionAllowanceState::Grant & candidate) const
	{
		if(choice())
			return side == chooser() && candidate.id == returns.back().grant
				&& candidate.source == HeroActionAllowanceState::GrantSource::CRISIS_COMMAND;
		return candidate.source != HeroActionAllowanceState::GrantSource::CRISIS_COMMAND;
	}
	void validateShape() const;
	void validateTransitionFrom(const State & previous) const;
	/// Restore only interrupted activation provenance, never roll back progress.
	SeizeInitiativeState seizeContextAfterTransition(const State & previous,
		const SeizeInitiativeState & current) const;
	void validate(const IBattleInfo & battle, BattleSide replacementSide = BattleSide::NONE,
		const HeroActionAllowanceState * replacementLedger = nullptr) const;
	/// Descriptor decoding precedes localInit: validate identity/provenance, not live health.
	void validateSerializedReferences(const IBattleInfo & battle, BattleSide replacementSide = BattleSide::NONE,
		const HeroActionAllowanceState * replacementLedger = nullptr) const;
	bool capture(BattleSide side, uint32_t unit);

	template<typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
		{
			validateShape();
			if(meaningful() && !h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND))
				throw std::runtime_error("Cannot discard Crisis Command suspension");
		}
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_CRISIS_COMMAND))
		{
			h & used; h & pending; h & returns;
			if(!h.saving) validateShape();
		}
		else if(!h.saving)
			*this = State{};
	}
};

/// Reads the validated captured registry, not a second feature toggle.
DLL_LINKAGE bool activeProfile(const JsonNode & rules);
DLL_LINKAGE void validateProfileSerialization(const JsonNode & rules, bool supported);
DLL_LINKAGE bool eligible(const CGHeroInstance * hero);
DLL_LINKAGE const battle::Unit * anchor(const CBattleInfoCallback & battle, BattleSide side);
}
