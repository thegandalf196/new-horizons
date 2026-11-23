/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "../bonuses/Bonus.h"
#include "BattleSide.h"
#include "BattleHex.h"

#include <optional>

class CBattleInfoCallback;
class CGHeroInstance;
struct BattleAttackInfo;
namespace battle { class Unit; }
namespace spells { class Spell; }

/// Combat rules of the revised Rampart hero specialties (captured revisedSpecialties,
/// format 1166). Shared by the authority, the client and the battle AI; the server
/// applies the cast riders in BattleRampartSpecialtyCasts.
namespace newHorizonsRampart
{
inline constexpr auto BRITTLE_STACKING = "new-horizons:rampart-specialty:brittle";
inline constexpr auto LIVING_REMEDY_STACKING = "new-horizons:rampart-specialty:living-remedy";
inline constexpr auto AGAINST_THE_ODDS_STACKING = "new-horizons:rampart-specialty:against-the-odds";

/// Mephala: a blocking battlefield obstacle, a standing wall section or a closed gate
/// is next to the unit at `position` (its current position when invalid).
DLL_LINKAGE bool besideObstacleOrWall(const CBattleInfoCallback & battle, const battle::Unit & unit,
	BattleHex position = BattleHex::INVALID);
/// Use the Ground: physical protection of this attack's defender, in basis points.
DLL_LINKAGE int useTheGroundBasisPoints(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Rank steps the defender stands above the attacker (Core, Elite, Champion); 0 if unranked.
DLL_LINKAGE int rankSteps(const CBattleInfoCallback & battle, const battle::Unit & attacker, const battle::Unit & defender);
/// Giant-Slayer, Brittle and Ivor's Forest Ambush shot on one creature attack (1 when none applies).
DLL_LINKAGE double attackDamageMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack);
/// Coronius's Holy Wrath against `target`: +per-step damage for each rank above Core.
DLL_LINKAGE int64_t holyWrathDamage(const CBattleInfoCallback & battle, const CGHeroInstance * caster,
	const spells::Spell & spell, const battle::Unit & target, int64_t damage);

/// Alagar's ice spells: Ice Bolt and Frost Ring.
DLL_LINKAGE bool isIceSpell(SpellID spell);
/// The Brittle mark his ice spell leaves on an enemy stack it hit, until its next activation.
DLL_LINKAGE std::optional<Bonus> brittleMarker(const CGHeroInstance * hero);
DLL_LINKAGE bool isBrittleMarker(const Bonus * bonus);
/// Extra physical damage the unit takes while Brittle, in permille (0 when not Brittle).
DLL_LINKAGE int brittlePermille(const battle::Unit & unit);

/// Uland's Living Remedy Regeneration for a stack his Cure reached, or nothing.
DLL_LINKAGE std::optional<Bonus> livingRemedy(const CGHeroInstance * hero, const battle::Unit & unit);

/// Gem's Sorceress of Enroth: this hero cast is her first this combat and within her level.
DLL_LINKAGE bool sorceressCastIsFree(const CBattleInfoCallback & battle, const CGHeroInstance & caster,
	const spells::Spell & spell);

/// Aeris's revised Perfect Mount on this Pegasus, for magical creature attacks (the
/// spell path reads the same SPELL_DAMAGE_REDUCTION bonus); 0 when absent.
DLL_LINKAGE int perfectMountReductionPercent(const battle::Unit & unit);

/// Total creature value of one side's army (war machines and turrets excluded).
DLL_LINKAGE int64_t armyValue(const CBattleInfoCallback & battle, BattleSide side);
/// Melodia's Against the Odds Luck for this side when its enemy is stronger, or nothing.
DLL_LINKAGE std::optional<Bonus> againstTheOdds(const CBattleInfoCallback & battle, BattleSide side);

/// True when this side's Rampart hero makes all cached pair damage depend on state: Use the
/// Ground on a field with obstacles or walls, or Ivor's Elves that can shoot from hiding.
DLL_LINKAGE bool heroDamageIsStateful(const CBattleInfoCallback & battle, BattleSide side, const CGHeroInstance * hero);
}
