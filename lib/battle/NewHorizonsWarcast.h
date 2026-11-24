/*
 * NewHorizonsWarcast.h, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#pragma once

#include "HeroCommand.h"
#include "BattleSide.h"

class CGHeroInstance;
class CBattleInfoCallback;
struct Bonus;
namespace battle { class Unit; }
namespace spells { class Mechanics; }

/// Warcasting (new-horizons:warcast): Orders are stronger on friendly stacks that
/// already carry the issuing hero's beneficial spells. The recipients and their
/// share are captured once, when the Order is committed, in HeroOrderState.
/// Every check below reads that plain snapshot; battles without the skill keep
/// empty snapshots and pay one empty-vector test.
namespace newHorizonsWarcast
{
inline constexpr const char * SKILL = "new-horizons:warcast";
inline constexpr const char * FIELD_ENCHANTER = "new-horizons:warcast.fieldEnchanter";
inline constexpr const char * KINDLED_EDGE = "new-horizons:warcast.kindledEdge";
inline constexpr const char * COMMANDERS_MEDITATION = "new-horizons:warcast.commandersMeditation";
inline constexpr const char * LAYERED_ENCHANTMENT = "new-horizons:warcast.layeredEnchantment";
inline constexpr const char * SUSTAINING_COMMAND = "new-horizons:warcast.sustainingCommand";
inline constexpr const char * OPENING = "new-horizons:warcast.opening";
inline constexpr const char * WARDED_RANKS = "new-horizons:warcast.wardedRanks";
inline constexpr const char * ENCHANTED_STEEL = "new-horizons:warcast.enchantedSteel";
inline constexpr const char * GRAND_CONVERGENCE = "new-horizons:warcast.grandConvergence";
inline constexpr const char * UNBROKEN_COMMAND = "new-horizons:warcast.unbrokenCommand";

inline constexpr int LAYERED_BONUS_POINTS = 10;
inline constexpr int GRAND_BONUS_POINTS = 15;
inline constexpr int KINDLED_DAMAGE_PERCENT = 5;
inline constexpr int WARDED_REDUCTION_BASIS_POINTS = 1500;
inline constexpr int STEEL_DEFENSE_IGNORE_PERCENT = 15;
inline constexpr int OPENING_SPELL_DAMAGE_PERCENT = 20;
inline constexpr int MEDITATION_MANA = 3;
inline constexpr int FIELD_ENCHANTER_MANA = 2;

/// Base share of the Order effect by Warcasting rank (0 when untrained).
DLL_LINKAGE int rankPercent(int rank);
DLL_LINKAGE int skillRank(const CGHeroInstance * hero);
DLL_LINKAGE bool hasPerk(const CGHeroInstance * hero, const char * perk);

/// Number of different beneficial spells cast by `owner` on the unit, stopping at
/// `limit`. Creature on-hit abilities are excluded; prayers and specialty marks are
/// not spell effects.
DLL_LINKAGE int ownBeneficialSpellCount(const battle::Unit & unit, PlayerColor owner, int limit = 2);
DLL_LINKAGE bool carriesOwnHostileSpell(const battle::Unit & unit, PlayerColor owner);

/// Fill the warcast snapshot of `order` from its recipients. No-op without the skill.
DLL_LINKAGE void capture(const CBattleInfoCallback & battle, BattleSide side, HeroOrderState & order,
	const std::vector<uint32_t> & recipients);

/// Add `percent` percent of `value`, rounding toward zero.
DLL_LINKAGE int scaled(int value, int percent);
DLL_LINKAGE double scaled(double value, int percent);

/// The unit is a warcast recipient of a scheduled Order whose snapshot carries `perk`.
DLL_LINKAGE bool riderActive(const std::vector<HeroOrderState> & states, uint32_t unitId,
	int32_t round, uint8_t perk);
DLL_LINKAGE bool riderActive(const CBattleInfoCallback & battle, const battle::Unit * unit, uint8_t perk);
/// Kindled Edge (Order damage stage) and Enchanted Steel (Enchanted Weapons' stage), once per attack.
DLL_LINKAGE void addAttackRiders(const std::vector<HeroOrderState> & states, uint32_t attacker, int32_t round,
	int & orderDamagePercent, int & defenseIgnorePercent);
/// Unbroken Command, Iron Will or Divine Discipline must authorize a captured carry ledger.
DLL_LINKAGE bool carryAuthorized(const HeroOrderState & order, const CGHeroInstance * hero);

/// Warded Ranks basis points for a defender, added to Spellward's stage.
DLL_LINKAGE int wardedRanksBasisPoints(const CBattleInfoCallback & battle, const battle::Unit * unit);

/// Opening's Exposed marker on an enemy of `side`, until the end of the next round.
DLL_LINKAGE Bonus exposedMarker(BattleSide side);
DLL_LINKAGE bool isExposedMarker(const Bonus * bonus, BattleSide side);
DLL_LINKAGE bool isExposedMarker(const Bonus * bonus);
/// Extra hero-spell damage percent against an Exposed enemy for an Opening hero.
DLL_LINKAGE int openingSpellDamagePercent(const CBattleInfoCallback & battle, const CGHeroInstance * hero,
	const battle::Unit * recipient);

/// Field Enchanter: reduced Mana cost for a beneficial hero spell whose target
/// stack is under one of the caster's active Orders.
DLL_LINKAGE int32_t fieldEnchanterCost(const spells::Mechanics & mechanics, const battle::Unit * target, int32_t cost);

/// Sustaining Command marker: one per spell family, lasting as long as the sustained effect.
DLL_LINKAGE Bonus sustainedMarker(const Bonus & effect);
DLL_LINKAGE bool isSustainedMarker(const Bonus * bonus);
}
