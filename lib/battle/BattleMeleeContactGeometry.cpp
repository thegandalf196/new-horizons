/*
 * BattleMeleeContactGeometry.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"

namespace battle
{
/// Only a typed non-displacing excursion supplies this contact footprint.
/// Ordinary and hypothetical legacy callers retain their existing geometry.
struct MeleeContactFootprint
{
	const BattleHexArray & hexes;
};
}

AttackableTiles CBattleInfoCallback::getPotentiallyAttackableHexes(
	const battle::Unit* attacker,
	const battle::Unit * defender,
	BattleHex destinationTile,
	BattleHex attackerPos,
	BattleHex defenderPos,
	const battle::MeleeContactFootprint * contact) const
{
	//does not return hex attacked directly
	AttackableTiles at;
	RETURN_IF_NOT_BATTLE(at);

	BattleHex attackOriginHex = (attackerPos.toInt() != BattleHex::INVALID) ? attackerPos : attacker->getPosition(); //real or hypothetical (cursor) position

	defenderPos = (defenderPos.toInt() != BattleHex::INVALID) ? defenderPos : defender->getPosition(); //real or hypothetical (cursor) position

	if(attacker->hasBonusOfType(BonusType::ATTACKS_ALL_ADJACENT))
		at.hostileCreaturePositions.insert(attacker->getSurroundingHexes(attackerPos));

	// If attacker is double-wide and its head is not adjacent to enemy we need to turn around
	if(attacker->doubleWide() && !vstd::contains(defender->getSurroundingHexes(defenderPos), attackOriginHex))
		attackOriginHex = attacker->occupiedHex(attackOriginHex);

	auto attackDirection = BattleHex::mutualPosition(attackOriginHex, defenderPos);

	// If defender is double-wide, attacker always prefers targeting its 'tail', if it is reachable
	if(defender->doubleWide() && BattleHex::mutualPosition(attackOriginHex, defender->occupiedHex(defenderPos)) != BattleHex::NONE)
		attackDirection = BattleHex::mutualPosition(attackOriginHex, defender->occupiedHex(defenderPos));

	if(attackDirection == BattleHex::NONE)
		throw std::runtime_error("!!!");

	const auto coversContact = [attacker, contact](const BattleHex & hex)
	{
		return contact ? contact->hexes.contains(hex) : attacker->coversPos(hex);
	};

	const auto & processTargets = [&](const std::vector<int> & additionalTargets) -> BattleHexArray
	{
		BattleHexArray output;

		for (int targetPath : additionalTargets)
		{
			BattleHex target = attackOriginHex;
			std::vector<BattleHex::EDir> path;

			for (int targetPathLeft = targetPath; targetPathLeft != 0; targetPathLeft /= 10)
				path.push_back(static_cast<BattleHex::EDir>((attackDirection + targetPathLeft % 10 - 1) % 6));

			try
			{
				if(attacker->doubleWide() && coversContact(target.cloneInDirection(path.front())))
					target.moveInDirection(attackDirection);

				for(BattleHex::EDir nextDirection : path)
					target.moveInDirection(nextDirection);
			}
			catch(const std::out_of_range &)
			{
				// Hex out of range, for example outside of battlefield. This is valid situation, so skip this hex
				continue;
			}

			if (target.isValid() && !coversContact(target))
				output.insert(target);
		}
		return output;
	};

	const auto multihexUnit = attacker->getBonusesOfType(BonusType::MULTIHEX_UNIT_ATTACK);
	const auto multihexEnemy = attacker->getBonusesOfType(BonusType::MULTIHEX_ENEMY_ATTACK);
	const auto multihexAnimation = attacker->getBonusesOfType(BonusType::MULTIHEX_ANIMATION);

	for (const auto & bonus : *multihexUnit)
		at.friendlyCreaturePositions.insert(processTargets(bonus->parameters->toVector()));

	for (const auto & bonus : *multihexEnemy)
		at.hostileCreaturePositions.insert(processTargets(bonus->parameters->toVector()));

	for (const auto & bonus : *multihexAnimation)
		at.overrideAnimationPositions.insert(processTargets(bonus->parameters->toVector()));

	if(attacker->hasBonusOfType(BonusType::THREE_HEADED_ATTACK))
		at.hostileCreaturePositions.insert(processTargets({2,6}));

	if(attacker->hasBonusOfType(BonusType::WIDE_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11, 111, 2, 12, 6, 16 }));

	if(attacker->hasBonusOfType(BonusType::TWO_HEX_ATTACK_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11 }));

	if (attacker->hasBonusOfType(BonusType::PRISM_HEX_ATTACK_BREATH))
		at.friendlyCreaturePositions.insert(processTargets({ 11, 12, 16 }));

	return at;
}

std::pair<battle::Units, bool> CBattleInfoCallback::getAerialAttackedBattleUnits(
	const newHorizonsAerialSupremacy::Excursion & excursion) const
{
	newHorizonsAerialSupremacy::validate(*this, excursion);
	const auto * actor = battleGetUnitByID(excursion.actor);
	const auto * opponent = battleGetUnitByID(excursion.opponent);
	const battle::MeleeContactFootprint contact{actor->getHexes(excursion.landing)};
	const auto tiles = getPotentiallyAttackableHexes(actor, opponent, excursion.target,
		excursion.landing, excursion.target, &contact);
	std::pair<battle::Units, bool> result;
	const auto addOnce = [&](const BattleHex & hex, bool hostileOnly)
	{
		const auto * unit = battleGetUnitByPos(hex, true);
		if(!unit || unit->unitId() == excursion.actor || unit->isGhost() || unit->isInvincible()
			|| (hostileOnly && battleGetOwner(unit) == battleGetOwner(actor)))
			return;
		if(std::ranges::find(result.first, unit) == result.first.end())
			result.first.push_back(unit);
	};
	// Keep authoritative hostile/friendly tile order and ordinary deduplication.
	for(const auto & hex : tiles.hostileCreaturePositions)
		addOnce(hex, true);
	for(const auto & hex : tiles.friendlyCreaturePositions)
		addOnce(hex, false);
	if(tiles.friendlyCreaturePositions.empty())
		result.second = !result.first.empty();
	else
		for(const auto & hex : tiles.friendlyCreaturePositions)
			for(const auto * unit : result.first)
				if(unit->coversPos(hex))
					result.second = true;
	return result;
}
