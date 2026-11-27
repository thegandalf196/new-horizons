/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "../constants/EntityIdentifiers.h"
#include "../battle/BattleSide.h"
class CGHeroInstance;
class IBattleInfo;
struct Bonus;
class ServerCallback;
struct SetStackEffect;
class CBattleInfoCallback;
namespace battle { class Unit; }
namespace spells { class Mechanics; }
namespace infernoSpellSpecialties
{
DLL_LINKAGE void preserveRefreshChronology(const CBattleInfoCallback &, SetStackEffect &);
DLL_LINKAGE int kindlingBonusBasisPoints(const IBattleInfo &, BattleSide, SpellID);
DLL_LINKAGE bool bloodfireProtects(const spells::Mechanics &, const battle::Unit &);
/// Applies Bloodfire's friendly Attack or Burn Away's enemy dispel after a Fireball hit.
DLL_LINKAGE void applyFireballRider(ServerCallback &, const spells::Mechanics &, const battle::Unit &);
}
