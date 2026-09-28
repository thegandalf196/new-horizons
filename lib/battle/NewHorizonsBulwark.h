/*
 * NewHorizonsBulwark.h, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once

#include <cstdint>
#include <string_view>

class CGHeroInstance;
namespace battle
{
class CUnitState;
}

namespace newHorizonsBulwark
{
constexpr std::string_view SKILL_ID = "new-horizons:bulwarkOfTheMire";
constexpr std::string_view MIREBORN_ID = "new-horizons:bulwarkOfTheMire.mireborn";
constexpr std::string_view THICK_HIDE_ID = "new-horizons:bulwarkOfTheMire.thickHide";
constexpr std::string_view BOG_AMBUSH_ID = "new-horizons:bulwarkOfTheMire.bogAmbush";
constexpr std::string_view DEEP_BULWARK_ID = "new-horizons:bulwarkOfTheMire.deepBulwark";
constexpr std::string_view SWAMP_RENEWAL_ID = "new-horizons:bulwarkOfTheMire.swampRenewal";
constexpr std::string_view MIRE_GRIP_ID = "new-horizons:bulwarkOfTheMire.mireGrip";
constexpr std::string_view TOXIC_SPINES_ID = "new-horizons:bulwarkOfTheMire.toxicSpines";
constexpr std::string_view SHARED_COVER_ID = "new-horizons:bulwarkOfTheMire.sharedCover";
constexpr std::string_view IMMOVABLE_ID = "new-horizons:bulwarkOfTheMire.immovable";
constexpr std::string_view VENGEFUL_MIRE_ID = "new-horizons:bulwarkOfTheMire.vengefulMire";

/// Percentages are represented in basis points so Advanced's 7.5% base and
/// 0.15% per Defense remain exact throughout the server damage pipeline.
constexpr int BASIS_POINTS_PER_PERCENT = 100;

DLL_LINKAGE int rank(const CGHeroInstance * hero);
DLL_LINKAGE bool hasMireborn(const CGHeroInstance * hero);
DLL_LINKAGE bool hasThickHide(const CGHeroInstance * hero);
DLL_LINKAGE bool hasBogAmbush(const CGHeroInstance * hero);
DLL_LINKAGE bool hasDeepBulwark(const CGHeroInstance * hero);
DLL_LINKAGE bool hasSwampRenewal(const CGHeroInstance * hero);
DLL_LINKAGE bool hasMireGrip(const CGHeroInstance * hero);
DLL_LINKAGE bool hasToxicSpines(const CGHeroInstance * hero);
DLL_LINKAGE bool hasSharedCover(const CGHeroInstance * hero);
DLL_LINKAGE bool hasImmovable(const CGHeroInstance * hero);
DLL_LINKAGE bool hasVengefulMire(const CGHeroInstance * hero);
DLL_LINKAGE int reductionBasisPoints(int rank, int heroDefense, bool mireTerrain);
DLL_LINKAGE int preemptivePercent(int rank);
DLL_LINKAGE int preemptivePercent(int rank, bool bogAmbush);
DLL_LINKAGE int reflectionPercent(int rank);
DLL_LINKAGE int reflectionBasisPoints(int rank, bool ranged, bool thickHide);
DLL_LINKAGE int reflectionBasisPoints(int rank, bool ranged, bool thickHide, bool vengefulMire);
DLL_LINKAGE int sharedCoverBasisPoints(int reductionBasisPoints);
/// Consume accumulated Defend damage and heal only the surviving unit's current HP.
DLL_LINKAGE int64_t applySwampRenewal(battle::CUnitState * stack, const CGHeroInstance * hero);
DLL_LINKAGE int64_t reflectedDamage(int64_t actualHealthLoss, int reflectionBasisPoints);
DLL_LINKAGE int64_t toxicSpinesPoisonBase(int64_t actualReflectedHealthLoss);
DLL_LINKAGE bool applyPhysicalPoison(battle::CUnitState * target, int64_t baseDamage, int32_t sourceStackId);
DLL_LINKAGE int64_t physicalPoisonTickDamage(const battle::CUnitState * target);
DLL_LINKAGE void advancePhysicalPoison(battle::CUnitState * target);
DLL_LINKAGE void clearPhysicalPoison(battle::CUnitState * target);
}
