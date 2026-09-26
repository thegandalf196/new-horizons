/*
 * NewHorizonsBattleStatus.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/battle/BattleSide.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/spells/NewHorizonsSorcery.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace newHorizonsBattleStatus
{
/// Time Stop currently reuses the registered SPELLINT placeholder frame. Keep
/// the status wording here so each battle stack presentation says the same
/// thing without adding a second runtime state API to the client.
inline constexpr std::string_view TIME_STOP_SPELL_KEY = "new-horizons:timeStop";

inline bool isTimeStop(std::string_view spellKey)
{
	return spellKey == TIME_STOP_SPELL_KEY;
}

inline std::string timeStopTooltip(std::string_view spellDescription)
{
	std::string result = "TIME STOP - STASIS\n";
	result += spellDescription;
	result += "\n\nRemaining: until the beginning of the caster's next Hero Action.";
	return result;
}

/// A short badge fits inside the 48x36 SPELLINT slot while the hover text
/// carries the full remaining-until-caster-action semantics.
inline constexpr std::string_view TIME_STOP_BADGE = "ST";

inline bool isFocusMagic(std::string_view spellKey)
{
	return spellKey == newHorizonsSorcery::FOCUS_MAGIC_SPELL;
}

inline bool isArcaneBreach(std::string_view spellKey)
{
	return spellKey == newHorizonsSorcery::ARCANE_BREACH_EFFECT;
}

inline bool isStatusTrigger(const Bonus & bonus, std::string_view spellKey, std::string_view triggerKey)
{
	if(bonus.type != BonusType::COMBAT_EVENT_TRIGGER || bonus.source != BonusSource::SPELL_EFFECT || !bonus.parameters)
		return false;

	try
	{
		return bonus.sid.toString() == spellKey && bonus.subtype.toString() == triggerKey;
	}
	catch(const std::exception &)
	{
		return false;
	}
}

inline std::optional<int32_t> beneficiarySide(const Bonus & bonus)
{
	if(!bonus.parameters)
		return std::nullopt;

	try
	{
		const auto & parameters = bonus.parameters->toCustom<JsonNode>();
		if(!parameters.isStruct())
			return std::nullopt;

		const auto side = parameters.Struct().find("beneficiarySide");
		if(side == parameters.Struct().end() || !side->second.isNumber())
			return std::nullopt;

		const auto value = side->second.Float();
		if(value != static_cast<int32_t>(BattleSide::ATTACKER) && value != static_cast<int32_t>(BattleSide::DEFENDER))
			return std::nullopt;

		return static_cast<int32_t>(value);
	}
	catch(const std::exception &)
	{
		return std::nullopt;
	}
}

inline std::string formatBasisPoints(int64_t basisPoints)
{
	const bool negative = basisPoints < 0;
	const auto absolute = negative ? -basisPoints : basisPoints;
	const auto wholePercent = absolute / 100;
	const auto fractionalPercent = absolute % 100;

	std::string result = (negative ? "-" : "") + std::to_string(wholePercent);
	if(fractionalPercent != 0)
	{
		std::string fraction = std::to_string(fractionalPercent + 100).substr(1);
		if(fraction.back() == '0')
			fraction.pop_back();
		result += "." + fraction;
	}
	return result + "%";
}

inline std::string roundsRemaining(int rounds)
{
	return std::to_string(rounds) + (rounds == 1 ? " round remaining" : " rounds remaining");
}

inline std::string beneficiarySideName(int32_t side)
{
	if(side == static_cast<int32_t>(BattleSide::ATTACKER))
		return "attacking side";
	if(side == static_cast<int32_t>(BattleSide::DEFENDER))
		return "defending side";
	return "unknown side";
}

struct FocusMagicStatus
{
	int32_t penetrationBasisPoints = 0;
	int32_t beneficiarySide = static_cast<int32_t>(BattleSide::NONE);
	int32_t remainingRounds = 0;
};

template<typename BonusRange>
inline std::optional<FocusMagicStatus> focusMagicStatus(const BonusRange & bonuses)
{
	for(const auto & bonus : bonuses)
	{
		if(!isStatusTrigger(*bonus, newHorizonsSorcery::FOCUS_MAGIC_SPELL,
			newHorizonsSorcery::FOCUS_MAGIC_TRIGGER)
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0 || bonus->val <= 0)
			continue;

		const auto side = beneficiarySide(*bonus);
		if(!side)
			continue;

		return FocusMagicStatus{bonus->val, *side, bonus->turnsRemain};
	}
	return std::nullopt;
}

inline std::string focusMagicTooltip(std::string_view spellDescription, const FocusMagicStatus & status)
{
	std::string result(spellDescription);
	result += "\n\nCaptured penetration per mark: " + formatBasisPoints(status.penetrationBasisPoints) + ".";
	result += "\nCaptured beneficiary: " + beneficiarySideName(status.beneficiarySide) + ".";
	result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	return result;
}

struct ArcaneBreachMarkGroup
{
	int32_t beneficiarySide = static_cast<int32_t>(BattleSide::NONE);
	int32_t remainingRounds = 0;
	int32_t markCount = 0;
	int64_t totalPenetrationBasisPoints = 0;
};

struct ArcaneBreachStatus
{
	std::vector<ArcaneBreachMarkGroup> groups;

	int32_t markCount() const
	{
		int32_t result = 0;
		for(const auto & group : groups)
			result += group.markCount;
		return result;
	}
};

template<typename BonusRange>
inline ArcaneBreachStatus arcaneBreachStatus(const BonusRange & bonuses)
{
	using GroupKey = std::pair<int32_t, int32_t>;
	std::map<GroupKey, ArcaneBreachMarkGroup> groups;
	std::map<int32_t, int32_t> acceptedMarksBySide;

	for(const auto & bonus : bonuses)
	{
		if(!isStatusTrigger(*bonus, newHorizonsSorcery::ARCANE_BREACH_EFFECT,
			newHorizonsSorcery::ARCANE_BREACH_TRIGGER)
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0 || bonus->val <= 0)
			continue;

		const auto side = beneficiarySide(*bonus);
		if(!side || acceptedMarksBySide[*side] >= newHorizonsSorcery::ARCANE_BREACH_MAX_MARKS)
			continue;

		++acceptedMarksBySide[*side];
		const auto key = GroupKey(*side, bonus->turnsRemain);
		auto & group = groups[key];
		group.beneficiarySide = *side;
		group.remainingRounds = bonus->turnsRemain;
		++group.markCount;
		group.totalPenetrationBasisPoints += std::min(bonus->val,
			newHorizonsSorcery::ARCANE_BREACH_CAP_BASIS_POINTS);
	}

	ArcaneBreachStatus result;
	for(const auto & entry : groups)
		result.groups.push_back(entry.second);
	return result;
}

inline std::string arcaneBreachTooltip(const ArcaneBreachStatus & status)
{
	std::string result = "Arcane Breach";
	if(status.groups.empty())
		return result + "\nNo active marks with a valid beneficiary side.";

	for(const auto & group : status.groups)
	{
		result += "\n\nCaptured beneficiary: " + beneficiarySideName(group.beneficiarySide) + ".";
		result += "\nMarks: " + std::to_string(group.markCount) + ".";
		result += "\nRemaining: " + roundsRemaining(group.remainingRounds) + ".";
		result += "\nThis group's total penetration: " + formatBasisPoints(group.totalPenetrationBasisPoints) + ".";
	}
	result += "\n\nOnly subsequent friendly ranged creature attacks from the named beneficiary side benefit. "
		"The target's actual Creature Defense is unchanged; melee attacks do not benefit.";
	return result;
}
}
