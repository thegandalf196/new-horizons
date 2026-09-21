/*
 * HeroCommand.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../json/JsonNode.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

struct Bonus;
class CGHeroInstance;

/// Stable identifiers in the versioned New Horizons combat ruleset.
enum class HeroCommand : int8_t
{
	NONE = 0,
	CHARGE = 1,
	HOLD_THE_LINE = 2,
	// Legacy Order identifier. Frozen for save/wire decoding; not emitted by
	// the current partial Orders roster.
	ADVANCE = 3,
	// Legacy Doctrine identifiers. Numeric values are frozen for save/wire decoding;
	// current New Horizons rules never expose or issue them.
	AGGRESSIVE = 4,
	DEFENSIVE = 5,
	FOCUS_FIRE = 6,
	// The canonical Orders keep the original command identifiers intact and use
	// new values above the old decode-only range.  Value 7 intentionally remains
	// unused: older clients/tests treated it as an invalid command and the wire
	// gap makes accidental legacy reinterpretation impossible.
	RIPOSTE = 8,
	BRACE = 9,
	PROTECT = 10,
	FLANK = 11,
	SECOND_WIND = 12
};

/// A value-only snapshot of the transient state created when an Order is issued.
/// It is deliberately independent of CStack pointers so it can be serialized in
/// saves and copied into client battle snapshots without exposing authority.
struct DLL_LINKAGE HeroOrderAnchor
{
	uint32_t unitId = std::numeric_limits<uint32_t>::max();
	int16_t position = -1;

	bool operator==(const HeroOrderAnchor &) const = default;

	template <typename Handler> void serialize(Handler & h)
	{
		h & unitId;
		h & position;
	}
};

struct DLL_LINKAGE HeroOrderFlankTarget
{
	uint32_t unitId = std::numeric_limits<uint32_t>::max();
	uint8_t sideMask = 0;

	bool operator==(const HeroOrderFlankTarget &) const = default;

	template <typename Handler> void serialize(Handler & h)
	{
		h & unitId;
		h & sideMask;
	}
};

struct DLL_LINKAGE HeroOrderState
{
	static constexpr uint32_t INVALID_UNIT_ID = std::numeric_limits<uint32_t>::max();

	HeroCommand command = HeroCommand::NONE;
	int32_t issuedRound = 0;
	uint32_t primaryTargetUnitId = INVALID_UNIT_ID;
	uint32_t secondaryTargetUnitId = INVALID_UNIT_ID;
	bool protectIntercepted = false;
	/// Protect is a one-way adjacency contract. Once either unit separates, it
	/// cannot become armed again merely by moving back next to its partner.
	bool protectBroken = false;
	bool secondWindActive = false;
	std::vector<uint32_t> consumedUnitIds;
	std::vector<uint32_t> braceTriggeredUnitIds;
	std::vector<uint32_t> holdBrokenUnitIds;
	std::vector<HeroOrderAnchor> anchors;
	std::vector<HeroOrderFlankTarget> flankTargets;

	bool operator==(const HeroOrderState &) const = default;

	bool containsConsumed(uint32_t unitId) const
	{
		return std::binary_search(consumedUnitIds.begin(), consumedUnitIds.end(), unitId);
	}

	bool containsBraceTrigger(uint32_t unitId) const
	{
		return std::binary_search(braceTriggeredUnitIds.begin(), braceTriggeredUnitIds.end(), unitId);
	}

	bool containsHoldBroken(uint32_t unitId) const
	{
		return std::binary_search(holdBrokenUnitIds.begin(), holdBrokenUnitIds.end(), unitId);
	}

	const HeroOrderAnchor * anchorFor(uint32_t unitId) const
	{
		const auto it = std::find_if(anchors.begin(), anchors.end(), [unitId](const HeroOrderAnchor & anchor)
		{
			return anchor.unitId == unitId;
		});
		return it == anchors.end() ? nullptr : &*it;
	}

	HeroOrderFlankTarget * flankFor(uint32_t unitId)
	{
		const auto it = std::find_if(flankTargets.begin(), flankTargets.end(), [unitId](const HeroOrderFlankTarget & target)
		{
			return target.unitId == unitId;
		});
		return it == flankTargets.end() ? nullptr : &*it;
	}

	const HeroOrderFlankTarget * flankFor(uint32_t unitId) const
	{
		const auto it = std::find_if(flankTargets.begin(), flankTargets.end(), [unitId](const HeroOrderFlankTarget & target)
		{
			return target.unitId == unitId;
		});
		return it == flankTargets.end() ? nullptr : &*it;
	}

	void validateShape() const
	{
		const auto maxWireId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
		if(command == HeroCommand::NONE || issuedRound < 1
			|| (primaryTargetUnitId != INVALID_UNIT_ID && primaryTargetUnitId > maxWireId)
			|| (secondaryTargetUnitId != INVALID_UNIT_ID && secondaryTargetUnitId > maxWireId)
			|| !std::is_sorted(consumedUnitIds.begin(), consumedUnitIds.end())
			|| std::adjacent_find(consumedUnitIds.begin(), consumedUnitIds.end()) != consumedUnitIds.end()
			|| !std::is_sorted(braceTriggeredUnitIds.begin(), braceTriggeredUnitIds.end())
			|| std::adjacent_find(braceTriggeredUnitIds.begin(), braceTriggeredUnitIds.end()) != braceTriggeredUnitIds.end()
			|| !std::is_sorted(holdBrokenUnitIds.begin(), holdBrokenUnitIds.end())
			|| std::adjacent_find(holdBrokenUnitIds.begin(), holdBrokenUnitIds.end()) != holdBrokenUnitIds.end())
			throw std::runtime_error("Invalid New Horizons Hero Order state shape");
		for(const auto & id : consumedUnitIds)
			if(id > maxWireId)
				throw std::runtime_error("Invalid consumed Hero Order unit identity");
		for(const auto & id : braceTriggeredUnitIds)
			if(id > maxWireId)
				throw std::runtime_error("Invalid Brace Hero Order unit identity");
		for(const auto & id : holdBrokenUnitIds)
			if(id > maxWireId)
				throw std::runtime_error("Invalid Hold the Line Hero Order unit identity");
		for(const auto & anchor : anchors)
			if(anchor.unitId > maxWireId || anchor.position < 0 || anchor.position >= 2047)
				throw std::runtime_error("Invalid Hold the Line anchor");
		uint32_t previous = 0;
		bool firstFlankTarget = true;
		for(const auto & target : flankTargets)
		{
			if(target.unitId > maxWireId || target.sideMask > 0x3f || (!firstFlankTarget && target.unitId <= previous))
				throw std::runtime_error("Invalid Flank target state");
			previous = target.unitId;
			firstFlankTarget = false;
		}
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving)
			validateShape();
		h & command;
		h & issuedRound;
		h & primaryTargetUnitId;
		h & secondaryTargetUnitId;
		h & protectIntercepted;
		h & protectBroken;
		h & secondWindActive;
		h & consumedUnitIds;
		h & braceTriggeredUnitIds;
		h & holdBrokenUnitIds;
		h & anchors;
		h & flankTargets;
		if(!h.saving)
			validateShape();
	}
};

namespace heroCommands
{
constexpr int RULESET_VERSION = 1;
constexpr int TARGETED_RULESET_VERSION = 2;
/// Current New Horizons data emits Orders only.  Versions 1 and 2 remain
/// readable because they are embedded in existing saves and battle snapshots.
constexpr int ORDERS_ONLY_RULESET_VERSION = 3;
constexpr int CURRENT_RULESET_VERSION = ORDERS_ONLY_RULESET_VERSION;
constexpr int MIN_EFFECT_PERCENT = -90;
constexpr int MAX_EFFECT_PERCENT = 200;
/// Arithmetic safety bound, not a gameplay balance target.
constexpr double MAX_TARGETED_COEFFICIENT = 1000000;
DLL_LINKAGE std::string key(HeroCommand command);
DLL_LINKAGE bool isDoctrine(HeroCommand command);
DLL_LINKAGE bool valid(HeroCommand command);
/// Rules must have passed validateRules; legacy enum values never enable a new command.
DLL_LINKAGE bool supportedByRules(const JsonNode & rules, HeroCommand command);
/// True only for the command identifiers emitted by the current ruleset.
DLL_LINKAGE bool isActive(HeroCommand command);
/// True for the canonical eight-Order profile (as opposed to a legacy three-Order v3 snapshot).
DLL_LINKAGE bool isCanonicalRules(const JsonNode & rules);
/// Empty rules mean legacy gameplay. Unsupported or malformed nonempty rules fail closed.
DLL_LINKAGE void validateRules(const JsonNode & rules);
DLL_LINKAGE int coefficient(const JsonNode & effect, int attack, int defense);
/// Evaluate an Order formula for a hero, scaling only its Attack/Defense-derived
/// terms by the hero's New Horizons Command rank (100/110/120/130%).
DLL_LINKAGE int coefficient(const JsonNode & effect, const CGHeroInstance & hero);
DLL_LINKAGE int efficiencyPercent(const CGHeroInstance & hero);
DLL_LINKAGE int secondWindPercent(const CGHeroInstance & hero);
DLL_LINKAGE std::vector<Bonus> bonuses(const JsonNode & rules, HeroCommand command, const CGHeroInstance & hero);
}
