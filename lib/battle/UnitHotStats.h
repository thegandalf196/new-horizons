/*
 * UnitHotStats.h, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once
#include <array>
#include <atomic>
#include <cstdint>

namespace battle
{
/// Derived per-unit answers that battle rules and the battle AI ask many
/// times between two changes of the unit. The bonus system stays their only
/// definition; a slot only remembers the last answer under a validity key.
enum class HotStat : uint8_t
{
	SUPPRESSION_LEVEL, ///< newHorizonsCreatureAbilitySuppression::suppressionLevel
	HYPNOTIZED, ///< CUnitState::isHypnotized
	INVINCIBLE, ///< CUnitState::isInvincible
	MAX_HEALTH, ///< CUnitState::getMaxHealth
	FOCUS_MAGIC_CHARGES, ///< newHorizonsFocusMagic::charges
	FROZEN, ///< newHorizonsFrozen::isFrozen
	DYNAMIC_DAMAGE_PASSIVE, ///< newHorizonsPassiveSpecialties::hasDynamicDamagePassive
	SIEGE_WEAPON, ///< hasBonusOfType(SIEGE_WEAPON) of newHorizonsCombatSkills::isOrdinaryCreatureAttacker
	COUNT
};

/// Never-saved slot table owned by one unit. Each slot is one atomic word
/// holding (key, value), so a reader on another thread sees either an empty
/// slot or a complete answer for the key it was stored under; a different key
/// is a miss. Copies and assignments start empty. NH_HOT_STATS=0 turns reuse
/// off (for A/B measurement); NH_HOT_STATS_CHECK=1 recomputes every reused
/// answer and aborts with a diagnostic on a mismatch.
class DLL_LINKAGE UnitHotStats
{
public:
	enum class Mode : uint8_t { OFF, ON, CHECK };

	UnitHotStats() noexcept;
	UnitHotStats(const UnitHotStats &) noexcept;
	UnitHotStats & operator=(const UnitHotStats &) noexcept;

	Mode mode() const noexcept { return currentMode; }
	/// The answer stored for `stat` under exactly `key`, if any.
	bool find(HotStat stat, uint32_t key, int32_t & value) const noexcept
	{
		const uint64_t word = slots[static_cast<size_t>(stat)].load(std::memory_order_acquire);
		if((word & FILLED) == 0 || static_cast<uint32_t>(word >> 32) != key)
			return false;
		value = static_cast<int32_t>(word & VALUE_MASK);
		return true;
	}
	/// Negative answers are never stored (none of the slots produces one).
	void store(HotStat stat, uint32_t key, int32_t value) const noexcept
	{
		if(value < 0)
			return;
		const uint64_t word = (static_cast<uint64_t>(key) << 32) | FILLED | static_cast<uint64_t>(value);
		slots[static_cast<size_t>(stat)].store(word, std::memory_order_release);
	}
	void clear() const noexcept;

private:
	static constexpr uint64_t FILLED = uint64_t(1) << 31;
	static constexpr uint64_t VALUE_MASK = FILLED - 1;
	Mode currentMode;
	mutable std::array<std::atomic<uint64_t>, static_cast<size_t>(HotStat::COUNT)> slots;
};

/// One read's view of a unit's slots: null `stats` means the unit kind does
/// not memoise (every answer is computed), `key` is its current validity key,
/// and `excluded` lists slots (bit per HotStat) whose inputs the key does not
/// cover for this unit right now; those are computed every time.
struct UnitHotStatsAccess
{
	const UnitHotStats * stats = nullptr;
	uint32_t key = 0;
	uint32_t excluded = 0;
};

constexpr uint32_t hotStatBit(HotStat stat) noexcept
{
	return uint32_t(1) << static_cast<uint32_t>(stat);
}
}
