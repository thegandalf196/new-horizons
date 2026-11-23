/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsRampartSpecialties.h"
#include "BattleAccessibilityQuery.h"
#include "BattleAttackInfo.h"
#include "CBattleInfoCallback.h"
#include "CObstacleInstance.h"
#include "IBattleState.h"
#include "NewHorizonsForestAmbush.h"
#include "Unit.h"
#include "../CCreatureHandler.h"
#include "../entities/building/TownFortifications.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/NewHorizonsRampartSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/CSpell.h"

#include <array>

namespace newHorizonsRampart
{
namespace
{
constexpr std::array<int, 6> INDESTRUCTIBLE_WALL_HEXES{12, 45, 62, 112, 147, 165};

const newHorizonsHeroes::RampartSpecialtyRules * captured(const CGHeroInstance * hero)
{
	return hero && hero->compiledDevelopment() ? &hero->compiledDevelopment()->rampartSpecialties : nullptr;
}

bool wallHex(const CBattleInfoCallback & battle, BattleHex hex)
{
	if(std::ranges::find(INDESTRUCTIBLE_WALL_HEXES, hex.toInt()) != INDESTRUCTIBLE_WALL_HEXES.end())
		return true;
	if(hex.toInt() == BattleHex::GATE_OUTER || hex.toInt() == BattleHex::GATE_INNER)
		return battle.battleGetGateState() == EGateState::CLOSED || battle.battleGetGateState() == EGateState::BLOCKED;
	for(const auto & [part, position] : battle::accessibilityWallParts())
		if(position == hex)
			return battle.battleGetWallState(part) != EWallState::DESTROYED;
	return false;
}

bool hasWalls(const CBattleInfoCallback & battle)
{
	return battle.battleGetFortifications().wallsHealth > 0;
}

int rank(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	const auto * state = battle.getBattle();
	return state ? newHorizonsHeroes::specialtyRank(state->getCreatureCategoryRules(), unit.creatureId()) : -1;
}

bool ordinaryCreature(const battle::Unit & unit)
{
	return unit.alive() && !unit.isGhost() && !unit.isTurret() && !unit.hasBonusOfType(BonusType::SIEGE_WEAPON);
}
}

bool besideObstacleOrWall(const CBattleInfoCallback & battle, const battle::Unit & unit, BattleHex position)
{
	const bool walls = hasWalls(battle);
	for(const auto & hex : unit.getSurroundingHexes(position))
	{
		if(walls && wallHex(battle, hex))
			return true;
		if(!battle.battleGetAllObstaclesOnPos(hex, true).empty())
			return true;
	}
	return false;
}

int useTheGroundBasisPoints(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.physicalDamage || !attack.defender || !attack.defender->alive())
		return 0;
	const int permille = newHorizonsHeroes::useTheGroundPermille(battle.battleGetOwnerHero(attack.defender));
	if(permille <= 0)
		return 0;
	const auto position = attack.defenderPos.isValid() ? attack.defenderPos : attack.defender->getPosition();
	return besideObstacleOrWall(battle, *attack.defender, position) ? permille * 10 : 0;
}

int rankSteps(const CBattleInfoCallback & battle, const battle::Unit & attacker, const battle::Unit & defender)
{
	const int own = rank(battle, attacker);
	const int target = rank(battle, defender);
	return own < 0 || target < 0 ? 0 : std::max(0, target - own);
}

double attackDamageMultiplier(const CBattleInfoCallback & battle, const BattleAttackInfo & attack)
{
	if(!attack.attacker || !attack.defender)
		return 1.0;
	double result = 1.0;
	const auto * attackingHero = battle.battleGetOwnerHero(attack.attacker);
	if(const int perStep = newHorizonsHeroes::giantSlayerPermillePerStep(attackingHero))
		result *= 1.0 + perStep * rankSteps(battle, *attack.attacker, *attack.defender) / 1000.0;
	if(attack.physicalDamage)
		if(const int brittle = brittlePermille(*attack.defender))
			result *= 1.0 + brittle / 1000.0;
	if(attack.physicalDamage && attack.shooting)
		if(const int ambush = newHorizonsHeroes::forestAmbushShotPercent(attackingHero);
			ambush > 0 && newHorizonsForestAmbush::shotLuck(battle, attack.attacker, true) > 0)
			result *= 1.0 + ambush / 100.0;
	return result;
}

int64_t holyWrathDamage(const CBattleInfoCallback & battle, const CGHeroInstance * caster,
	const spells::Spell & spell, const battle::Unit & target, int64_t damage)
{
	const int perStep = newHorizonsHeroes::giantSlayerPermillePerStep(caster);
	if(perStep <= 0 || damage <= 0 || spell.getJsonKey() != "new-horizons:holyWrath")
		return damage;
	const int steps = std::max(0, rank(battle, target));
	if(steps == 0)
		return damage;
	const int64_t boosted = damage * (1000 + int64_t{perStep} * steps) / 1000;
	return boosted < damage ? std::numeric_limits<int64_t>::max() : boosted;
}

bool isIceSpell(SpellID spell)
{
	return spell == SpellID::ICE_BOLT || spell == SpellID::FROST_RING;
}

std::optional<Bonus> brittleMarker(const CGHeroInstance * hero)
{
	const int permille = newHorizonsHeroes::brittlePermille(hero);
	if(permille <= 0)
		return std::nullopt;
	Bonus marker(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION, BonusType::NONE, BonusSource::HERO_SPECIAL,
		permille, BonusSourceID(hero->getHeroTypeID()));
	marker.stacking = BRITTLE_STACKING;
	marker.description.appendTextID("new-horizons.rampart.brittle.effect");
	return marker;
}

bool isBrittleMarker(const Bonus * bonus)
{
	return bonus && bonus->type == BonusType::NONE && bonus->stacking == BRITTLE_STACKING;
}

int brittlePermille(const battle::Unit & unit)
{
	if(!unit.mayHaveBonusType(BonusType::NONE))
		return 0;
	static const CSelector selector = Selector::type()(BonusType::NONE).And(CSelector(isBrittleMarker));
	const auto held = unit.getBonuses(selector, "new-horizons:rampart-specialty:brittle");
	int result = 0;
	for(const auto & bonus : *held)
		result = std::max(result, bonus->val);
	return result;
}

std::optional<Bonus> livingRemedy(const CGHeroInstance * hero, const battle::Unit & unit)
{
	const int basisPoints = newHorizonsHeroes::livingRemedyBasisPoints(hero);
	if(basisPoints <= 0 || !unit.alive() || unit.isGhost())
		return std::nullopt;
	const int64_t fullHealth = int64_t{unit.getCount()} * unit.getMaxHealth();
	const int64_t healing = std::max<int64_t>(1, fullHealth * basisPoints / 10000);
	Bonus regeneration(BonusDuration::N_TURNS, BonusType::HP_REGENERATION, BonusSource::HERO_SPECIAL,
		static_cast<int32_t>(std::min<int64_t>(healing, std::numeric_limits<int32_t>::max())),
		BonusSourceID(hero->getHeroTypeID()));
	regeneration.turnsRemain = newHorizonsHeroes::livingRemedyRounds(hero);
	regeneration.stacking = LIVING_REMEDY_STACKING;
	regeneration.description.appendTextID("new-horizons.rampart.livingRemedy.effect");
	return regeneration;
}

bool sorceressCastIsFree(const CBattleInfoCallback & battle, const CGHeroInstance & caster, const spells::Spell & spell)
{
	const int maximumLevel = newHorizonsHeroes::sorceressFreeSpellLevel(&caster);
	const auto * state = battle.getBattle();
	if(maximumLevel <= 0 || !state || !spell.isCombat())
		return false;
	const auto side = battle.playerToSide(caster.tempOwner);
	if((side != BattleSide::ATTACKER && side != BattleSide::DEFENDER) || state->getSideHero(side) != &caster
		|| state->hasCompletedHeroSpellCast(side))
		return false;
	return battle.battleGetSpellLevel(spell.getId()) <= maximumLevel;
}

int perfectMountReductionPercent(const battle::Unit & unit)
{
	if(!unit.mayHaveBonusType(BonusType::SPELL_DAMAGE_REDUCTION))
		return 0;
	static const std::string stacking = "new-horizons:ordinary-passive:core:aeris:magicalImmunity";
	static const CSelector selector = Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(SpellSchool::ANY))
		.And(CSelector([](const Bonus * bonus) { return bonus && bonus->stacking == stacking; }));
	return std::clamp(unit.valOfBonuses(selector, "new-horizons:rampart-specialty:perfect-mount"), 0, 100);
}

int64_t armyValue(const CBattleInfoCallback & battle, BattleSide side)
{
	int64_t total = 0;
	for(const auto * unit : battle.battleGetUnitsIf([side](const battle::Unit * candidate)
		{ return candidate->unitSide() == side && ordinaryCreature(*candidate); }))
		if(const auto * creature = unit->creatureId().toCreature())
			total += int64_t{creature->getAIValue()} * unit->getCount();
	return total;
}

std::optional<Bonus> againstTheOdds(const CBattleInfoCallback & battle, BattleSide side)
{
	const auto * hero = battle.battleGetFightingHero(side);
	const int luck = newHorizonsHeroes::againstTheOddsLuck(hero);
	if(luck <= 0 || armyValue(battle, CBattleInfoEssentials::otherSide(side)) <= armyValue(battle, side))
		return std::nullopt;
	Bonus bonus(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::HERO_SPECIAL, luck,
		BonusSourceID(hero->getHeroTypeID()));
	bonus.stacking = AGAINST_THE_ODDS_STACKING;
	bonus.description.appendTextID("new-horizons.rampart.againstTheOdds.effect");
	return bonus;
}

bool heroDamageIsStateful(const CBattleInfoCallback & battle, BattleSide side, const CGHeroInstance * hero)
{
	const auto * rules = captured(hero);
	if(!rules)
		return false;
	// Use the Ground depends on positions and on obstacles that can appear or vanish
	// (Force Field, Remove Obstacle), so a pair entry is never reused while she fights.
	if(rules->useTheGroundPermille && newHorizonsHeroes::useTheGroundPermille(hero) > 0)
		return true;
	if(rules->forestAmbushShotPercent && newHorizonsHeroes::forestAmbushShotPercent(hero) > 0)
		for(const auto * unit : battle.battleGetUnitsIf([side](const battle::Unit * candidate)
			{ return candidate->unitSide() == side && candidate->alive(); }))
			if(newHorizonsForestAmbush::entitled(battle, unit))
				return true;
	// Brittle marks are tracked per unit with the other temporary combat modifiers.
	return false;
}
}
