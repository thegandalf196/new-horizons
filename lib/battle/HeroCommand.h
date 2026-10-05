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
#include <array>
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
	static constexpr uint16_t MAX_HOLD_MAGICAL_REDUCTION_BASIS_POINTS = 5000;
	static constexpr uint16_t BASIS_POINTS_PER_PHYSICAL_PERCENT = 50;

	HeroCommand command = HeroCommand::NONE;
	int32_t issuedRound = 0;
	uint32_t primaryTargetUnitId = INVALID_UNIT_ID;
	uint32_t secondaryTargetUnitId = INVALID_UNIT_ID;
	/// Number of qualifying melee attacks redirected by Protect during this Order.
	/// This count and the issued-order limit below are public battle snapshots, so
	/// AI and clients never need to inspect a concealed opposing hero.
	uint8_t protectInterceptionsConsumed = 0;
	/// Protect allowance captured when the Order is issued: one normally, two with
	/// Shield Master. It does not change if perk data or hero visibility changes.
	uint8_t protectInterceptionLimit = 1;
	/// Protect is a one-way adjacency contract. Once either unit separates, it
	/// cannot become armed again merely by moving back next to its partner.
	bool protectBroken = false;
	bool secondWindActive = false;
	std::vector<uint32_t> consumedUnitIds;
	std::vector<uint32_t> braceTriggeredUnitIds;
	std::vector<uint32_t> holdBrokenUnitIds;
	std::vector<HeroOrderAnchor> anchors;
	std::vector<HeroOrderFlankTarget> flankTargets;
	/// Spell-to-Order Warcasting empowerment captured when this Order was issued.
	/// Later attacks must not consult the side's newly armed Order-to-Spell state.
	int32_t warcastingBonusPercent = 0;
	/// Iron Discipline's magical reduction, captured as basis points when Hold is issued.
	/// Basis points preserve exactly half of an odd integer physical reduction value.
	uint16_t holdMagicalReductionBasisPoints = 0;
	/// Sacred Command's attribute-efficiency increment, captured when this Order is issued.
	int32_t sacredCommandEfficiencyBonusPercent = 0;
	/// Knightly Sequence's additional attribute-efficiency increment, captured when this Order is issued.
	int32_t knightlySequenceEfficiencyBonusPercent = 0;

	/// Combined Divine Mandate efficiency used by existing Order formulas.
	int32_t divineMandateEfficiencyBonusPercent() const
	{
		return sacredCommandEfficiencyBonusPercent + knightlySequenceEfficiencyBonusPercent;
	}

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

	bool isHoldTheLineRecipient(uint32_t unitId, int16_t position, int32_t currentRound) const
	{
		if(command != HeroCommand::HOLD_THE_LINE || issuedRound != currentRound
			|| containsHoldBroken(unitId))
			return false;
		const auto * anchor = anchorFor(unitId);
		return anchor && anchor->position == position;
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
			|| warcastingBonusPercent < 0 || warcastingBonusPercent > 100
			|| (sacredCommandEfficiencyBonusPercent != 0 && sacredCommandEfficiencyBonusPercent != 10)
			|| (knightlySequenceEfficiencyBonusPercent != 0 && knightlySequenceEfficiencyBonusPercent != 5)
			|| holdMagicalReductionBasisPoints > MAX_HOLD_MAGICAL_REDUCTION_BASIS_POINTS
			|| holdMagicalReductionBasisPoints % BASIS_POINTS_PER_PHYSICAL_PERCENT != 0
			|| protectInterceptionLimit < 1 || protectInterceptionLimit > 2
			|| protectInterceptionsConsumed > protectInterceptionLimit
			|| (primaryTargetUnitId != INVALID_UNIT_ID && primaryTargetUnitId > maxWireId)
			|| (secondaryTargetUnitId != INVALID_UNIT_ID && secondaryTargetUnitId > maxWireId)
			|| !std::is_sorted(consumedUnitIds.begin(), consumedUnitIds.end())
			|| std::adjacent_find(consumedUnitIds.begin(), consumedUnitIds.end()) != consumedUnitIds.end()
			|| !std::is_sorted(braceTriggeredUnitIds.begin(), braceTriggeredUnitIds.end())
			|| std::adjacent_find(braceTriggeredUnitIds.begin(), braceTriggeredUnitIds.end()) != braceTriggeredUnitIds.end()
			|| !std::is_sorted(holdBrokenUnitIds.begin(), holdBrokenUnitIds.end())
			|| std::adjacent_find(holdBrokenUnitIds.begin(), holdBrokenUnitIds.end()) != holdBrokenUnitIds.end())
			throw std::runtime_error("Invalid New Horizons Hero Order state shape");
		if(command != HeroCommand::PROTECT
			&& (protectInterceptionsConsumed != 0 || protectInterceptionLimit != 1))
			throw std::runtime_error("Non-Protect Hero Order contains Protect interception state");
		if(command != HeroCommand::HOLD_THE_LINE && holdMagicalReductionBasisPoints != 0)
			throw std::runtime_error("Non-Hold Hero Order contains Iron Discipline state");
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
		if(h.saving && (protectInterceptionsConsumed > 1 || protectInterceptionLimit > 1)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SHIELD_MASTER))
			throw std::runtime_error("Cannot discard Shield Master Protect state");
		if(h.saving && warcastingBonusPercent != 0 && !h.hasFeature(Handler::Version::NEW_HORIZONS_WARCASTING))
			throw std::runtime_error("Cannot discard Warcasting Order snapshot");
		if(h.saving && holdMagicalReductionBasisPoints != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_IRON_DISCIPLINE))
			throw std::runtime_error("Cannot discard Iron Discipline Order snapshot");
		if(h.saving && sacredCommandEfficiencyBonusPercent != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_SACRED_COMMAND))
			throw std::runtime_error("Cannot discard Sacred Command Order snapshot");
		if(h.saving && knightlySequenceEfficiencyBonusPercent != 0
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_KNIGHTLY_SEQUENCE))
			throw std::runtime_error("Cannot discard Knightly Sequence Order snapshot");
		if(h.saving)
			validateShape();
		h & command;
		h & issuedRound;
		h & primaryTargetUnitId;
		h & secondaryTargetUnitId;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SHIELD_MASTER))
		{
			h & protectInterceptionsConsumed;
			h & protectInterceptionLimit;
		}
		else if(h.saving)
		{
			bool legacyProtectIntercepted = protectInterceptionsConsumed != 0;
			h & legacyProtectIntercepted;
		}
		else
		{
			bool legacyProtectIntercepted = false;
			h & legacyProtectIntercepted;
			protectInterceptionsConsumed = legacyProtectIntercepted ? 1 : 0;
			protectInterceptionLimit = 1;
		}
		h & protectBroken;
		h & secondWindActive;
		h & consumedUnitIds;
		h & braceTriggeredUnitIds;
		h & holdBrokenUnitIds;
		h & anchors;
		h & flankTargets;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_WARCASTING))
			h & warcastingBonusPercent;
		else if(!h.saving)
			warcastingBonusPercent = 0;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_IRON_DISCIPLINE))
			h & holdMagicalReductionBasisPoints;
		else if(!h.saving)
			holdMagicalReductionBasisPoints = 0;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_SACRED_COMMAND))
			h & sacredCommandEfficiencyBonusPercent;
		else if(!h.saving)
			sacredCommandEfficiencyBonusPercent = 0;
		if(h.hasFeature(Handler::Version::NEW_HORIZONS_KNIGHTLY_SEQUENCE))
			h & knightlySequenceEfficiencyBonusPercent;
		else if(!h.saving)
			knightlySequenceEfficiencyBonusPercent = 0;
		if(!h.saving)
			validateShape();
	}
};

namespace newHorizonsShieldMaster
{
constexpr char SKILL[] = "new-horizons:armorer";
constexpr char PERK[] = "new-horizons:armorer.shieldMaster";
constexpr uint8_t ORDINARY_PROTECT_INTERCEPTION_LIMIT = 1;
constexpr uint8_t SHIELD_MASTER_PROTECT_INTERCEPTION_LIMIT = 2;
}

namespace newHorizonsIronDiscipline
{
constexpr char SKILL[] = "new-horizons:armorer";
constexpr char PERK[] = "new-horizons:armorer.ironDiscipline";
constexpr uint16_t BASIS_POINTS_PER_PHYSICAL_PERCENT = HeroOrderState::BASIS_POINTS_PER_PHYSICAL_PERCENT;
}

namespace heroCommands
{
constexpr int RULESET_VERSION = 1;
constexpr int TARGETED_RULESET_VERSION = 2;
/// Current New Horizons data emits Orders only.  Versions 1 and 2 remain
/// readable because they are embedded in existing saves and battle snapshots.
constexpr int ORDERS_ONLY_RULESET_VERSION = 3;
constexpr int CURRENT_RULESET_VERSION = ORDERS_ONLY_RULESET_VERSION;
inline constexpr std::array<HeroCommand, 8> CANONICAL_COMMANDS{
	HeroCommand::CHARGE,
	HeroCommand::HOLD_THE_LINE,
	HeroCommand::FOCUS_FIRE,
	HeroCommand::RIPOSTE,
	HeroCommand::BRACE,
	HeroCommand::PROTECT,
	HeroCommand::FLANK,
	HeroCommand::SECOND_WIND};
constexpr int MIN_EFFECT_PERCENT = -90;
constexpr int MAX_EFFECT_PERCENT = 200;
/// Basic Offense Encirclement value for each additional distinct Flank side.
constexpr int ENCIRCLEMENT_ADDITIONAL_SIDE_PERCENT = 7;
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
/// Add captured Spell-to-Order Warcasting and Divine Mandate efficiency bonuses
/// to attribute-derived terms. Flat formula terms remain unchanged.
DLL_LINKAGE int coefficient(const JsonNode & effect, const CGHeroInstance & hero, int warcastingBonusPercent,
	int divineMandateEfficiencyBonusPercent);
DLL_LINKAGE int coefficient(const JsonNode & effect, const CGHeroInstance & hero, int warcastingBonusPercent);
DLL_LINKAGE int efficiencyPercent(const CGHeroInstance & hero);
DLL_LINKAGE int secondWindPercent(const CGHeroInstance & hero);
/// Warcasting and Divine Mandate scale only Second Wind's Leadership-derived
/// part; its base 50% damage component remains flat.
DLL_LINKAGE int secondWindPercent(const CGHeroInstance & hero, int warcastingBonusPercent,
	int divineMandateEfficiencyBonusPercent);
DLL_LINKAGE int secondWindPercent(const CGHeroInstance & hero, int warcastingBonusPercent);
/// True when the hero currently has the active Expert Command Double Command perk.
DLL_LINKAGE bool hasDoubleCommand(const CGHeroInstance * hero);
/// True when the hero currently has the active Basic Command Battle Plan perk.
DLL_LINKAGE bool hasBattlePlan(const CGHeroInstance * hero);
/// True when the hero currently has the active Advanced Command perk.
DLL_LINKAGE bool hasCombinedArms(const CGHeroInstance * hero);
/// Half of Focus Fire's snapshotted Order bonus, preserving a half percentage point.
DLL_LINKAGE double combinedArmsFocusFirePercent(int rangedDamagePercent, const CGHeroInstance & hero);
/// Half of only Flank's Attack-derived component. The returned fractional percentage is
/// applied by the damage calculator so normal damage rounding remains the final rounding step.
DLL_LINKAGE double combinedArmsFlankPercent(const JsonNode & meleeDamageFormula,
	const CGHeroInstance & hero, int warcastingBonusPercent = 0);
DLL_LINKAGE double combinedArmsFlankPercent(const JsonNode & meleeDamageFormula,
	const CGHeroInstance & hero, int warcastingBonusPercent, int divineMandateEfficiencyBonusPercent);
DLL_LINKAGE std::vector<Bonus> bonuses(const JsonNode & rules, HeroCommand command, const CGHeroInstance & hero);
}
