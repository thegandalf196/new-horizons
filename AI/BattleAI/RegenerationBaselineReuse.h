/* RegenerationBaselineReuse.h, VCMI / New Horizons, GPL v2.0 or later. */
#pragma once
#include "AttackPossibility.h"
#include <optional>
#include <cstdint>

/// One synchronous Hero-candidate frame with immutable beforeCast callback state.
/// Stores ONLY completed baseline heal observations, never target forecasts/scores.
class RegenerationBaselineReuse
{
public:
	struct TestControl { bool enabled = true; uint64_t hits = 0; uint64_t completed = 0; };
	struct Observation { uint32_t unit; int64_t heal; };
	struct Key
	{
		const Environment * environment = nullptr;
		const CBattleInfoCallback * before = nullptr;
		std::vector<std::vector<uint32_t>> queue;
		BattleSide side;
		PlayerColor player;
		DamageCache::Snapshot damage;
	};
	struct Session
	{
		const std::vector<Observation> * observations = nullptr;
		std::optional<Key> recording;
	};
	explicit RegenerationBaselineReuse(TestControl * control = nullptr) : control(control) {}
	Session prepare(const Environment * environment, const CBattleInfoCallback & before, const std::vector<battle::Units> & order,
		const DamageCache & damage, BattleSide side, PlayerColor player);
	void complete(Session && session, std::vector<Observation> observations);
private:
	TestControl * control;
	std::optional<Key> key;
	std::vector<Observation> observations;
};
