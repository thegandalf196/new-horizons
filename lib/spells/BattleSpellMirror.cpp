/*
 * BattleSpellMechanics.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleSpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsBlink.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsNaturesWrath.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsSorcery.h"
#include "../ScopeGuard.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../networkPacks/PacksForClientBattle.h"

#include <algorithm>
#include <vstd/RNG.h>

namespace spells
{

bool BattleSpellMechanics::wouldResist(const battle::Unit * unit) const
{
	if(!unit)
		return false;

	const auto unitId = unit->unitId();
	auto resistance = std::ranges::find(resistanceRolls, unitId, &ResistanceRoll::unitId);
	if(resistance == resistanceRolls.end())
		return resistantUnitIds.contains(unitId);
	if(resistance->resolved || !activeResistanceServer || !activeResistanceRng)
		return resistance->resisted;

	const BattleSide recipientControllerSide = battle()->playerToSide(battle()->battleGetOwner(unit));
	const BattleSide spellCasterSide = effectiveCasterSide();
	const bool validSides = (recipientControllerSide == BattleSide::ATTACKER || recipientControllerSide == BattleSide::DEFENDER)
		&& (spellCasterSide == BattleSide::ATTACKER || spellCasterSide == BattleSide::DEFENDER);
	if((!isNegativeSpell() && !newHorizonsNaturesWrath::enabled(*this)) || !isMagicalEffect() || !validSides
		|| recipientControllerSide == spellCasterSide
		|| !unit->isValidTarget(false) || !isReceptive(unit)
		|| resistance->probability <= 0 || resistance->probability >= 100)
		return resistance->resisted;

	// Chain Lightning and scripted collateral call this method at their actual
	// resistance-consumer point. Cache the result before entering the server
	// resolver so repeated queries (including the normal target filter below)
	// cannot spend the side's one-use reroll twice.
	const int probability = resistance->probability;
	const bool firstResult = resistance->resisted;
	resistance->resolved = true;
	auto * const server = activeResistanceServer;
	auto * const rng = activeResistanceRng;
	const auto draw = [rng, firstResult, probability, firstDraw = true]() mutable
	{
		if(firstDraw)
		{
			firstDraw = false;
			return firstResult;
		}
		return rng->nextInt(0, 99) < probability;
	};
	const bool finalResult = server->resolveAdverseCombatRoll(battle()->getBattle()->getBattleID(),
		recipientControllerSide, true, false, draw);
	resistance->resisted = finalResult;
	if(finalResult)
		resistantUnitIds.insert(unitId);
	else
		resistantUnitIds.erase(unitId);
	return finalResult;
}

bool BattleSpellMechanics::isSpellLocked(const battle::Unit * unit)
{
	if(!unit)
		return false;

	static const SpellID spellLock(SpellID::decode(newHorizonsSorcery::SPELL_LOCK_SPELL));
	const auto lockBonuses = unit->getBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(spellLock)));
	return lockBonuses && vstd::contains_if(*lockBonuses, [](const std::shared_ptr<Bonus> & bonus)
	{
		return bonus && bonus->type == BonusType::MAGIC_RESISTANCE
			&& Bonus::NTurns(bonus.get()) && bonus->turnsRemain > 0;
	});
}

void BattleSpellMechanics::prepareResistanceRolls(vstd::RNG & rng, const Target & target,
	const Target & spellTarget, bool naturesWrath, bool realityWarp, const battle::Units & wrathConductors)
{
	resistanceRolls.clear();
	resistantUnitIds.clear();
	if((isNegativeSpell() || naturesWrath || realityWarp) && isMagicalEffect())
	{
		//magic resistance
		const auto resistanceCandidates = naturesWrath ? wrathConductors : battle()->battleGetAllUnits(false);
		for(const auto * unit : resistanceCandidates)
		{
			if(!target.empty() && newHorizonsMagic::isProtectedAreaCenter(*battle(), getHeroCaster(),
				getSpellId(), *unit, target.front().hexValue, getCasterSide(), true))
				continue;
			if(realityWarp && (battle()->battleGetOwner(unit) == getCasterColor()
				|| std::ranges::none_of(spellTarget, [unit](const Destination & destination)
				{
					return destination.unitValue == unit;
				})))
				continue;
			if(isMagicalEffect() && isSpellLocked(unit))
			{
				resistantUnitIds.insert(unit->unitId());
				continue;
			}
			if(naturesWrath && (ownerMatches(unit, true) || unit->isInvincible() || !isReceptive(unit)))
				continue;
			// Life Drain has a hostile damage target and a friendly healing target.
			// The latter is not resisting a hostile effect; Spell Lock above still
			// blocks either half of the paired spell.
			if(owner->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL
				&& newHorizonsMagic::spellAllowedBySavedRoster(battle()->getBattle()->getMagicRules(), owner->getId())
				&& unit->unitSide() == casterSide)
				continue;
			// Friendly Blink is ordinary negative magic for target immunity, but
			// an ally is not resisting a hostile magical effect.
			if(owner->getJsonKey() == newHorizonsBlink::SPELL_ID && unit->unitSide() == casterSide)
				continue;
			const int prob = std::min(unit->magicResistance(), 100); //probability of resistance in %
			const bool didResist = rng.nextInt(0, 99) < prob;
			resistanceRolls.push_back({unit->unitId(), prob, didResist, false});
			if(didResist)
				resistantUnitIds.insert(unit->unitId());
		}
	}
}

bool BattleSpellMechanics::isReflected(ServerCallback * server, const battle::Unit * unit, vstd::RNG & rng)
{
	if (unit == nullptr)
		return false;
	const std::vector<int> directSpellRange = { 0 };
	bool isDirectSpell = !isMassive() && owner -> getLevelInfo(getRangeLevel()).range == directSpellRange;
	bool spellIsReflectable = isDirectSpell && (mode == Mode::HERO || mode == Mode::MAGIC_MIRROR) && isNegativeSpell();
	bool targetCanReflectSpell = spellIsReflectable && unit->getAllBonuses(Selector::type()(BonusType::MAGIC_MIRROR))->size()>0;
	const bool friendlyBlink = owner->getJsonKey() == newHorizonsBlink::SPELL_ID && unit->unitSide() == casterSide;
	if(!targetCanReflectSpell || friendlyBlink)
		return false;

	const int chance = unit->valOfBonuses(BonusType::MAGIC_MIRROR);
	const auto draw = [&rng, chance]()
	{
		return rng.nextInt(0, 99) < chance;
	};
	const BattleSide targetControllerSide = battle()->playerToSide(battle()->battleGetOwner(unit));
	const BattleSide affectedSide = effectiveCasterSide();
	const bool hostileTarget = (targetControllerSide == BattleSide::ATTACKER || targetControllerSide == BattleSide::DEFENDER)
		&& (affectedSide == BattleSide::ATTACKER || affectedSide == BattleSide::DEFENDER)
		&& targetControllerSide != affectedSide;
	if(!server || !hostileTarget || chance <= 0 || chance >= 100)
		return draw();

	// A successful redirect harms the army currently casting the spell. A
	// reflected cast changes the effective side even though casterSide remains
	// the original hero's side.
	return server->resolveAdverseCombatRoll(battle()->getBattle()->getBattleID(), affectedSide,
		true, true, draw);
}

void BattleSpellMechanics::reflect(ServerCallback * server, BattleSpellCast & sc, vstd::RNG & rng, const battle::Unit * unit)
{
	auto otherSide = battle()->otherSide(unit->unitSide());
	auto newTarget = getRandomUnit(rng, otherSide);
	if (newTarget == nullptr)
		throw std::runtime_error("Failed to find random unit to reflect spell!");
	auto reflectedTo = newTarget->getPosition();

	mode = Mode::MAGIC_MIRROR;
	sc.reflectedCres.insert(unit->unitId());
	sc.tile = reflectedTo;

	if (!isReceptive(newTarget))
		sc.resistedCres.insert(newTarget->unitId());    //A spell can be reflected to then resisted by an immune unit. Consistent with the original game.

	beforeCast(server, sc, rng, { Destination(reflectedTo) });
}

const battle::Unit * BattleSpellMechanics::getRandomUnit(vstd::RNG & rng, const BattleSide & side)
{
	auto targets = battle()->getBattle()->getUnitsIf([&side](const battle::Unit * unit)
	{
		return unit->unitSide() == side && unit->isValidTarget(false) &&
			!unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
	});
	return !targets.empty() ? (*RandomGeneratorUtil::nextItem(targets, rng)) : nullptr;
}

}
