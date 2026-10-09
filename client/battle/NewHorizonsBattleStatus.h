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
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsConfusionControl.h"
#include "../../lib/battle/NewHorizonsConfusionState.h"
#include "../../lib/battle/SylvanLuckState.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "StackInfoStatusPresentation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace newHorizonsFlankReadback
{
/// Side-mask bits follow BattleHex::EDir order, viewed from the marked target.
inline constexpr std::array<std::string_view, 6> DIRECTION_NAMES = {
	"top-left", "top-right", "right", "bottom-right", "bottom-left", "left"
};

inline std::vector<std::string_view> directionNames(uint8_t sideMask)
{
	std::vector<std::string_view> result;
	for(std::size_t index = 0; index < DIRECTION_NAMES.size(); ++index)
		if((sideMask & static_cast<uint8_t>(1u << index)) != 0)
			result.push_back(DIRECTION_NAMES[index]);
	return result;
}

inline std::string formatDirections(uint8_t sideMask)
{
	const auto names = directionNames(sideMask);
	if(names.empty())
		return "none yet";

	std::string result;
	for(const auto name : names)
	{
		if(!result.empty())
			result += ", ";
		result += name;
	}
	return result;
}
}

namespace newHorizonsBattleStatus
{
/// Time Stop currently reuses the registered SPELLINT placeholder frame. Keep
/// the status wording here so each battle stack presentation says the same
/// thing without adding a second runtime state API to the client.
inline constexpr std::string_view TIME_STOP_SPELL_KEY = "new-horizons:timeStop";
inline constexpr std::string_view SPELL_LOCK_SPELL_KEY = "new-horizons:spellLock";
inline constexpr std::string_view ENTANGLE_SPELL_KEY = "new-horizons:entangle";
inline constexpr std::string_view REGENERATION_SPELL_KEY = newHorizonsMagic::NATURE_REGENERATION_SPELL;
inline constexpr std::string_view SANCTUARY_SPELL_KEY = "new-horizons:sanctuary";
inline constexpr std::string_view GUARDIAN_SPIRIT_SPELL_KEY = "new-horizons:guardianSpirit";
inline constexpr std::string_view HEAVENLY_GALE_SPELL_KEY = "new-horizons:heavenlyGale";
inline constexpr std::string_view CRUSADE_SPELL_KEY = "new-horizons:crusade";
inline constexpr std::string_view SHADOW_GIFT_SPELL_KEY = "new-horizons:shadowGift";
inline constexpr std::string_view SHADOW_GIFT_TRIGGER_KEY = "core:shadowGift";
inline constexpr std::string_view VAMPIRISM_SPELL_KEY = "new-horizons:vampirism";
inline constexpr std::string_view VAMPIRISM_TRIGGER_KEY = "core:vampirism";
inline constexpr std::string_view DOOM_SPELL_KEY = "new-horizons:doom";
inline constexpr std::string_view REANIMATE_SPELL_KEY = "new-horizons:reanimate";
inline constexpr std::string_view DIVINE_RETRIBUTION_SPELL_KEY = "new-horizons:divineRetribution";

inline std::string formatBasisPoints(int64_t basisPoints);
inline std::string roundsRemaining(int rounds);

inline bool isRegeneration(std::string_view spellKey)
{
	return spellKey == REGENERATION_SPELL_KEY;
}

inline bool isShadowGift(std::string_view spellKey)
{
	return spellKey == SHADOW_GIFT_SPELL_KEY;
}

inline bool isVampirism(std::string_view spellKey)
{
	return spellKey == VAMPIRISM_SPELL_KEY;
}

inline bool isDoom(std::string_view spellKey)
{
	return spellKey == DOOM_SPELL_KEY;
}

inline bool isReanimate(std::string_view spellKey)
{
	return spellKey == REANIMATE_SPELL_KEY;
}

inline bool isDivineRetribution(std::string_view spellKey)
{
	return spellKey == DIVINE_RETRIBUTION_SPELL_KEY;
}

inline bool isTimeStop(std::string_view spellKey)
{
	return spellKey == TIME_STOP_SPELL_KEY;
}

inline constexpr std::string_view CONFUSION_SPELL_KEY = "new-horizons:confusion";

inline bool isConfusion(std::string_view spellKey)
{
	return spellKey == CONFUSION_SPELL_KEY;
}

inline constexpr std::string_view CONFUSION_BADGE = "NEXT";

struct ConfusionStatus
{
	bool pending = false;
	bool confounder = false;
	PlayerColor caster = PlayerColor::CANNOT_DETERMINE;
	battle::ConfusionBehavior previousResolved = battle::ConfusionBehavior::NONE;
	bool active() const
	{
		return pending;
	}
	bool operator==(const ConfusionStatus &) const = default;
};

template <typename BonusListLike>
inline ConfusionStatus confusionStatus(const BonusListLike & bonuses, const battle::ConfusionState & state)
{
	ConfusionStatus result;
	try
	{
		state.validate();
	}
	catch(const std::runtime_error &)
	{
		return result;
	}
	result.previousResolved = state.previousResolved;
	if(!state.pending)
		return result;
	const Bonus * selected = nullptr;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::CONFUSION_PENDING)
			continue;
		if(selected || !newHorizonsConfusionControl::isPendingMarker(bonus.get()))
			return result;
		selected = bonus.get();
	}
	if(!selected || selected->spellCasterOwner != state.pendingCaster
		|| (selected->val == 2) != state.pendingConfounder)
		return result;
	const SpellID confusion(SpellID::decode(std::string(CONFUSION_SPELL_KEY)));
	if(!confusion.hasValue() || selected->sid != BonusSourceID(confusion))
		return result;
	result.pending = true;
	result.confounder = state.pendingConfounder;
	result.caster = state.pendingCaster;
	return result;
}

inline std::string confusionTooltip(std::string_view spellDescription, const ConfusionStatus & status)
{
	if(!status.active())
		return {};
	std::string result = "Confusion - pending next activation\n";
	result += spellDescription;
	result += "\n\nThe next Creature Activation is forced: Attack, Defend, or Wander, with equal initial chances."
		" Dispel removes the pending effect. Negative Morale forfeiture consumes it without recording a behavior.";
	if(status.confounder)
	{
		result += "\nCaptured Confounder: the previous resolved behavior cannot repeat when a different legal behavior is available."
			" The sole legal behavior may repeat.";
	}
	if(status.previousResolved != battle::ConfusionBehavior::NONE)
	{
		const auto previous = status.previousResolved == battle::ConfusionBehavior::ATTACK ? "Attack"
			: status.previousResolved == battle::ConfusionBehavior::DEFEND ? "Defend" : "Wander";
		result += std::string("\nPrevious resolved behavior: ") + previous
			+ ". This is history, not an additional active effect.";
	}
	return result;
}

inline std::string timeStopTooltip(std::string_view spellDescription)
{
	std::string result = "TIME STOP - STASIS\n";
	result += spellDescription;
	result += "\n\nRemaining: until the beginning of the caster's next Hero Action.";
	return result;
}

inline bool isSpellLock(std::string_view spellKey)
{
	return spellKey == SPELL_LOCK_SPELL_KEY;
}

inline bool isEntangle(std::string_view spellKey)
{
	return spellKey == ENTANGLE_SPELL_KEY;
}

template<typename BonusRange>
inline EntangleStatus entangleStatus(const BonusRange & bonuses)
{
	EntangleStatus result;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::BIND_EFFECT || bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0 || bonus->parameters)
			continue;

		try
		{
			if(bonus->sid.toString() != ENTANGLE_SPELL_KEY)
				continue;
		}
		catch(const std::exception &)
		{
			continue;
		}

		result.remainingRounds = std::max(result.remainingRounds, static_cast<int32_t>(bonus->turnsRemain));
	}
	return result;
}

inline std::string entangleTooltip(std::string_view spellDescription, const EntangleStatus & status)
{
	std::string result(spellDescription);
	if(!status.active())
		return result;

	result += "\n\nEntangle - Rooted. Voluntary movement is unavailable, but Initiative and activation timing are unchanged.";
	result += "\nUnlike Time Stop, the stack is not placed in stasis and can still take its normal activation.";
	result += "\nIt may attack adjacent enemies, retaliate, shoot, Wait, Defend, and use abilities that do not require movement.";
	result += "\nForced displacement or teleportation removes the roots.";
	result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	return result;
}

inline bool isSanctuary(std::string_view spellKey)
{
	return spellKey == SANCTUARY_SPELL_KEY;
}

inline bool isGuardianSpirit(std::string_view spellKey)
{
	return spellKey == GUARDIAN_SPIRIT_SPELL_KEY;
}

inline bool isHeavenlyGale(std::string_view spellKey)
{
	return spellKey == HEAVENLY_GALE_SPELL_KEY;
}

inline bool isCrusade(std::string_view spellKey)
{
	return spellKey == CRUSADE_SPELL_KEY;
}

struct HeavenlyGaleStatus
{
	int32_t reductionBasisPoints = 0;
	int32_t remainingRounds = 0;

	bool active() const { return reductionBasisPoints > 0 && remainingRounds > 0; }
};

template<typename BonusRange>
inline HeavenlyGaleStatus heavenlyGaleStatus(const BonusRange & bonuses)
{
	HeavenlyGaleStatus result;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::HEAVENLY_GALE
			|| bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0)
			continue;
		result.reductionBasisPoints = std::max(result.reductionBasisPoints, bonus->val);
		result.remainingRounds = std::max(result.remainingRounds, static_cast<int32_t>(bonus->turnsRemain));
	}
	return result;
}

inline std::string heavenlyGaleTooltip(std::string_view spellDescription, const HeavenlyGaleStatus & status)
{
	std::string result(spellDescription);
	if(!status.active())
		return result;
	result += "\n\nPhysical ranged projectile damage reduction: ";
	result += formatBasisPoints(status.reductionBasisPoints);
	result += ". Remaining: " + std::to_string(status.remainingRounds) + " rounds.";
	result += "\nIncludes physical siege shots; excludes melee, spells, magical beams, and explosions.";
	return result;
}

struct CrusadeStatus
{
	int32_t attackBonus = 0;
	int32_t defenseBonus = 0;
	int32_t initiativeBonus = 0;
	int32_t magicalDamageReductionBasisPoints = 0;
	int32_t remainingRounds = 0;
	bool protectsMoraleFromNegative = false;

	bool active() const
	{
		return remainingRounds > 0 && (attackBonus != 0 || defenseBonus != 0 || initiativeBonus != 0
			|| magicalDamageReductionBasisPoints != 0 || protectsMoraleFromNegative);
	}
};

template<typename BonusRange>
inline CrusadeStatus crusadeStatus(const BonusRange & bonuses)
{
	CrusadeStatus result;
	bool hasTimedCrusadeBonus = false;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0)
			continue;

		try
		{
			if(bonus->sid.toString() != CRUSADE_SPELL_KEY)
				continue;
		}
		catch(const std::exception &)
		{
			continue;
		}

		switch(bonus->type)
		{
			case BonusType::PRIMARY_SKILL:
				if(bonus->subtype == BonusSubtypeID(PrimarySkill::ATTACK))
					result.attackBonus += bonus->val;
				else if(bonus->subtype == BonusSubtypeID(PrimarySkill::DEFENSE))
					result.defenseBonus += bonus->val;
				else
					continue;
				break;
			case BonusType::STACKS_INITIATIVE_FLAT:
				result.initiativeBonus += bonus->val;
				break;
			case BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS:
				if(bonus->subtype != BonusSubtypeID(SpellSchool::ANY))
					continue;
				result.magicalDamageReductionBasisPoints += bonus->val;
				break;
			case BonusType::MINIMUM_MORALE:
				if(bonus->val != 0)
					continue;
				result.protectsMoraleFromNegative = true;
				break;
			default:
				continue;
		}

		if(!hasTimedCrusadeBonus)
		{
			result.remainingRounds = bonus->turnsRemain;
			hasTimedCrusadeBonus = true;
		}
		else
			result.remainingRounds = std::min(result.remainingRounds, static_cast<int32_t>(bonus->turnsRemain));
	}
	return result;
}

inline std::string crusadeTooltip(std::string_view spellDescription, const CrusadeStatus & status)
{
	std::string result(spellDescription);
	if(!status.active())
		return result;

	result += "\n\nCurrent Crusade bonuses:";
	result += "\nAttack: +" + std::to_string(status.attackBonus) + ".";
	result += "\nDefense: +" + std::to_string(status.defenseBonus) + ".";
	result += "\nInitiative: +" + std::to_string(status.initiativeBonus) + " flat points.";
	result += "\nCrusade's independent Magical Damage Reduction: ";
	result += formatBasisPoints(status.magicalDamageReductionBasisPoints) + ".";
	if(status.protectsMoraleFromNegative)
		result += "\nMorale cannot fall below 0 while Crusade is active.";
	result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	return result;
}

struct GuardianSpiritStatus
{
	int64_t remainingHitPoints = 0;
	int32_t remainingRounds = 0;

	bool active() const { return remainingHitPoints > 0 && remainingRounds > 0; }
	bool operator==(const GuardianSpiritStatus &) const = default;
};

inline std::string guardianSpiritTooltip(std::string_view spellDescription, const GuardianSpiritStatus & status)
{
	std::string result(spellDescription);
	result += "\n\nGuardian Spirit: " + std::to_string(status.remainingHitPoints)
		+ " protective HP; " + std::to_string(status.remainingRounds) + " rounds remaining.";
	result += "\nAbsorbs physical creature damage before creature HP. Spell damage bypasses it.";
	return result;
}

struct SpellLockStatus
{
	bool preservesBeneficial = true;
	int32_t remainingRounds = 0;
};

template<typename BonusRange>
inline std::optional<SpellLockStatus> spellLockStatus(const BonusRange & bonuses)
{
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->source != BonusSource::SPELL_EFFECT || bonus->type != BonusType::NONE
			|| bonus->sid.toString() != SPELL_LOCK_SPELL_KEY
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0)
			continue;

		return SpellLockStatus{bonus->val > 0, bonus->turnsRemain};
	}
	return std::nullopt;
}

inline std::string spellLockTooltip(std::string_view spellDescription, const SpellLockStatus & status)
{
	std::string result(spellDescription);
	result += "\n\nPreserved: ";
	result += status.preservesBeneficial ? "beneficial" : "hostile";
	result += " magical effects. Their timers are frozen, but the effects continue working.";
	result += "\nNo further magic can affect this stack; Orders are unaffected.";
	result += "\nRemaining: " + std::to_string(status.remainingRounds)
		+ (status.remainingRounds == 1 ? " round." : " rounds.");
	return result;
}

inline constexpr std::string_view SPELL_LOCK_BADGE = "SL";

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

inline std::optional<int64_t> divineRetributionIntegerParameter(const Bonus & bonus, std::string_view key)
{
	if(!bonus.parameters)
		return std::nullopt;

	try
	{
		const auto & parameters = bonus.parameters->toCustom<JsonNode>();
		if(!parameters.isStruct())
			return std::nullopt;

		const auto value = parameters.Struct().find(std::string(key));
		if(value == parameters.Struct().end() || !value->second.isNumber())
			return std::nullopt;

		return value->second.Integer();
	}
	catch(const std::exception &)
	{
		return std::nullopt;
	}
}

template<typename BonusRange>
inline DivineRetributionProtectionStatus divineRetributionProtectionStatus(const BonusRange & bonuses)
{
	DivineRetributionProtectionStatus result;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::DIVINE_RETRIBUTION
			|| bonus->source != BonusSource::SPELL_EFFECT || bonus->duration != BonusDuration::N_TURNS
			|| bonus->turnsRemain <= 0 || bonus->val <= 0)
			continue;

		try
		{
			if(bonus->sid.toString() != DIVINE_RETRIBUTION_SPELL_KEY)
				continue;
		}
		catch(const std::exception &)
		{
			continue;
		}

		const auto savedPercent = divineRetributionIntegerParameter(*bonus, "retributionistPercent");
		const auto retributionistPercent = savedPercent
			? static_cast<int32_t>(std::clamp<int64_t>(*savedPercent, 100, 120)) : 100;
		const DivineRetributionProtectionStatus candidate{
			bonus->val, bonus->turnsRemain, retributionistPercent};
		if(!result.active() || candidate.rawCap > result.rawCap
			|| (candidate.rawCap == result.rawCap && candidate.remainingRounds > result.remainingRounds))
			result = candidate;
	}
	return result;
}

template<typename BonusRange>
inline DivineRetributionJudgedStatus divineRetributionJudgedStatus(const BonusRange & bonuses)
{
	DivineRetributionJudgedStatus result;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::DIVINE_RETRIBUTION_JUDGED)
			continue;

		const auto round = divineRetributionIntegerParameter(*bonus, "round");
		const auto protectedUnitId = divineRetributionIntegerParameter(*bonus, "protectedUnitId");
		const auto actualHpDamage = divineRetributionIntegerParameter(*bonus, "actualHpDamage");
		const auto rawCap = divineRetributionIntegerParameter(*bonus, "rawCap");
		const auto retributionistPercent = divineRetributionIntegerParameter(*bonus, "retributionistPercent");
		if(!round || !protectedUnitId || !actualHpDamage || !rawCap || !retributionistPercent
			|| *round < 0 || *protectedUnitId < 0 || *actualHpDamage < 0 || *rawCap < 0)
			continue;

		const auto boundedCap = static_cast<int32_t>(std::clamp<int64_t>(*rawCap, 0,
			std::numeric_limits<int32_t>::max()));
		const auto boundedPercent = static_cast<int32_t>(std::clamp<int64_t>(*retributionistPercent, 100, 120));
		result.pendingHolyDamage += divineRetributionPendingHolyDamage(*actualHpDamage,
			boundedCap, boundedPercent);
		if(result.judgmentCount < std::numeric_limits<int32_t>::max())
			++result.judgmentCount;
	}
	return result;
}

struct ShadowGiftStatus
{
	int64_t maximumHealthLost = 0;
	int32_t damageBonusBasisPoints = 0;
	int32_t remainingRounds = 0;

	bool hasTimedBuff() const
	{
		return damageBonusBasisPoints > 0 && remainingRounds > 0;
	}

	bool hasMaximumHealthLoss() const
	{
		return maximumHealthLost > 0;
	}

	bool operator==(const ShadowGiftStatus &) const = default;
};

struct VampirismStatus
{
	int32_t lifestealBasisPoints = 0;
	int32_t remainingRounds = 0;

	bool active() const
	{
		return lifestealBasisPoints > 0 && remainingRounds > 0;
	}

	bool operator==(const VampirismStatus &) const = default;
};

struct DoomStatus
{
	int32_t damagePenaltyPercent = 0;
	int32_t moralePenalty = 0;
	int32_t remainingRounds = 0;

	bool active() const
	{
		return damagePenaltyPercent > 0 && remainingRounds > 0;
	}

	bool operator==(const DoomStatus &) const = default;
};

template<typename BonusRange>
inline DoomStatus doomStatus(const BonusRange & bonuses)
{
	DoomStatus result;
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->sid.toString() != DOOM_SPELL_KEY)
			continue;

		if(bonus->type == BonusType::GENERAL_ATTACK_REDUCTION
			&& bonus->duration == BonusDuration::N_TURNS && bonus->turnsRemain > 0 && bonus->val > 0)
		{
			result.damagePenaltyPercent = std::max(result.damagePenaltyPercent, bonus->val);
			result.remainingRounds = std::max(result.remainingRounds,
				static_cast<int32_t>(bonus->turnsRemain));
		}
		else if(bonus->type == BonusType::MORALE && bonus->val < 0)
			result.moralePenalty = std::min(result.moralePenalty, bonus->val);
	}
	return result;
}

inline std::string temporaryCreatureTooltip(int32_t remainingCount)
{
	const auto count = std::max<int32_t>(0, remainingCount);
	std::string result = "Temporary\n" + std::to_string(count)
		+ (count == 1 ? " temporary creature remains in this stack." : " temporary creatures remain in this stack.");
	result += "\nThey fight normally during this battle and disappear when the battle ends.";
	return result;
}

inline std::string formatBasisPoints(int64_t basisPoints);
inline std::string roundsRemaining(int rounds);

template<typename BonusRange>
inline ShadowGiftStatus shadowGiftStatus(const BonusRange & bonuses, int64_t maximumHealthLost)
{
	ShadowGiftStatus result;
	result.maximumHealthLost = std::max<int64_t>(0, maximumHealthLost);
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::COMBAT_EVENT_TRIGGER || bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0 || bonus->val <= 0)
			continue;
		try
		{
			if(bonus->sid.toString() != SHADOW_GIFT_SPELL_KEY || bonus->subtype.toString() != SHADOW_GIFT_TRIGGER_KEY)
				continue;
		}
		catch(const std::exception &)
		{
			continue;
		}

		result.damageBonusBasisPoints = bonus->val;
		result.remainingRounds = bonus->turnsRemain;
		break;
	}
	return result;
}

template<typename BonusRange>
inline VampirismStatus vampirismStatus(const BonusRange & bonuses)
{
	for(const auto & bonus : bonuses)
	{
		if(!bonus || bonus->type != BonusType::COMBAT_EVENT_TRIGGER || bonus->source != BonusSource::SPELL_EFFECT
			|| bonus->duration != BonusDuration::N_TURNS || bonus->turnsRemain <= 0 || bonus->val <= 0)
			continue;
		try
		{
			if(bonus->sid.toString() != VAMPIRISM_SPELL_KEY || bonus->subtype.toString() != VAMPIRISM_TRIGGER_KEY)
				continue;
		}
		catch(const std::exception &)
		{
			continue;
		}

		return {bonus->val, bonus->turnsRemain};
	}
	return {};
}

inline std::string shadowGiftBuffTooltip(const ShadowGiftStatus & status)
{
	return "Shadow Gift - Offensive enchantment\nShadow damage bonus: "
		+ formatBasisPoints(status.damageBonusBasisPoints)
		+ ".\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
}

inline std::string shadowGiftCapTooltip(const ShadowGiftStatus & status)
{
	return "Shadow Gift - Battle-long vitality sacrifice\nMaximum aggregate HP permanently lost: "
		+ std::to_string(status.maximumHealthLost)
		+ " HP. This cap loss remains after the three-round damage enchantment expires and cannot be dispelled.";
}

inline std::string vampirismTooltip(std::string_view spellDescription, const VampirismStatus & status)
{
	std::string result(spellDescription);
	result += "\n\nCurrent lifesteal: " + formatBasisPoints(status.lifestealBasisPoints)
		+ " of actual attack damage dealt, including retaliation.";
	result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	result += "\nHealing restores surviving creatures and cannot revive casualties.";
	return result;
}

inline std::string doomTooltip(std::string_view spellDescription, const DoomStatus & status)
{
	std::string result(spellDescription);
	result += "\n\nCurrent outgoing damage, including retaliation damage, is reduced by "
		+ std::to_string(status.damagePenaltyPercent) + "% per hit.";
	result += "\nInitiative and battlefield movement are also reduced by "
		+ std::to_string(status.damagePenaltyPercent) + "%.";
	result += "\nMorale penalty: " + std::to_string(status.moralePenalty) + ".";
	result += "\nCreature Defense is unchanged.";
	result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	return result;
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

inline std::string formatPercentagePoints(int64_t basisPoints)
{
	auto result = formatBasisPoints(basisPoints);
	result.pop_back();
	return result + " percentage points";
}

inline std::string roundsRemaining(int rounds)
{
	return std::to_string(rounds) + (rounds == 1 ? " round remaining" : " rounds remaining");
}

inline std::string divineRetributionProtectionTooltip(std::string_view spellDescription,
	const DivineRetributionProtectionStatus & status)
{
	std::string result(spellDescription);
	if(!status.active())
		return result;

	result += "\n\nProtection: this stack returns Holy damage equal to 30% of actual HP damage taken this round, capped at ";
	result += std::to_string(status.rawCap) + " HP.";
	if(status.retributionistPercent > 100)
		result += " Retributionist increases the capped amount by 20%.";
	result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	return result;
}

inline std::string divineRetributionJudgedTooltip(const DivineRetributionJudgedStatus & status)
{
	std::string result = "Divine Retribution - Judged\nPending Holy damage: ";
	result += std::to_string(status.pendingHolyDamage) + " HP.";
	result += "\n" + std::to_string(status.judgmentCount)
		+ (status.judgmentCount == 1 ? " saved damage record awaits resolution." : " saved damage records await resolution.");
	result += "\nPayout per record: floor(min(floor(actualHpDamage * 30 / 100), rawCap) * retributionistPercent / 100).";
	return result;
}

inline std::string regenerationTooltip(std::string_view spellDescription, const RegenerationStatus & status)
{
	std::string result(spellDescription);
	result += "\n\nCurrent regeneration rate: ";
	result += formatBasisPoints((static_cast<int64_t>(status.rateMillionths) + 50) / 100);
	result += " of eligible damage suffered while the effect is active.";
	result += "\nCurrently healable marked wounds at the next activation: ";
	result += std::to_string(status.healablePendingHealth);
	result += " HP.";
	result += "\nRegeneration restores wounds among surviving creatures; it does not revive casualties.";
	if(status.remainingRounds > 0)
		result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	return result;
}

struct BulwarkStatus
{
	int32_t damageReductionBasisPoints = 0;
	int32_t preemptiveDamagePercent = 0;
	int32_t meleeReflectionBasisPoints = 0;
	int32_t rangedReflectionBasisPoints = 0;
	bool mirebornTerrainBonus = false;
	bool bogAmbush = false;
	bool preemptiveReady = false;
	bool sharedCoverApplied = false;
	int32_t sharedCoverAdjustmentBasisPoints = 0;
	int32_t vengefulMireBonusBasisPoints = 0;

	bool operator==(const BulwarkStatus &) const = default;
};

struct BattlecraftWaitStatus
{
	int32_t damageBonusPercent = 0;

	bool active() const { return damageBonusPercent > 0; }
	bool operator==(const BattlecraftWaitStatus &) const = default;
};

inline std::optional<BattlecraftWaitStatus> makeBattlecraftWaitStatus(int32_t damageBonusPercent, bool armed)
{
	if(!armed || damageBonusPercent <= 0)
		return std::nullopt;

	return BattlecraftWaitStatus{damageBonusPercent};
}

inline std::string battlecraftWaitTooltip(const BattlecraftWaitStatus & status)
{
	if(!status.active())
		return {};

	return "Battlecraft - Wait\nThe next attack or retaliation before the end of this round deals +"
		+ std::to_string(status.damageBonusPercent) + "% physical damage.";
}

inline std::optional<BulwarkStatus> makeBulwarkStatus(int rank, int heroDefense, bool mirebornTerrain,
	bool bogAmbush, bool thickHide, bool preemptiveUsed, bool sharedCoverApplies = false,
	bool vengefulMire = false)
{
	if(rank < 1 || rank > 3)
		return std::nullopt;

	const int baseReduction = newHorizonsBulwark::reductionBasisPoints(rank, heroDefense, mirebornTerrain);
	const bool applySharedCover = sharedCoverApplies && baseReduction > 0;
	const int effectiveReduction = applySharedCover
		? std::min(10000, baseReduction + newHorizonsBulwark::sharedCoverBasisPoints(baseReduction))
		: baseReduction;
	const int baseMeleeReflection = newHorizonsBulwark::reflectionBasisPoints(rank, false, thickHide, false);
	const int effectiveMeleeReflection = newHorizonsBulwark::reflectionBasisPoints(rank, false, thickHide, vengefulMire);

	BulwarkStatus result;
	result.damageReductionBasisPoints = effectiveReduction;
	result.preemptiveDamagePercent = newHorizonsBulwark::preemptivePercent(rank, bogAmbush);
	result.meleeReflectionBasisPoints = effectiveMeleeReflection;
	result.rangedReflectionBasisPoints = newHorizonsBulwark::reflectionBasisPoints(rank, true, thickHide, vengefulMire);
	result.mirebornTerrainBonus = mirebornTerrain;
	result.bogAmbush = bogAmbush;
	result.preemptiveReady = !preemptiveUsed;
	result.sharedCoverApplied = applySharedCover;
	result.sharedCoverAdjustmentBasisPoints = effectiveReduction - baseReduction;
	result.vengefulMireBonusBasisPoints = effectiveMeleeReflection - baseMeleeReflection;
	return result;
}

struct DefendStatus
{
	bool defending = false;
	int32_t battlecraftReductionPercent = 0;
	std::optional<BulwarkStatus> bulwark;

	bool operator==(const DefendStatus &) const = default;
};

struct BattleFormStatus
{
	bool transformed = false;
	int32_t currentCreature = -1;
	int32_t originalCreature = -1;
	int32_t remainingRounds = 0;
	int64_t aggregateCreatureHealth = 0;
	std::string currentCreatureName;
	std::string originalCreatureName;

	bool active() const
	{
		return transformed && remainingRounds > 0;
	}

	bool operator==(const BattleFormStatus &) const = default;
};

inline BattleFormStatus makeBattleFormStatus(bool transformed, int32_t currentCreature,
	int32_t originalCreature, int32_t remainingRounds, int64_t aggregateCreatureHealth,
	std::string currentCreatureName, std::string originalCreatureName)
{
	if(!transformed || remainingRounds <= 0)
		return {};

	return {true, currentCreature, originalCreature, remainingRounds, aggregateCreatureHealth,
		std::move(currentCreatureName), std::move(originalCreatureName)};
}

inline std::string battleFormTooltip(const BattleFormStatus & status)
{
	if(!status.active())
		return {};

	std::string result = "Transformed\nCurrent form: " + status.currentCreatureName
		+ "; original creature: " + status.originalCreatureName + ".";
	result += "\nCurrent aggregate creature HP: " + std::to_string(status.aggregateCreatureHealth)
		+ " HP. Form changes preserve this exact surviving creature-HP total.";
	result += "\nTemporary HP is tracked separately. The stack's owner and battle-side allegiance are unchanged.";
	result += "\nRemaining: " + roundsRemaining(status.remainingRounds) + ".";
	return result;
}

struct BattleMoraleReadback
{
	bool available = false;
	int32_t real = 0;
	int32_t effective = 0;
	int32_t standardBearerBonus = 0;
	int32_t firstRoundModifier = 0;
	int32_t steadfastAdjustment = 0;
	int32_t espritDeCorpsAdjustment = 0;
	bool commandingPresenceFloorApplied = false;
	bool furyUnboundFloorApplied = false;
	bool unaffectedByMorale = false;
	std::vector<std::string> bonusDescriptions;

	bool active() const { return available; }
	bool hasSources() const
	{
		if(unaffectedByMorale)
			return false;

		return !bonusDescriptions.empty() || standardBearerBonus != 0 || firstRoundModifier != 0
			|| steadfastAdjustment != 0 || espritDeCorpsAdjustment != 0
			|| commandingPresenceFloorApplied || furyUnboundFloorApplied;
	}

	bool operator==(const BattleMoraleReadback &) const = default;
};

inline BattleMoraleReadback makeBattleMoraleReadback(bool enabled, int32_t real, int32_t effective,
	int32_t standardBearerBonus, int32_t firstRoundModifier, int32_t steadfastAdjustment,
	bool commandingPresenceFloorApplied, bool furyUnboundFloorApplied, bool unaffectedByMorale,
	std::vector<std::string> bonusDescriptions, int32_t espritDeCorpsAdjustment = 0)
{
	if(!enabled)
		return {};
	if(unaffectedByMorale)
		return {true, real, effective, 0, 0, 0, 0, false, false, true, {}};

	return {true, real, effective, standardBearerBonus, firstRoundModifier, steadfastAdjustment, espritDeCorpsAdjustment,
		commandingPresenceFloorApplied, furyUnboundFloorApplied, false, std::move(bonusDescriptions)};
}

/// Version key for the panel's stable, callback-scoped bonus descriptions.
/// Exact live Luck/Morale values are still read through their shared APIs.
struct BattleBonusDescriptionCacheKey
{
	uint32_t unitId = 0;
	int32_t bonusTreeVersion = 0;
	const void * scopedCallbackIdentity = nullptr;

	bool operator==(const BattleBonusDescriptionCacheKey &) const = default;
};

/// Read-only, player-scoped presentation of the value used by a normal
/// target-neutral attack. The exact value comes from the shared battle query;
/// the accompanying flags explain exceptional bonuses without reimplementing
/// its Luck curve or attack-specific conditions.
struct BattleLuckReadback
{
	bool available = false;
	int32_t ordinaryAttackLuck = 0;
	bool noLuck = false;
	bool maxLuck = false;
	bool maximumLuckLimitPresent = false;
	int32_t maximumLuckLimit = 0;
	std::vector<std::string> bonusDescriptions;

	bool active() const { return available; }
	bool hasSources() const { return !bonusDescriptions.empty(); }
	bool operator==(const BattleLuckReadback &) const = default;
};

inline BattleLuckReadback makeBattleLuckReadback(bool enabled, int32_t ordinaryAttackLuck,
	bool noLuck, bool maxLuck, bool maximumLuckLimitPresent, int32_t maximumLuckLimit,
	std::vector<std::string> bonusDescriptions)
{
	if(!enabled)
		return {};
	// These exceptional bonuses short-circuit the ordinary LUCK list in the
	// shared attack query. Keep their flags/value, but do not claim raw Luck
	// bonuses contribute to the returned number.
	if(noLuck || maxLuck)
		bonusDescriptions.clear();

	return {true, ordinaryAttackLuck, noLuck, maxLuck, maximumLuckLimitPresent,
		maximumLuckLimit, std::move(bonusDescriptions)};
}

struct SylvanLuckStackStatus
{
	bool skillPresent = false;
	int32_t effectiveCappedLuck = 0;
	int32_t sharedFortuneLuck = 0;
	int32_t cascadingFortuneLuck = 0;
	int32_t forestFavorSpeed = 0;
	bool serendipityChanceOnlyReady = false;
	bool forestFavorReady = false;
	bool fortunateAimConditional = false;
	bool gamblerAttackReady = false;
	bool chainFortuneReady = false;

	bool active() const
	{
		return sharedFortuneLuck != 0 || cascadingFortuneLuck != 0 || forestFavorSpeed != 0
			|| serendipityChanceOnlyReady || forestFavorReady || fortunateAimConditional
			|| gamblerAttackReady || chainFortuneReady;
	}

	bool operator==(const SylvanLuckStackStatus &) const = default;
};

inline SylvanLuckStackStatus makeSylvanLuckStackStatus(const SylvanLuckState & state, uint32_t unitId,
	int32_t effectiveCappedLuck, bool enabled, bool canReceiveFortunateAim)
{
	if(!enabled)
		return {};

	const bool positiveLuckTriggered = state.positiveLuckUnits.contains(unitId);
	SylvanLuckStackStatus result;
	result.skillPresent = true;
	result.effectiveCappedLuck = effectiveCappedLuck;
	result.sharedFortuneLuck = state.sharedUnits.contains(unitId) ? 1 : 0;
	result.cascadingFortuneLuck = state.cascadingUnits.contains(unitId) ? 3 : 0;
	result.forestFavorSpeed = state.speedBonus(unitId);
	result.serendipityChanceOnlyReady = state.serendipity && !positiveLuckTriggered;
	result.forestFavorReady = state.forestsFavor && !positiveLuckTriggered;
	result.fortunateAimConditional = state.fortunateAim && canReceiveFortunateAim;
	result.gamblerAttackReady = state.gamblerAttackAvailable();
	result.chainFortuneReady = state.chainFortuneAvailable(unitId);
	return result;
}

inline std::string sylvanLuckStackTooltip(const SylvanLuckStackStatus & status,
	std::string_view skillName, std::string_view serendipityName,
	std::string_view forestFavorName, std::string_view sharedFortuneName,
	std::string_view cascadingFortuneName, std::string_view fortunateAimName)
{
	if(!status.active())
		return {};

	const auto signedValue = [](int32_t value)
	{
		return value > 0 ? "+" + std::to_string(value) : std::to_string(value);
	};
	std::string result = skillName.empty() ? "Sylvan Luck" : std::string(skillName);
	result += "\n\nCurrent capped Luck for an ordinary attack: " + signedValue(status.effectiveCappedLuck)
		+ ". This excludes Serendipity's chance-only Luck, Fortunate Aim's Focus Fire target bonus, and automatic Perfect Moment.";
	if(status.sharedFortuneLuck > 0)
		result += "\n" + std::string(sharedFortuneName) + ": +1 temporary Luck until this stack's next activation.";
	if(status.cascadingFortuneLuck > 0)
		result += "\n" + std::string(cascadingFortuneName) + ": +3 temporary Luck for this stack's current activation.";
	if(status.forestFavorSpeed > 0)
		result += "\n" + std::string(forestFavorName) + ": +" + std::to_string(status.forestFavorSpeed)
			+ " Speed until the next creature activation begins.";
	else if(status.forestFavorReady)
		result += "\n" + std::string(forestFavorName)
			+ ": this stack's first positive Luck trigger this combat grants +2 Speed for the remainder of that activation.";
	if(status.serendipityChanceOnlyReady)
		result += "\n" + std::string(serendipityName)
			+ ": +1 Luck for trigger chance only until this stack triggers positive Luck once this combat; it is not persistent temporary Luck.";
	if(status.fortunateAimConditional)
		result += "\n" + std::string(fortunateAimName)
			+ ": +1 attack Luck only when this shooter attacks the active Focus Fire target; this conditional bonus is not included above.";
	if(status.gamblerAttackReady)
		result += "\nGambler: +3 attack Luck is ready for this side's first attack this round, subject to normal Luck caps.";
	if(status.chainFortuneReady)
		result += "\nChain of Fortune: this different friendly stack's next attack receives +1 Luck and consumes the gift, subject to normal Luck caps.";
	return result;
}

struct StackInfoStatusSnapshot
{
	DefendStatus defend;
	std::optional<BattlecraftWaitStatus> battlecraftWait;
	int overwatchRange = 0;
	SylvanLuckStackStatus sylvanLuck;
	BattleFormStatus battleForm;
	PhysicalPoisonStatus physicalPoison;
	TemporaryCreatureStatus temporaryCreatures;
	EntangleStatus entangle;
	DivineRetributionProtectionStatus divineRetribution;
	DivineRetributionJudgedStatus divineRetributionJudged;
	RegenerationStatus regeneration;
	GuardianSpiritStatus guardianSpirit;
	ShadowGiftStatus shadowGift;
	VampirismStatus vampirism;
	DoomStatus doom;
	ConfusionStatus confusion;

	bool operator==(const StackInfoStatusSnapshot &) const = default;
};

inline std::string defendStatusTooltip(const DefendStatus & status)
{
	if(!status.defending)
		return {};

	std::string result = status.bulwark ? "Bulwark of the Mire - Defend\n" : "Defend\n";
	result += "This stack remains Defending until its next normal Creature Activation.";
	if(status.battlecraftReductionPercent > 0)
	{
		result += "\nBattlecraft physical creature-damage reduction: "
			+ std::to_string(status.battlecraftReductionPercent)
			+ "% until this stack's next normal Creature Activation. It applies only to physical creature damage and contributes to the shared Physical Damage Reduction cap.";
	}

	if(!status.bulwark)
		return result;

	const auto & bulwark = *status.bulwark;
	result += "\nPhysical creature damage reduction: " + formatBasisPoints(bulwark.damageReductionBasisPoints) + ".";
	if(bulwark.mirebornTerrainBonus)
		result += " Includes Mireborn's +5 percentage points on swamp or rough terrain.";
	if(bulwark.sharedCoverApplied)
	{
		if(bulwark.sharedCoverAdjustmentBasisPoints > 0)
			result += "\nShared Cover adds +" + formatPercentagePoints(bulwark.sharedCoverAdjustmentBasisPoints)
				+ " (half the base Bulwark reduction, capped at 100%) while an adjacent friendly creature stack is also Defending.";
		else
			result += "\nShared Cover reaches its 100% Bulwark reduction cap while an "
				"adjacent friendly creature stack is also Defending.";
	}
	result += "\nPre-emptive strike: " + std::to_string(bulwark.preemptiveDamagePercent)
		+ "% normal damage against the first qualifying melee attacker; ";
	result += bulwark.preemptiveReady ? "ready." : "already triggered during this Defend stance.";
	if(bulwark.bogAmbush)
		result += " Bog Ambush is included above (+25 percentage points, capped at 100%).";
	if(bulwark.meleeReflectionBasisPoints > 0)
		result += "\nMelee reflection: " + formatBasisPoints(bulwark.meleeReflectionBasisPoints)
			+ " of actual physical health loss after reductions.";
	if(bulwark.vengefulMireBonusBasisPoints > 0)
		result += " Includes Vengeful Mire (adds +" + formatPercentagePoints(bulwark.vengefulMireBonusBasisPoints)
			+ " to melee reflection only, up to 75%).";
	if(bulwark.rangedReflectionBasisPoints > 0)
		result += "\nRanged reflection: " + formatBasisPoints(bulwark.rangedReflectionBasisPoints)
			+ " of actual ranged physical health loss after reductions (Thick Hide).";
	return result;
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
