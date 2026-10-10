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
#include "../entities/creature/NewHorizonsRecruitmentTraining.h"

#include "../json/JsonNode.h"
#include "../battle/CUnitState.h"
#include "../battle/NewHorizonsPlague.h"
#include <limits>

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

	bool hasConfusionState() const
	{
		return battle::hasConfusionState(data);
	}

	template <typename Handler> void validateMoraleActivationSerialization(Handler & h) const
	{
		if(battle::hasMoraleActivationState(data)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_MORALE_EXTRA_DAMAGE))
			throw std::runtime_error("Cannot discard current Morale activation provenance");
	}

	template <typename Handler> void validateOpportunistSerialization(Handler & h) const
	{
		const auto & marker = data["state"]["luckyOwnAttackSequence"];
		if(!marker.isNull() && !marker.isBool())
			throw std::runtime_error("Own lucky attack sequence must be a boolean");
		if(marker.isBool() && marker.Bool() && !h.hasFeature(Handler::Version::NEW_HORIZONS_OPPORTUNIST))
			throw std::runtime_error("Cannot discard an earned own lucky attack sequence");
	}

	template <typename Handler> void validateConfusionSerialization(Handler & h) const
	{
		battle::confusionStateFromUnitJson(data).validateSerialization(h);
	}

	template <typename Handler> void validateVeteranCohesionSerialization(Handler & h) const
	{
		if(battle::hasVeteranCohesionState(data) && !h.hasFeature(Handler::Version::NEW_HORIZONS_VETERAN_COHESION))
			throw std::runtime_error("Cannot discard Veteran Cohesion receipt in an older unit update format");
	}

	template <typename Handler> void validateOverwatchSerialization(Handler & h) const
	{
		if(battle::hasHeroicSpiritState(data) && !h.hasFeature(Handler::Version::NEW_HORIZONS_HEROIC_SPIRIT))
			throw std::runtime_error("Cannot discard Heroic Spirit receipt in an older unit update");
		if(battle::hasOverwatchState(data) && !h.hasFeature(Handler::Version::NEW_HORIZONS_OVERWATCH))
			throw std::runtime_error("Cannot discard Overwatch state in an older unit update format");
	}

	template <typename Handler> void validatePlagueSerialization(Handler & h) const
	{
		if(newHorizonsPlague::containsExtendedPropagation(data)
			&& !h.hasFeature(Handler::Version::NEW_HORIZONS_PLAGUEBEARER))
			throw std::runtime_error("Cannot discard captured Plague unit propagation limit");
	}
	template <typename Handler> void validateTrainingSerialization(Handler & h) const
	{
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_DIPLOMACY_COHORTS)
			&& newHorizonsTraining::containsMercenaryBonus(data))
			throw std::runtime_error("Cannot discard Mercenary Captain unit bonus");
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_RECRUITMENT_TRAINING)
			&& newHorizonsTraining::containsTrainingBonus(data))
			throw std::runtime_error("Cannot discard training bonus in an older unit update");
	}

	template <typename Handler> void validateFrozenSerialization(Handler & h) const
	{
		const auto & state = data["state"];
		if(!state.isStruct() || state.Struct().count("frozenAppliedRound") == 0)
			return;
		const auto & round = state["frozenAppliedRound"];
		if(round.getType() != JsonNode::JsonType::DATA_INTEGER || round.Integer() < -1
			|| round.Integer() > std::numeric_limits<int32_t>::max())
			throw std::runtime_error("Invalid Frozen application receipt in unit update");
		if(round.Integer() != -1 && !h.hasFeature(Handler::Version::NEW_HORIZONS_FROZEN))
			throw std::runtime_error("Cannot discard Frozen application receipt in an older unit update format");
	}

	template <typename Handler> void validateBattleFormSerialization(Handler & h) const
	{
		const auto & state = data["state"];
		const auto activeCreature = [](const JsonNode & creature)
		{
			return (creature.isString() && !creature.String().empty())
				|| (creature.isNumber() && creature.Integer() >= 0);
		};
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_SPELLCRAFT_TARGET_DURATION)
			&& state["phantomRoundsRemaining"].isNumber() && state["phantomRoundsRemaining"].Integer() > 3)
			throw std::runtime_error("Cannot discard extended Phantom lifetime in an older unit update");
		const auto & rounds = state["battleFormRoundsRemaining"];
		const auto & pending = state["battleFormRestorationPending"];
		if(!h.hasFeature(Handler::Version::NEW_HORIZONS_SAFE_BATTLE_FORMS)
			&& (activeCreature(state["battleFormCreature"])
				|| activeCreature(state["battleFormOriginalCreature"])
				|| (rounds.isNumber() && rounds.Integer() != 0)
				|| (pending.getType() == JsonNode::JsonType::DATA_BOOL && pending.Bool())))
			throw std::runtime_error("Cannot discard battle-form state in an older unit update format");
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

	bool hasRebirthOriginalAggregateHP() const
	{
		const auto & originalHP = data["rebirthOriginalAggregateHP"];
		return originalHP.getType() == JsonNode::JsonType::DATA_INTEGER && originalHP.Integer() != 0;
	}

	bool hasBattlecraftMasteryState() const
	{
		return data["state"]["battlecraftWaitMasteryDoubled"].Bool()
			|| data["state"]["battlecraftDefendMasteryDoubled"].Bool();
	}

	bool hasArmorerLastStandUnitTransientState() const
	{
		return data["state"]["armorerLastStandEndedActivation"].Bool()
			|| data["state"]["armorerLastStandDefending"].Bool();
	}

	template <typename Handler> void serialize(Handler & h)
	{
		if(h.saving) validateMoraleActivationSerialization(h);
		if(h.saving)
			validateVeteranCohesionSerialization(h);
		if(h.saving)
			validateTrainingSerialization(h);
		if(h.saving) validatePlagueSerialization(h);
		if(h.saving)
			validateFrozenSerialization(h);
		if(h.saving)
			validateOverwatchSerialization(h);
		if(h.saving)
			validateBattleFormSerialization(h);
		if(h.saving)
			validateConfusionSerialization(h);
		if(h.saving)
			validateOpportunistSerialization(h);
		if(h.saving && !h.hasFeature(Handler::Version::BATTLE_CASUALTY_PROVENANCE)
			&& hasCasualtyProvenanceState())
			throw std::runtime_error("Cannot discard casualty provenance in an older unit update format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_REBIRTH_OUTPUT_ORIGINAL_HP)
			&& hasRebirthOriginalAggregateHP())
			throw std::runtime_error("Cannot discard Rebirth output original HP in an older unit update format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_BATTLEFIELD_MASTERY)
			&& hasBattlecraftMasteryState())
			throw std::runtime_error("Cannot discard Battlefield Mastery unit state in an older format");
		if(h.saving && !h.hasFeature(Handler::Version::NEW_HORIZONS_ARMORER_LAST_STAND)
			&& hasArmorerLastStandUnitTransientState())
			throw std::runtime_error("Cannot discard Last Stand activation-end state in an older unit update format");
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
		if(!h.saving) validateMoraleActivationSerialization(h);
		if(!h.saving) validatePlagueSerialization(h);
		h & operation;
		if(!h.saving)
			validateVeteranCohesionSerialization(h);
		if(!h.saving)
			validateOverwatchSerialization(h);
		if(!h.saving)
			validateFrozenSerialization(h);
		if(!h.saving)
			validateBattleFormSerialization(h);
		if(!h.saving)
			validateConfusionSerialization(h);
		if(!h.saving)
			validateOpportunistSerialization(h);
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
