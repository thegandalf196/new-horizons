/*
 * BattleChanges.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../json/JsonNode.h"
#include "../battle/CUnitState.h"

class BattleChanges
{
public:
	enum class EOperation : si8
	{
		ADD,
		UPDATE,
		REMOVE,
	};

	JsonNode data;
	EOperation operation = EOperation::UPDATE;

	BattleChanges() = default;
	explicit BattleChanges(EOperation operation_)
		: operation(operation_)
	{
	}
};

class UnitChanges : public BattleChanges
{
public:
	uint32_t id = 0;
	int64_t healthDelta = 0;

	UnitChanges() = default;
	UnitChanges(uint32_t id_, EOperation operation_)
		: BattleChanges(operation_)
		, id(id_)
	{
	}

	bool hasNoQuarterMoraleState() const
	{
		const auto & remaining = data["state"]["noQuarterMoraleActivationsRemaining"];
		return remaining.isNumber() && remaining.Integer() > 0;
	}

	bool hasRageThroughPainState() const
	{
		const auto & increment = data["state"]["personalBloodrageIncrement"];
		return increment.isNumber() && increment.Float() != 0.0;
	}

	bool hasBattlecraftPreemptiveStrikeRoundState() const
	{
		const auto & round = data["state"]["battlecraftPreemptiveStrikeRound"];
		return round.isNumber() && round.Integer() >= 0;
	}

	bool hasCasualtyProvenanceState() const
	{
		return battle::hasCasualtyProvenanceState(data);
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_CASUALTY_PROVENANCE)
			&& hasCasualtyProvenanceState())
			throw std::runtime_error("Cannot discard casualty provenance in an older unit update format");
		const auto & veteranDamage = data["state"]["veteranPhysicalDamageSinceActivation"];
		const auto & activationMovementBonus = data["state"]["activationMovementBonus"];
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_ARMORER_VETERAN)
			&& veteranDamage.isNumber() && veteranDamage.Integer() != 0)
			throw std::runtime_error("Cannot discard Veteran damage history in an older unit update format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RESERVE)
			&& activationMovementBonus.isNumber() && activationMovementBonus.Integer() != 0)
			throw std::runtime_error("Cannot discard Battlecraft Reserve movement state in an older unit update format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_RAGE_THROUGH_PAIN)
			&& hasRageThroughPainState())
			throw std::runtime_error("Cannot discard personal Bloodrage state in an older unit update format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLECRAFT_PREEMPTIVE_STRIKE)
			&& hasBattlecraftPreemptiveStrikeRoundState())
			throw std::runtime_error("Cannot discard Battlecraft Pre-emptive Strike round state in an older unit update format");
		h & id;
		h & healthDelta;
		h & data;
		h & operation;
	}
};

class ObstacleChanges : public BattleChanges
{
public:
	uint32_t id = 0;

	ObstacleChanges() = default;

	ObstacleChanges(uint32_t id_, EOperation operation_)
		: BattleChanges(operation_),
		id(id_)
	{
	}

	template <typename Handler> void serialize(Handler & h)
	{
		h & id;
		h & data;
		h & operation;
	}
};
