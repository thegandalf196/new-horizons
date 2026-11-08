/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "NewHorizonsOrdinaryStrike.h"
#include "BattleHex.h"
#include "../spells/ProxyCaster.h"
class CBattleInfoCallback;
class CGHeroInstance;
struct BattleAttackInfo;
namespace newHorizonsAttackControllers
{
/// Ephemeral accepted-primary receipt, prepared before the attack packet.
struct DLL_LINKAGE Strike
{
	uint32_t source = 0;
	uint32_t target = 0;
	BattleHex impact;
	bool fireball = false;
	bool bloodlust = false;
	newHorizonsOrdinaryStrike::EffectPlan cripple;
};
inline constexpr int BLOODLUST_LEVEL = 2;
inline constexpr int BLOODLUST_DURATION = 3;
/// Loaded native non-cumulative Bloodlust effects, without recipient stamping.
DLL_LINKAGE std::vector<Bonus> bloodlustBonusPlan();
DLL_LINKAGE bool barrageKillQualifies(const CBattleInfoCallback &, const battle::Unit & source,
	const battle::Unit & victim, int64_t actualKilledCount);
/// Same accepted recipient and impact only; requires native CUnitState activation state.
DLL_LINKAGE bool barrageRepeatAllowed(const CBattleInfoCallback &, const battle::Unit * source,
	const battle::Unit * target, uint32_t capturedTargetId, BattleHex impact);
DLL_LINKAGE int barrageDamagePercent(const CBattleInfoCallback &, const battle::Unit &);
DLL_LINKAGE int barrageShots(const CBattleInfoCallback &, const battle::Unit &);
DLL_LINKAGE int ballistaPhysicalDamagePercent(const CBattleInfoCallback &, const battle::Unit &);
DLL_LINKAGE Strike captureStrike(const CBattleInfoCallback &, const BattleAttackInfo &, int64_t primaryDamage);
DLL_LINKAGE bool isCripple(const Bonus *);
DLL_LINKAGE std::vector<Bonus> crippleRemoval(const battle::Unit &);
DLL_LINKAGE std::vector<uint32_t> bloodlustRecipients(const CBattleInfoCallback &, const battle::Unit &);
/// Creature-triggered PASSIVE spell: hero output and school mastery, creature
/// ownership/provenance, no Mana or manual Hero Spell Action.
class DLL_LINKAGE BallistaCaster final : public spells::ProxyCaster
{
	const battle::Unit & source;
public:
	BallistaCaster(const battle::Unit &, const CGHeroInstance &);
	int32_t getCasterUnitId() const override;
	PlayerColor getCasterOwner() const override;
	void spendMana(ServerCallback *, int32_t) const override;
};
}
