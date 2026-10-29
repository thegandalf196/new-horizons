/*
 * AttackPreviewHitContext.h, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include <cstdint>
#include <optional>
#include "../../lib/battle/NewHorizonsAerialSupremacy.h"
#include "../../lib/battle/IBattleInfoCallback.h"
#include "../../lib/battle/BattleSide.h"
#include <memory>
#include <utility>
#include <vector>

class HypotheticBattle;
class CBattleInfoCallback;
struct BattleAttackInfo;
struct FortuneStrikeProjection;
namespace battle { class CUnitState; class Unit; enum class DamageProvenance : uint8_t; }
namespace newHorizonsArmorer { struct ArmorerLastStandDamageResult; }

/// Synchronous borrowed context for the existing preview's Last Stand/Luck
/// hit phases. The caller owns the real effect branch and activation flag for
/// this object's entire lifetime; no callback, native/configuration lease or
/// independent graph is synthesized. Unknown Luck remains unknown.
class AttackPreviewHitContext
{
	const std::shared_ptr<HypotheticBattle> & fortunePreview;
	bool & endedActiveActivationByLastStand;
public:
	static bool anyNightProwler(const battle::Units & units);
	/// Separate phases preserve creature-admission query order in the caller.
	static void capturePrimaryFortune(FortuneStrikeProjection & receipt,
		const CBattleInfoCallback & state, const BattleAttackInfo & attack, const battle::Unit & primary);
	static void classifyPrimaryStrike(FortuneStrikeProjection & receipt, const BattleAttackInfo & attack,
		bool perfectMoment, int attackIndex, const std::optional<newHorizonsAerialSupremacy::Excursion> & aerial);
	static bool canMireGripBeHit(const CBattleInfoCallback & state, const battle::Unit * primary,
		bool longReachAttack, const battle::Units & possibleVictims);
	FortuneStrikeProjection capturePreemptiveStrike(const BattleAttackInfo & attack,
		battle::CUnitState & actor, int64_t requestedDamage, int64_t appliedDamage, bool perfectFortune,
		BattleSide perfectFortuneSide, bool brace, battle::DamageProvenance provenance) const;
	struct SoulHarvestHit
	{
		bool eligible;
		bool willRebirth;
		int32_t countBefore;
	};
	static SoulHarvestHit prepareSoulHarvest(const CBattleInfoCallback & state,
		const battle::Unit & actor, const battle::Unit & victim, bool physical);
	static void finishSoulHarvest(const SoulHarvestHit & eligibility,
		battle::CUnitState & actor, const battle::CUnitState & victim);
	static std::pair<newHorizonsArmorer::ArmorerLastStandDamageResult, BattleSide> resolveUnspentLastStand(
		const CBattleInfoCallback & state, const BattleAttackInfo & attack, const battle::Unit * target, int64_t damage);
	static void applyUnspentLastStand(HypotheticBattle & state, battle::CUnitState * target,
		const newHorizonsArmorer::ArmorerLastStandDamageResult & result, BattleSide side, bool retaliation, bool apply = true);
	static bool hasGambler(const CBattleInfoCallback & state);
	static bool hasPerfectFortune(const CBattleInfoCallback & state);
	static bool hasSecondChance(const CBattleInfoCallback & state, const battle::Unit * unit, bool physicalDamage);
	static bool hasDefiant(const CBattleInfoCallback & state);
	static bool hasLuckSerendipity(const CBattleInfoCallback & state);
	static bool hasReadyLastStand(const CBattleInfoCallback & state);
	AttackPreviewHitContext(const std::shared_ptr<HypotheticBattle> & branch, bool & endedActivation);
	newHorizonsArmorer::ArmorerLastStandDamageResult resolveLastStandDamage(
		const BattleAttackInfo & attack, battle::CUnitState * target, int64_t damage) const;
	void applyLastStandDefend(battle::CUnitState * target, bool reactiveForLastStand) const;
	bool captureAndProjectFortuneStrike(const BattleAttackInfo & attack,
		const std::vector<std::pair<uint32_t, int64_t>> & hits,
		battle::CUnitState * actor, FortuneStrikeProjection & receipt) const;
};
