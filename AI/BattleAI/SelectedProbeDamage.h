/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#pragma once
#include <cstdint>
#include <memory>
#include <optional>

class HypotheticBattle;
class StackWithBonuses;
class IDamageCalculatorScript;
class BattleExchangeVariant;
class DamageCache;
class DamageCacheAdmission;
struct DLL_LINKAGE BattleTargetProbeTiming;

/// One selected ordinary probe, borrowed only until its first applied hit.
/// Not a cache key: the serial selector owns the immutable interval.
/// The value is captured after both damage lookups have completed, including
/// their idempotent DamageCache tracking. It never crosses an applied mutation.
class SelectedProbeDamage
{
	friend class BattleExchangeVariant;
	const HypotheticBattle * source = nullptr;
	const StackWithBonuses * actor = nullptr;
	const StackWithBonuses * target = nullptr;
	const IDamageCalculatorScript * calculator = nullptr;
	bool shooting = false;
	bool reuseScore = true;
	struct Value
	{
		int64_t raw;
		uint64_t healthLoss;
		float damageReduce;
		const DamageCache * cache;
	};
	/// Call-local ordinary-hit stages. The caller preserves native Last Stand and
	/// health projection between damage() and damageReduce(); no mutation intervenes.
	class Evaluation
	{
		const std::shared_ptr<StackWithBonuses> & attacker;
		const std::shared_ptr<StackWithBonuses> & defender;
		const std::shared_ptr<HypotheticBattle> & battle;
		DamageCache & cache;
		bool shooting, evaluateOnly, protectIntercepted;
		const BattleTargetProbeTiming * timing;
		DamageCacheAdmission * admission;
		SelectedProbeDamage * produced;
		SelectedProbeDamage * selected;
		uint64_t * damageUses;
		uint64_t * scoreUses;
		std::optional<Value> retained;
		int64_t rawDamage = 0;
	public:
		Evaluation(const std::shared_ptr<StackWithBonuses> & attacker,
			const std::shared_ptr<StackWithBonuses> & defender,
			const std::shared_ptr<HypotheticBattle> & battle, DamageCache & cache,
			bool shooting, bool evaluateOnly, bool protectIntercepted,
			const BattleTargetProbeTiming * timing, DamageCacheAdmission * admission,
			SelectedProbeDamage * produced, SelectedProbeDamage * selected,
			uint64_t * damageUses, uint64_t * scoreUses);
		int64_t damage();
		float damageReduce(uint64_t healthLoss);
	};
	std::optional<Value> value;

	void record(const HypotheticBattle & battle, const StackWithBonuses & attacker,
		const StackWithBonuses & defender, bool shot, int64_t damage, uint64_t healthLoss,
		float damageReduce, const DamageCache & cache);
	const IDamageCalculatorScript * admit() const;
	bool admitScore() const;
	std::optional<Value> consume(const HypotheticBattle & battle,
		const StackWithBonuses & attacker, const StackWithBonuses & defender, bool shot);
public:
	SelectedProbeDamage() = default;
};
