/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SylvanTacticsProjection.h"
#include "../../lib/battle/NewHorizonsReactiveDefense.h"

struct BattleStackAttacked;
class UnitChanges;

/// One already accepted projected damage packet. The existing caller owns its
/// target order and completion boundary; this object neither selects actions
/// nor classifies generic removal as combat destruction.
class PhysicalStrikeProjection
{
	HypotheticBattle * branch;
	sylvanTacticsAI::StrikeProjection sylvan;
	struct Death
	{
		uint32_t actor;
		newHorizonsReactiveDefense::Destruction victim;
	};
	std::vector<Death> deaths;
public:
	explicit PhysicalStrikeProjection(HypotheticBattle * value) : branch(value), sylvan(value) {}
	static bool needsProjection(const CBattleInfoCallback &, const battle::Unit *, const battle::Unit *);
	/// Explicit damage only. Generic removal, value restoration and spawn wounds are not events.
	static std::optional<newHorizonsReactiveDefense::Destruction> captureForecastDamage(
		const HypotheticBattle &, const battle::Unit &);
	static std::vector<newHorizonsReactiveDefense::Destruction> captureDamagePacket(
		const HypotheticBattle &, std::span<const BattleStackAttacked>);
	static std::vector<newHorizonsReactiveDefense::Destruction> captureDamagePacket(
		const HypotheticBattle &, std::span<const UnitChanges>);
	static void completeDamagePacket(HypotheticBattle &,
		std::span<const newHorizonsReactiveDefense::Destruction>);
	static int64_t applyExpectedReactionDamage(HypotheticBattle &, const BattleAttackInfo &, battle::CUnitState &);
	void damage(const CBattleInfoCallback &, const BattleAttackInfo &, battle::CUnitState &,
		int64_t &, battle::DamageProvenance);
	void finish(uint32_t attackerId);
	/// Final-snapshot replay includes unharmed Demon recipients outside the
	/// selected attack's affectedUnits. Only these two strict overlays transfer.
	static void restoreEffects(HypotheticBattle & destination, const HypotheticBattle & captured);
};
