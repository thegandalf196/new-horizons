/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../bonuses/Bonus.h"
#include "BattleSide.h"

#include <optional>

class CBattleInfoCallback;
class CGHeroInstance;
struct BattleAttackInfo;
namespace battle { class Unit; }

/// Combat rules of the revised Academy hero specialties (captured revisedSpecialties,
/// format 1166). Shared by the authority, the client and the battle AI; the server
/// applies Neela's per-hit marks and Astral's practice in BattleAcademySpecialties.
namespace newHorizonsAcademy
{
inline constexpr auto HARDENED_STACKING = "new-horizons:academy-specialty:hardened";

/// Neela's Hardened mark on one of her stacks after it was hit: the reduction it already
/// holds plus one hit's share, up to her level total. Lasts until the stack next acts.
DLL_LINKAGE std::optional<Bonus> hardenedMarker(const CGHeroInstance * hero, const battle::Unit & unit);
DLL_LINKAGE bool isHardenedMarker(const Bonus * bonus);
/// Damage reduction the unit's Hardened mark gives against creature attacks, in permille.
DLL_LINKAGE int hardenedPermille(const battle::Unit & unit);
/// Fafner's extra damage when a Naga stack strikes a target it has bound, in percent (0 otherwise).
DLL_LINKAGE int serpentsEmbraceBoundPercent(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Hardened and Serpent's Embrace on one creature attack (1 when neither applies).
DLL_LINKAGE double attackDamageMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
}
