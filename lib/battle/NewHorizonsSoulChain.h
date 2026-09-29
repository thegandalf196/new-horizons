/*
 * NewHorizonsSoulChain.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

#include "BattleSide.h"
#include "Destination.h"

class CBattleInfoCallback;
class JsonNode;
struct BattleStackAttacked;

namespace battle
{
class Unit;
}

namespace newHorizonsSoulChain
{
constexpr std::string_view SPELL_ID = "new-horizons:soulChain";
constexpr int32_t MAX_TARGETS = 3;
constexpr int32_t BASIS_POINTS_PER_WHOLE = 10'000;
constexpr int32_t BASE_ECHO_CAP_BASIS_POINTS = 4'000;
constexpr int32_t SOUL_BINDER_BONUS_BASIS_POINTS = 1'500;

struct DLL_LINKAGE Link
{
	uint32_t primaryUnitId = 0;
	BattleSide casterSide = BattleSide::NONE;
	int32_t echoBasisPoints = 0;
};

/// True only when the saved battle ruleset admits the Soul Chain spell.
DLL_LINKAGE bool isEnabled(const JsonNode & savedMagicRules);

/// Structural target validation shared by authoritative casting and forecasts.
/// Receptivity, Spell Lock and other spell-specific checks remain on the caller.
DLL_LINKAGE bool validEnemyTargetSet(
	const CBattleInfoCallback & battle,
	BattleSide casterSide,
	const battle::Target & target);

/// The first ordered target is the primary; the next zero to two are secondaries.
/// The fixed 20% is unscaled; the raw Spell Power term uses the saved School ×
/// Spellcraft coefficient. Soul Binder's 15 percentage points are added after
/// the ordinary 40% cap.
DLL_LINKAGE int32_t echoPercentBasisPoints(int32_t rawSpellPower,
	int32_t spellPowerCoefficientBasisPoints, bool soulBinderActive);

/// Floors the percentage of actual damage that is echoed, before primary-target
/// magical resistance and damage reduction are applied.
DLL_LINKAGE int64_t echoDamage(int64_t actualSecondaryDamage, int32_t echoBasisPoints);

/// Reads the serializable relationship marker stored on a secondary stack.
DLL_LINKAGE std::optional<Link> linkFor(const battle::Unit * secondary);

/// Identifies an echo packet so it cannot trigger any Soul Chain relationship.
DLL_LINKAGE bool isEchoHit(const BattleStackAttacked & hit);

/// Applies the ordinary saved-spell Shadow damage modifiers to an already
/// calculated echo amount. The returned value is pre-casualty-clamp damage.
DLL_LINKAGE int64_t adjustedEchoDamage(
	const CBattleInfoCallback & battle,
	BattleSide casterSide,
	const battle::Unit * primary,
	int64_t actualSecondaryDamage,
	int32_t echoBasisPoints);
}
