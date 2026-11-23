/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "RampartSpecialtyValue.h"
#include "AttackPossibility.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsRampartSpecialties.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/entities/hero/NewHorizonsRampartSpecialties.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/CSpell.h"

#include <set>

namespace rampartSpecialtyAI
{
namespace
{
constexpr float unevaluated = std::numeric_limits<float>::lowest() / 2;
/// Regeneration heals wounds only, so half of what it could restore is credited.
constexpr double REGENERATION_REALIZED = 0.5;
/// A cancelled Defend no longer absorbs this share of the blows the stack then takes.
constexpr double DEFEND_SHARE = 0.2;

/// Stacks the candidate reaches: its single target, the units on a Frost Ring around
/// its centre, or every friendly stack of a mass Cure.
std::vector<uint32_t> reached(const CBattleInfoCallback & battle, BattleSide side, const PossibleSpellcast & candidate)
{
	std::vector<uint32_t> result;
	if(candidate.dest.empty())
	{
		if(candidate.spell->id == SpellID::CURE)
			for(const auto * unit : battle.battleGetUnitsIf([side](const battle::Unit * other)
				{ return other->unitSide() == side && other->alive() && !other->isGhost(); }))
				result.push_back(unit->unitId());
		return result;
	}
	const auto & destination = candidate.dest.front();
	if(candidate.spell->id == SpellID::FROST_RING && destination.hexValue.isValid())
	{
		std::set<uint32_t> seen;
		for(const auto & hex : destination.hexValue.getNeighbouringTiles())
			if(const auto * unit = battle.battleGetUnitByPos(hex, true); unit && seen.insert(unit->unitId()).second)
				result.push_back(unit->unitId());
		return result;
	}
	if(destination.unitValue)
		result.push_back(destination.unitValue->unitId());
	else if(const auto * unit = battle.battleGetUnitByPos(destination.hexValue, true))
		result.push_back(unit->unitId());
	return result;
}

/// Regeneration only matters for a stack that is already wounded or that an enemy can
/// hit before the effect runs out: a shooter, or a walker within reach of its hex.
bool needsRegeneration(const CBattleInfoCallback & battle, const battle::Unit & target)
{
	if(target.getFirstHPleft() < target.getMaxHealth())
		return true;
	for(const auto * enemy : battle.battleGetUnitsIf([&target](const battle::Unit * candidate)
		{ return candidate->unitSide() != target.unitSide() && candidate->alive() && !candidate->isGhost(); }))
	{
		if(battle.battleCanShoot(enemy))
			return true;
		const auto distance = BattleHex::getDistance(enemy->getPosition(), target.getPosition());
		if(distance <= static_cast<int>(enemy->getMovementRange()) + 1)
			return true;
	}
	return false;
}
}

float livingRemedyValue(const CBattleInfoCallback & battle, BattleSide side, uint32_t unit)
{
	const auto * target = battle.battleGetUnitByID(unit);
	if(!target || !target->alive() || target->isGhost() || target->unitSide() != side)
		return 0.0f;
	const auto regeneration = newHorizonsRampart::livingRemedy(battle.battleGetFightingHero(side), *target);
	if(!regeneration || !needsRegeneration(battle, *target))
		return 0.0f;
	// Each round heals at most the top creature's wound.
	const double perRound = std::min<double>(regeneration->val, target->getMaxHealth());
	return static_cast<float>(perRound * regeneration->turnsRemain * REGENERATION_REALIZED);
}

float brittleValue(const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & board,
	BattleSide side, uint32_t unit, DamageCache & damage)
{
	const int permille = newHorizonsHeroes::brittlePermille(battle.battleGetFightingHero(side));
	const auto * target = board ? board->battleGetUnitByID(unit) : nullptr;
	if(permille <= 0 || !target || !target->alive() || target->isGhost() || target->unitSide() == side)
		return 0.0f;
	// The mark lasts until the target's next activation: count the blows this
	// side's stacks that still act this round can deliver to it.
	double incoming = 0;
	for(const auto * own : board->battleGetUnitsIf([side](const battle::Unit * candidate)
		{ return candidate->unitSide() == side && candidate->alive() && !candidate->isGhost(); }))
		if(own->willMove(0))
			incoming += damage.getDamage(own, target, board);
	incoming = std::min<double>(incoming, target->getAvailableHealth());
	double value = incoming * permille / 1000.0;
	if(target->defended())
		value += incoming * DEFEND_SHARE;
	return static_cast<float>(value);
}

float riderValue(const CBattleInfoCallback & battle, const std::shared_ptr<CBattleInfoCallback> & board,
	BattleSide side, const PossibleSpellcast & candidate, DamageCache & damage)
{
	if(candidate.command != HeroCommand::NONE || !candidate.spell)
		return 0.0f;
	const auto * hero = battle.battleGetFightingHero(side);
	const bool remedy = candidate.spell->id == SpellID::CURE && newHorizonsHeroes::livingRemedyBasisPoints(hero) > 0;
	const bool brittle = newHorizonsRampart::isIceSpell(candidate.spell->id) && newHorizonsHeroes::brittlePermille(hero) > 0;
	if(!remedy && !brittle)
		return 0.0f;
	float value = 0.0f;
	for(const auto id : reached(battle, side, candidate))
		value += remedy ? livingRemedyValue(battle, side, id) : brittleValue(battle, board, side, id, damage);
	return value;
}

void addSpecialtyValue(std::vector<PossibleSpellcast> & candidates, const CBattleInfoCallback & battle,
	const std::shared_ptr<CBattleInfoCallback> & board, BattleSide side, DamageCache & damage, float positive)
{
	if(!board || !battle.battleGetFightingHero(side))
		return;
	for(auto & candidate : candidates)
		if(candidate.value > unevaluated)
			if(const auto bonus = riderValue(battle, board, side, candidate, damage); bonus > 0)
				candidate.value += bonus * positive;
}
}
