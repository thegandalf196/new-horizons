/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SylvanTacticsProjection.h"
#include "ArcaneInspirationProjection.h"
#include "../../lib/battle/NewHorizonsFactionSpellDamage.h"
#include "../../lib/battle/NewHorizonsReactiveDefense.h"

struct BattleStackAttacked;
class UnitChanges;

/// One already accepted projected damage packet. The existing caller owns its
/// target order and completion boundary; this object neither selects actions
/// nor classifies generic removal as combat destruction.
class PhysicalStrikeProjection
{
public:
	struct Destruction
	{
		uint32_t unit;
		std::optional<newHorizonsReactiveDefense::Destruction> solidarity;
		std::optional<newHorizonsFactionSpellDamage::Cataclysm> cataclysm;
	};
private:
	HypotheticBattle * branch;
	sylvanTacticsAI::StrikeProjection sylvan;
	arcaneInspirationAI::StrikeProjection inspiration;
	struct Death
	{
		uint32_t actor;
		Destruction victim;
	};
	std::vector<Death> deaths;
	std::vector<std::pair<uint32_t, newHorizonsOrdinaryStrike::EffectPlan>> exposures;
public:
	explicit PhysicalStrikeProjection(HypotheticBattle * value) : branch(value), sylvan(value), inspiration(value) {}
	static bool needsProjection(const CBattleInfoCallback &, const battle::Unit *, const battle::Unit *);
	/// Explicit damage only. Generic removal, value restoration and spawn wounds are not events.
	static std::optional<Destruction> captureForecastDamage(
		const HypotheticBattle &, const battle::Unit &);
	static std::vector<Destruction> captureDamagePacket(
		const HypotheticBattle &, std::span<const BattleStackAttacked>);
	static std::vector<Destruction> captureDamagePacket(
		const HypotheticBattle &, std::span<const UnitChanges>);
	static void completeDamagePacket(HypotheticBattle &,
		std::span<const Destruction>);
	static int64_t applyExpectedReactionDamage(HypotheticBattle &, const BattleAttackInfo &, battle::CUnitState &);
	void damage(const CBattleInfoCallback &, const BattleAttackInfo &, battle::CUnitState &,
		int64_t &, battle::DamageProvenance);
	void finish(uint32_t attackerId);
	std::pair<float, float> reprisalValue(DamageCache & cache, const std::shared_ptr<CBattleInfoCallback> & source, PlayerColor reference) const
	{ return inspiration.value(cache, source, reference); }
	/// Final-snapshot replay includes unharmed Demon recipients outside the
	/// selected attack's affectedUnits. Only these two strict overlays transfer.
	static void restoreEffects(HypotheticBattle & destination, const HypotheticBattle & captured);
};
