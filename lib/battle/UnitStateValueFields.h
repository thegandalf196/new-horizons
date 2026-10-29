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
#include <tuple>
#include <type_traits>
#include <utility>

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
			unit.capacityRegenerationRemainderTenths,
			unit.timeStopTurnConsumedFlag,
			unit.regenerationRateMillionths,
			unit.regenerationPendingMicroHealth,
			unit.summoned,
			unit.natureSummoned,
			unit.summonRoundsRemaining,
			unit.waiting,
			unit.waitedThisTurn,
			unit.battlecraftWaitBonusUsed,
			unit.forestAmbush,
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


public:
	static void copy(CUnitState & destination, const CUnitState & source)
	{
		auto target = references(destination);
		copyFields(target, references(source), std::make_index_sequence<std::tuple_size_v<decltype(target)>>{});
	}

	static void exchange(CUnitState & destination, CUnitState & prepared) noexcept
	{
		auto target = references(destination);
		exchangeFields(target, references(prepared), std::make_index_sequence<std::tuple_size_v<decltype(target)>>{});
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
