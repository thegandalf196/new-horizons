/*
 * NewHorizonsQueueActivationStatus.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

#include "../../lib/battle/BattleUnitTurnReason.h"

namespace newHorizonsQueueActivationStatus
{
enum class Origin : uint8_t
{
	NONE,
	NORMAL,
	MORALE,
	QUARTERMASTER,
	SECOND_WIND
};

struct Status
{
	static constexpr uint32_t INVALID_UNIT_ID = std::numeric_limits<uint32_t>::max();
	static constexpr int32_t INVALID_ROUND = -2;

	uint32_t activeUnitId = INVALID_UNIT_ID;
	int32_t round = INVALID_ROUND;
	Origin origin = Origin::NONE;

	bool operator==(const Status &) const = default;
};

inline Origin classify(BattleUnitTurnReason reason, bool secondWindRecipient)
{
	switch(reason)
	{
		case BattleUnitTurnReason::MORALE:
			return Origin::MORALE;
		case BattleUnitTurnReason::REDUCED_EXTRA_ACTIVATION:
			return Origin::QUARTERMASTER;
		case BattleUnitTurnReason::HERO_COMMAND:
			return secondWindRecipient ? Origin::SECOND_WIND : Origin::NORMAL;
		default:
			return Origin::NORMAL;
	}
}

inline bool isSameActivationContinuation(BattleUnitTurnReason reason)
{
	switch(reason)
	{
		case BattleUnitTurnReason::HERO_SPELLCAST:
		case BattleUnitTurnReason::UNIT_SPELLCAST:
		case BattleUnitTurnReason::HERO_COMMAND:
		case BattleUnitTurnReason::ACTION_REJECTED:
		case BattleUnitTurnReason::MASTER_GATE_CONTINUATION:
		case BattleUnitTurnReason::PURSUIT_CONTINUATION:
		case BattleUnitTurnReason::RANGED_ATTACK_CONTINUATION:
			return true;
		default:
			return false;
	}
}

inline void update(Status & status, uint32_t unitId, int32_t round,
	BattleUnitTurnReason reason, bool secondWindRecipient)
{
	const bool sameActivation = status.activeUnitId == unitId && status.round == round;
	const auto classified = classify(reason, secondWindRecipient);
	if(!sameActivation)
	{
		status = {unitId, round, classified};
		return;
	}

	if(classified != Origin::NORMAL)
	{
		status.origin = classified;
		return;
	}

	if(!isSameActivationContinuation(reason))
		status.origin = Origin::NORMAL;
}

inline bool marksCurrentEntry(const Status & status, uint32_t unitId, size_t turnIndex)
{
	return turnIndex == 0 && status.activeUnitId == unitId
		&& status.origin != Origin::NONE && status.origin != Origin::NORMAL;
}
}
