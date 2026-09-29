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
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/bonuses/BonusEnum.h"
#include "../../lib/bonuses/BonusParameters.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "StackInfoStatusPresentation.h"

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
inline constexpr std::string_view SPELL_LOCK_SPELL_KEY = "new-horizons:spellLock";
inline constexpr std::string_view REGENERATION_SPELL_KEY = newHorizonsMagic::NATURE_REGENERATION_SPELL;
inline constexpr std::string_view SANCTUARY_SPELL_KEY = "new-horizons:sanctuary";
inline constexpr std::string_view GUARDIAN_SPIRIT_SPELL_KEY = "new-horizons:guardianSpirit";
inline constexpr std::string_view SHADOW_GIFT_SPELL_KEY = "new-horizons:shadowGift";
inline constexpr std::string_view SHADOW_GIFT_TRIGGER_KEY = "core:shadowGift";
inline constexpr std::string_view VAMPIRISM_SPELL_KEY = "new-horizons:vampirism";
inline constexpr std::string_view VAMPIRISM_TRIGGER_KEY = "core:vampirism";
inline constexpr std::string_view DOOM_SPELL_KEY = "new-horizons:doom";
inline constexpr std::string_view REANIMATE_SPELL_KEY = "new-horizons:reanimate";

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

inline bool isSpellLock(std::string_view spellKey)
{
	return spellKey == SPELL_LOCK_SPELL_KEY;
}

inline bool isSanctuary(std::string_view spellKey)
{
	return spellKey == SANCTUARY_SPELL_KEY;
}

inline bool isGuardianSpirit(std::string_view spellKey)
{
	return spellKey == GUARDIAN_SPIRIT_SPELL_KEY;
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
	std::optional<BulwarkStatus> bulwark;

	bool operator==(const DefendStatus &) const = default;
};

struct StackInfoStatusSnapshot
{
	DefendStatus defend;
	PhysicalPoisonStatus physicalPoison;
	TemporaryCreatureStatus temporaryCreatures;
	RegenerationStatus regeneration;
	GuardianSpiritStatus guardianSpirit;
	ShadowGiftStatus shadowGift;
	VampirismStatus vampirism;
	DoomStatus doom;

	bool operator==(const StackInfoStatusSnapshot &) const = default;
};

inline std::string defendStatusTooltip(const DefendStatus & status)
{
	if(!status.defending)
		return {};

	if(!status.bulwark)
		return "Defend\nThis stack remains Defending through the end of the current battle round.";

	const auto & bulwark = *status.bulwark;
	std::string result = "Bulwark of the Mire - Defend\nThis stack remains Defending through the end of the current battle round.";
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
