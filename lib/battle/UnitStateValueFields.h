/*
 * UnitStateValueFields.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "CUnitState.h"
#include "PhysicalPoisonSpellOrigin.h"
#include <tuple>
#include <optional>
#include <type_traits>
#include <utility>
#include <stdexcept>

namespace battle
{
/// One ordered value inventory shared by legacy copy and prepared exchange.
/// This private implementation never transfers owner pointers or bonus caches.
class UnitStateValueFields
{
private:
	template<typename State> static auto references(State & unit) noexcept
	{
		return std::tie(
			unit.cloned,
			unit.activationMovementBonus,
			unit.defending,
			unit.drainedMana,
			unit.fear,
			unit.hadMorale,
			unit.moraleExtraActivation,
			unit.castSpellThisTurn,
			unit.ghost,
			unit.ghostPending,
			unit.movedThisRound,
			unit.confusionState,
			unit.demonicGateOpened,
			unit.dreadCavalryState,
			unit.pursuitMovementRemaining,
			unit.cleaveUsedThisActivation,
			unit.rangedFollowUpDamagePercent,
			unit.luckyOwnAttackSequence,
			unit.archeryCounterfireRound,
			unit.aerialRetaliationRound,
			unit.archeryDeadeyeRound,
			unit.archerySuppressionActivationSerial,
			unit.archeryRainOfArrowsActivationSerial,
			unit.archeryCrossfireRound,
			unit.archeryCrossfireAttackers,
			unit.archeryCrossfireDefenders,
			unit.noQuarterMoraleActivationsRemaining,
			unit.personalBloodrageIncrement,
			unit.veteranCohesionEarned,
			unit.ordinarySpecialtyLastRound,
			unit.capacityRegenerationRemainderTenths,
			unit.regenerationRateMillionths,
			unit.regenerationPendingMicroHealth,
			unit.summoned,
			unit.natureSummoned,
			unit.summonRoundsRemaining,
			unit.waiting,
			unit.waitedThisTurn,
			unit.battlecraftWaitBonusUsed,
			unit.forestAmbush,
			unit.sylvanAmbush,
			unit.battlecraftOverwatchReadyRound,
			unit.battlecraftOverwatchUsedRound,
			unit.heroicSpiritRetaliation,
			unit.heroicSpiritMoralePending,
			unit.battlecraftWaitMasteryDoubled,
			unit.battlecraftPreemptiveStrikeRound,
			unit.petrifyingBloodRound,
			unit.defensiveStanceMeleeBonus,
			unit.defensiveStanceRangedBonus,
			unit.battlecraftDefendMasteryDoubled,
			unit.armorerLastStandEndedActivation,
			unit.armorerLastStandDefending,
			unit.bulwarkPreemptiveUsed,
			unit.bulwarkMireGripApplied,
			unit.bulwarkDefendPhysicalDamage,
			unit.veteranPhysicalDamageSinceActivation,
			unit.bulwarkImmovableRound,
			unit.armorerBastionRound,
			unit.bulwarkToxicSpinesRound,
			unit.physicalPoisonBaseDamage,
			unit.physicalPoisonActivationsRemaining,
			unit.physicalPoisonSourceStackId,
			unit.physicalPoisonSpellSide,
			unit.physicalPoisonDescriptor,
			unit.redemptionShield,
			unit.arcaneWard,
			unit.completedActionThisRound,
			unit.guardianSpiritHitPoints,
			unit.guardianSpiritRoundsRemaining,
			unit.phantomInitialIntegrity,
			unit.phantomIntegrity,
			unit.phantomRoundsRemaining,
			unit.frozenAppliedRound,
			unit.phantomShadowGiftMaximumHealthLost,
			unit.capacityHealthReferenceMax,
			unit.battleFormOriginalHealth,
			unit.battleFormCreatureId,
			unit.battleFormOriginalCreatureId,
			unit.battleFormRoundsRemaining,
			unit.battleFormRestorationPending,
			unit.battleFormOriginalMaxHealth,
			unit.battleFormOriginalCount,
			unit.battleFormInitiativeSnapshot,
			unit.battleFormInitiativeSnapshotActive,
			unit.battleFormOriginalCapacityHealthReferenceMax,
			unit.battleFormOriginalCapacityRegenerationRemainderTenths,
			unit.casts,
			unit.counterAttacks,
			unit.shots,
			unit.health,
			unit.cloneID,
			unit.position);
	}


private:
	struct AmmoValue { int32_t used; };
	struct DescriptorValue { creatureAbility::Descriptor value; };
	struct RetaliationValue { int32_t used, totalCache; };
	template<typename Value> static constexpr bool auditedValue =
		std::is_integral_v<Value> || std::is_enum_v<Value>
		|| std::is_same_v<Value, CreatureID> || std::is_same_v<Value, BattleHex>
		|| std::is_same_v<Value, std::vector<uint32_t>>
		|| std::is_same_v<Value, ConfusionState> || std::is_same_v<Value, DreadCavalryState>
		|| std::is_same_v<Value, ForestAmbushState> || std::is_same_v<Value, SylvanAmbushState>
		|| std::is_same_v<Value, RedemptionShieldState> || std::is_same_v<Value, ArcaneWardShieldState>;

	template<typename Value> static Value ownedValue(const Value & value)
	{
		static_assert(auditedValue<Value>, "Native snapshot inventory needs an audited owned-value codec");
		return value;
	}
	static RedemptionShieldState ownedValue(const RedemptionShieldState & value)
	{
		value.validate();
		return value;
	}
	static DescriptorValue ownedValue(const creatureAbility::Descriptor & value)
	{
		if(!value) return {};
		value->validate();
		// Freeze values, not a const alias of a potentially mutable producer object.
		return {std::make_shared<const CreatureAbilityDescriptor>(*value)};
	}
	static CHealth ownedValue(const CHealth & value)
	{
		return CHealth::detached(value, value.valueContext());
	}
	static AmmoValue ownedValue(const CCasts & value) { return {value.used}; }
	static AmmoValue ownedValue(const CShots & value) { return {value.used}; }
	static RetaliationValue ownedValue(const CRetaliations & value) { return {value.used, value.totalCache}; }

	template<typename Values, size_t... Index> static auto captureFields(Values values, std::index_sequence<Index...>)
	{
		return std::tuple{ownedValue(std::get<Index>(values))...};
	}
	template<typename Value> static void validateRestore(const Value &, const Value &)
	{
		static_assert(auditedValue<Value>);
	}
	static void validateRestore(const RedemptionShieldState &, const RedemptionShieldState & value)
	{
		value.validate();
	}
	static void validateRestore(const creatureAbility::Descriptor &, const DescriptorValue & value)
	{
		if(value.value) value.value->validate();
	}
	static void validateRestore(const CHealth & target, const CHealth & value)
	{
		if(!value.isDetached() || target.valueContext() != value.valueContext())
			throw std::invalid_argument("Native snapshot health owner/context does not match receiver");
	}
	static void validateRestore(const CCasts &, const AmmoValue & value)
	{
		if(value.used < 0) throw std::invalid_argument("Negative native cast usage");
	}
	static void validateRestore(const CShots &, const AmmoValue & value)
	{
		if(value.used < 0) throw std::invalid_argument("Negative native shot usage");
	}
	static void validateRestore(const CRetaliations &, const RetaliationValue & value)
	{
		if(value.used < 0 || value.totalCache < -1) throw std::invalid_argument("Invalid native retaliation usage/cache");
	}
	template<typename Target, typename Stored, size_t... Index>
	static void validateFields(Target target, const Stored & values, std::index_sequence<Index...>)
	{
		(validateRestore(std::get<Index>(target), std::get<Index>(values)), ...);
	}
	template<typename Value> static void restoreValue(Value & target, Value & value) noexcept
	{
		static_assert(auditedValue<Value> && std::is_nothrow_swappable_v<Value>);
		using std::swap;
		swap(target, value);
	}
	static void restoreValue(creatureAbility::Descriptor & target, DescriptorValue & value) noexcept
	{
		target.swap(value.value);
	}
	static void restoreValue(CHealth & target, CHealth & value) noexcept { target.swapPreparedContents(value); }
	static void restoreValue(CCasts & target, AmmoValue & value) noexcept { target.used = value.used; }
	static void restoreValue(CShots & target, AmmoValue & value) noexcept { target.used = value.used; }
	static void restoreValue(CRetaliations & target, RetaliationValue & value) noexcept
	{
		target.used = value.used;
		target.totalCache = value.totalCache;
	}
	template<typename Target, typename Stored, size_t... Index>
	static void restoreFields(Target target, Stored & values, std::index_sequence<Index...>) noexcept
	{
		(restoreValue(std::get<Index>(target), std::get<Index>(values)), ...);
	}

public:
	/// Same ordered inventory as copy/exchange, transformed into owned values.
	static auto captureSnapshot(const CUnitState & source)
	{
		validatePhysicalPoisonSerialization(source, true);
		auto fields = references(source);
		return captureFields(fields, std::make_index_sequence<std::tuple_size_v<decltype(fields)>>{});
	}
	template<typename Stored> static int32_t snapshotRetaliationCache(const Stored & values)
	{
		// Unique type lookup fails compilation if the single native retaliation
		// inventory entry is removed or duplicated.
		return std::get<RetaliationValue>(values).totalCache;
	}
	static int32_t retaliationCache(const CUnitState & source)
	{
		return std::get<const CRetaliations &>(references(source)).totalCache;
	}
	template<typename Stored> static void restoreSnapshot(CUnitState & destination, const Stored & values,
		std::optional<int32_t> retaliationCache = std::nullopt)
	{
		auto target = references(destination);
		static_assert(std::tuple_size_v<Stored> == std::tuple_size_v<decltype(target)>);
		const auto sequence = std::make_index_sequence<std::tuple_size_v<Stored>>{};
		validateFields(target, values, sequence); // Every context validated before any write.
		if(retaliationCache && *retaliationCache < -1)
			throw std::invalid_argument("Invalid native retaliation cache override");
		auto prepared = values; // All vector/health allocation completes before the swap prefix.
		if(retaliationCache) std::get<RetaliationValue>(prepared).totalCache = *retaliationCache;
		restoreFields(target, prepared, sequence);
		destination.stateRevision.bump();
	}

	static void copy(CUnitState & destination, const CUnitState & source)
	{
		auto target = references(destination);
		copyFields(target, references(source), std::make_index_sequence<std::tuple_size_v<decltype(target)>>{});
		destination.stateRevision.bump(); // The revision itself is never part of the copied inventory.
	}

	static void exchange(CUnitState & destination, CUnitState & prepared) noexcept
	{
		auto target = references(destination);
		exchangeFields(target, references(prepared), std::make_index_sequence<std::tuple_size_v<decltype(target)>>{});
		destination.stateRevision.bump();
		prepared.stateRevision.bump();
	}

private:
	template<typename Destination, typename Source, size_t... Index>
	static void copyFields(Destination destination, Source source, std::index_sequence<Index...>)
	{
		// Comma fold preserves the legacy left-to-right member assignment order.
		((std::get<Index>(destination) = std::get<Index>(source)), ...);
	}

	template<typename Destination, typename Prepared, size_t... Index>
	static void exchangeFields(Destination destination, Prepared prepared, std::index_sequence<Index...>) noexcept
	{
		(exchangeValue(std::get<Index>(destination), std::get<Index>(prepared)), ...);
	}

	template<typename Value> static void exchangeValue(Value & destination, Value & prepared) noexcept
	{
		static_assert(std::is_nothrow_swappable_v<Value>);
		using std::swap;
		swap(destination, prepared);
	}

	static void exchangeValue(CHealth & destination, CHealth & prepared) noexcept
	{
		static_assert(noexcept(destination.swapPreparedContents(prepared)));
		destination.swapPreparedContents(prepared);
	}

	static void exchangeValue(CCasts & destination, CCasts & prepared) noexcept
	{
		std::swap(destination.used, prepared.used);
	}

	static void exchangeValue(CShots & destination, CShots & prepared) noexcept
	{
		std::swap(destination.used, prepared.used);
	}

	static void exchangeValue(CRetaliations & destination, CRetaliations & prepared) noexcept
	{
		std::swap(destination.used, prepared.used);
		std::swap(destination.totalCache, prepared.totalCache);
	}
};
}
