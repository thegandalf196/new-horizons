/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../../Global.h"
#include "BattleSide.h"
#include "../bonuses/Bonus.h"
#include <cstdint>
#include <vector>

class CBattleInfoCallback;
class CGHeroInstance;
class IBattleState;
struct BattleAttackInfo;
namespace battle { class Unit; }

namespace arcaneInspiration
{
inline constexpr auto SKILL = "new-horizons:warcasting";
enum Rider : uint32_t
{
	VALOR = 1, FOCUS = 2, BULWARK = 4, WEAPONS = 8,
	LINGERING = 16, REPRISAL = 32, SUPREMACY = 64
};
struct DLL_LINKAGE Profile
{
	int32_t corePercent = 0;
	uint32_t riders = 0;
	bool spellshock = false;
	bool operator==(const Profile &) const = default;
};
DLL_LINKAGE int32_t corePercent(int32_t rank, bool grandInspiration);
DLL_LINKAGE Profile profile(const CGHeroInstance * hero);
DLL_LINKAGE int32_t effectiveCorePercent(const CGHeroInstance * currentOwnerHero, int32_t basePercent);
/// Native bonus values are the complete saved/branch-owned effect state.
DLL_LINKAGE Bonus activeMarker(Profile policy, PlayerColor owner, int32_t round);
DLL_LINKAGE Bonus valorMarker(PlayerColor owner, int32_t round);
DLL_LINKAGE Bonus spellshockMarker(PlayerColor casterOwner, int32_t round);
DLL_LINKAGE Bonus retainedMarker(const Bonus & active);
DLL_LINKAGE bool isEffect(const Bonus * bonus);
DLL_LINKAGE bool isActive(const Bonus * bonus);
DLL_LINKAGE bool isRetained(const Bonus * bonus);
DLL_LINKAGE bool isSpellshock(const Bonus * bonus);
DLL_LINKAGE Profile activeProfile(const battle::Unit & unit, PlayerColor owner);
DLL_LINKAGE int32_t retainedPercent(const battle::Unit & unit, PlayerColor owner);
/// Current target-side physical protection from the active Bulwark rider.
DLL_LINKAGE int32_t physicalReductionPercent(const battle::Unit & unit, PlayerColor owner);
struct DLL_LINKAGE AttackPlan
{
	int32_t damageBonusPercent = 0;
	int32_t outputPenaltyPercent = 0;
	int32_t defenseIgnorePercent = 0;
	int32_t physicalReductionPercent = 0;
	int32_t protectionIgnorePercent = 0;
};
DLL_LINKAGE AttackPlan attackPlan(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Called at the beginning of the next combat round, independently of turn pauses.
DLL_LINKAGE void advanceRound(IBattleState & battle, int32_t nextRound);
DLL_LINKAGE int64_t reprisalRawDamage(int64_t inflictedDamage);
}
