/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <cstdint>
#include <optional>

class HypotheticBattle;
class StackWithBonuses;
class IDamageCalculatorScript;
class BattleExchangeVariant;

/// One selected ordinary probe, borrowed only until its first applied hit.
/// Not a cache key: the serial selector owns the immutable interval.
class SelectedProbeDamage
{
	friend class BattleExchangeVariant;
	const HypotheticBattle * source = nullptr;
	const StackWithBonuses * actor = nullptr;
	const StackWithBonuses * target = nullptr;
	const IDamageCalculatorScript * calculator = nullptr;
	bool shooting = false;
	std::optional<int64_t> raw;

	void record(const HypotheticBattle & battle, const StackWithBonuses & attacker,
		const StackWithBonuses & defender, bool shot, int64_t damage);
	const IDamageCalculatorScript * admit() const;
	std::optional<int64_t> consume(const HypotheticBattle & battle,
		const StackWithBonuses & attacker, const StackWithBonuses & defender, bool shot);
public:
	SelectedProbeDamage() = default;
};
