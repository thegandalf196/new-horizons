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
#include "NewHorizonsInfernoSpecialties.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsMetamagicCore.h"
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
	if(getSpell()->getJsonKey() == "new-horizons:forceLance") return false;
	if(newHorizonsMetamagic::empoweredDamage(*this) || newHorizonsMetamagic::explicitlyIrresistible(*this)) return false;
	const bool ordinary = wouldResistOrdinary(unit);
	if(!unit || !activeResistanceRng || !activeResistanceServer) return ordinary;
	auto cached = std::ranges::find(resistanceRolls, unit->unitId(), &ResistanceRoll::unitId);
	if(cached == resistanceRolls.end() || cached->metamagicRetried) return ordinary;
	cached->metamagicRetried = true;
	const int chance = newHorizonsMetamagic::debuffRetryChance(*this);
	if(!ordinary || chance <= 0 || !isReceptive(unit)
		|| battle()->battleGetOwner(unit) == getCasterColor()) return ordinary;
	cached->resisted = newHorizonsMetamagic::retryResistanceBasisPoints(ordinary, chance,
		cached->probabilityBasisPoints >= 0 ? cached->probabilityBasisPoints : cached->probability * 100,
		[&](int maximum) { return activeResistanceRng->nextInt(0, maximum); });
	if(cached->resisted) resistantUnitIds.insert(unit->unitId());
	else resistantUnitIds.erase(unit->unitId());
	return cached->resisted;
}

bool BattleSpellMechanics::wouldResistOverflow(const battle::Unit * unit) const
{
	if(!unit || !activeResistanceServer || !activeResistanceRng) return false; // Projection never rolls.
	const auto probability = std::clamp(battle()->battleGetMagicResistance(unit), 0, 100);
	if(probability <= 0) return false;
	if(probability >= 100) return true;
	const auto cached = std::ranges::find(resistanceRolls, unit->unitId(), &ResistanceRoll::unitId);
	if(cached == resistanceRolls.end())
	{
		const auto sample = activeResistanceRng->nextInt(0, 99);
		resistanceRolls.push_back({unit->unitId(), probability, sample < probability, false, sample});
	}
	else if(!cached->resolved && cached->probability != probability)
	{
		// The primary may have supplied a resistance aura. Keep the existing
		// eager sample, but defend the one chosen spill in its CURRENT context.
		cached->probability = probability;
		cached->resisted = cached->sample < probability;
	}
	return wouldResistOrdinary(unit);
}

bool BattleSpellMechanics::wouldResistOrdinary(const battle::Unit * unit) const
{
	if(!unit)
		return false;

	if(infernoSpellSpecialties::bloodfireProtects(*this, *unit)) return false;
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
	const int probability = resistance->probabilityBasisPoints >= 0 ? resistance->probabilityBasisPoints : resistance->probability;
	const int maximum = resistance->probabilityBasisPoints >= 0 ? 10000 : 100;
	const bool affliction = newHorizonsMetamagic::hasResistibleCreatureAffliction(*this);
	if(((!isNegativeSpell() && !newHorizonsNaturesWrath::enabled(*this)) || !isMagicalEffect()) && !affliction) return resistance->resisted;
	if(!validSides
		|| recipientControllerSide == spellCasterSide
		|| !unit->isValidTarget(false) || !isReceptive(unit)
		|| probability <= 0 || probability >= maximum)
		return resistance->resisted;

	// Chain Lightning and scripted collateral call this method at their actual
	// resistance-consumer point. Cache the result before entering the server
	// resolver so repeated queries (including the normal target filter below)
	// cannot spend the side's one-use reroll twice.
	const bool firstResult = resistance->resisted;
	resistance->resolved = true;
	auto * const server = activeResistanceServer;
	auto * const rng = activeResistanceRng;
	const auto draw = [rng, firstResult, probability, maximum, resistance, firstDraw = true]() mutable
	{
		if(firstDraw)
		{
			firstDraw = false;
			return firstResult;
		}
		resistance->sample = rng->nextInt(0, maximum - 1);
		return resistance->sample < probability;
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

void BattleSpellMechanics::prepareResistanceRolls(vstd::RNG & rng, const Target & target,
	const Target & spellTarget, bool naturesWrath, bool realityWarp, const battle::Units & wrathConductors)
{
	resistanceRolls.clear();
	resistantUnitIds.clear();
	if(newHorizonsMetamagic::explicitlyIrresistible(*this)) return;
	const bool ordinaryRolls = (isNegativeSpell() || naturesWrath || realityWarp) && isMagicalEffect();
	if(ordinaryRolls || newHorizonsMetamagic::hasResistibleCreatureAffliction(*this))
	{
		//magic resistance
		const auto resistanceCandidates = naturesWrath ? wrathConductors : battle()->battleGetAllUnits(false);
		for(const auto * unit : resistanceCandidates)
		{
			if(infernoSpellSpecialties::bloodfireProtects(*this, *unit)) continue;
			if(!target.empty() && newHorizonsMagic::isProtectedAreaCenter(*battle(), getHeroCaster(),
				getSpellId(), *unit, target.front().hexValue, getCasterSide(), true))
				continue;
			if(realityWarp && (battle()->battleGetOwner(unit) == getCasterColor()
				|| std::ranges::none_of(spellTarget, [unit](const Destination & destination)
				{
					return destination.unitValue == unit;
				})))
				continue;
			if(naturesWrath && (ownerMatches(unit, true) || unit->isInvincible() || !isReceptive(unit)))
				continue;
			// Life Drain has a hostile damage target and a friendly healing target.
			// The latter is not resisting a hostile effect.
			if(owner->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL
				&& newHorizonsMagic::spellAllowedBySavedRoster(battle()->getBattle()->getMagicRules(), owner->getId())
				&& battle()->battleGetOwner(unit) == getCasterColor())
				continue;
			// Friendly Blink is ordinary negative magic for target immunity, but
			// an ally is not resisting a hostile magical effect.
			if(owner->getJsonKey() == newHorizonsBlink::SPELL_ID && unit->unitSide() == casterSide)
				continue;
			if(!ordinaryRolls && (std::ranges::none_of(spellTarget, [unit](const Destination & destination)
				{ return destination.unitValue == unit || (destination.hexValue.isValid() && unit->coversPos(destination.hexValue)); })
				|| !unit->isValidTarget(false) || !isReceptive(unit))) continue;
			const int bp = newHorizonsMetamagic::normalResistanceBasisPoints(*this, unit);
			if(!ordinaryRolls && bp <= 0) continue; // An absent prayer cannot add draws to native nonmagical abilities.
			const bool fractional = bp % 100 != 0;
			const int sample = rng.nextInt(0, fractional ? 9999 : 99);
			const bool didResist = sample < (fractional ? bp : bp / 100);
			resistanceRolls.push_back({unit->unitId(), bp / 100, didResist, false, sample, false, fractional ? bp : -1});
			if(didResist)
				resistantUnitIds.insert(unit->unitId());
		}
	}
}

void BattleSpellMechanics::beforeCast(ServerCallback * server, BattleSpellCast & sc, vstd::RNG & rng, const Target & target)
{
	beforeCast(server, sc, rng, target, nullptr);
}

bool BattleSpellMechanics::usesCompoundMirror() const
{
	if(getSpell()->getJsonKey() == "new-horizons:forceLance") return false;
	return isNewHorizonsStormOfDaggers()
		|| (owner && owner->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL
			&& newHorizonsMagic::spellAllowedBySavedRoster(battle()->getBattle()->getMagicRules(), owner->getId()));
}

bool BattleSpellMechanics::ownerMatches(const battle::Unit * unit, bool sameOwner) const
{
	if(applyingRunicShare)
		return unit && ((battle()->battleGetOwner(unit) == battle()->sideToPlayer(effectiveCasterSide())) == sameOwner);
	if(usesCompoundMirror())
		return unit && ((battle()->battleGetOwner(unit) == getCasterColor()) == sameOwner);
	return BaseMechanics::ownerMatches(unit, sameOwner);
}

bool BattleSpellMechanics::prepareCompoundMirror(ServerCallback * server, BattleSpellCast & sc,
	vstd::RNG & rng, const Target & target, std::vector<CompoundMirrorEffect> & result)
{
	const bool storm = isNewHorizonsStormOfDaggers();
	const size_t shares = storm ? target.size() : 1;
	if(mode != Mode::HERO || target.empty()
		|| std::none_of(target.begin(), target.begin() + shares, [](const Destination & destination)
		{
			return destination.unitValue
				&& destination.unitValue->hasBonusOfType(BonusType::MAGIC_MIRROR);
		}))
		return false;

	// Original validation and the initial eager resistance draws already ran.
	// This scratch plan is one paid cast, never a second cast or saved state.
	const auto originalMode = mode;
	const auto originalRolls = resistanceRolls;
	const auto originalResistant = resistantUnitIds;
	const auto restore = vstd::makeScopeGuard([&]()
	{
		mode = originalMode;
		resistanceRolls = originalRolls;
		resistantUnitIds = originalResistant;
	});
	effectsToApply.clear();
	for(size_t index = 0; index < shares; ++index)
	{
		mode = originalMode;
		resistanceRolls = originalRolls;
		resistantUnitIds = originalResistant;
		const auto * original = target[index].unitValue;
		if(!original)
			continue;
		Target aim = storm ? Target{Destination(original)} : target;
		const auto * damageTarget = aim.front().unitValue;
		effectsToApply = effects->prepare(this, aim, transformSpellTarget(aim));
		const auto recipients = collectTargets();
		for(const auto * unit : recipients)
			if(!vstd::contains(affectedUnits, unit))
				affectedUnits.push_back(unit);
		result.push_back({mode, std::move(effectsToApply)});
	}
	effectsToApply.clear();
	return true;
}

bool BattleSpellMechanics::isReflected(ServerCallback * server, const battle::Unit * unit, vstd::RNG & rng)
{
	if(getSpell()->getJsonKey() == "new-horizons:forceLance") return false;
	if (unit == nullptr)
		return false;
	const std::vector<int> directSpellRange = { 0 };
	bool isDirectSpell = !isMassive() && owner -> getLevelInfo(getRangeLevel()).range == directSpellRange;
	bool spellIsReflectable = isDirectSpell && !applyingRunicShare
		&& (mode == Mode::HERO || mode == Mode::MAGIC_MIRROR) && isNegativeSpell();
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

const battle::Unit * BattleSpellMechanics::getRandomUnitForOwner(vstd::RNG & rng, PlayerColor owner)
{
	auto targets = battle()->getBattle()->getUnitsIf([this, owner](const battle::Unit * unit)
	{
		return battle()->battleGetOwner(unit) == owner && unit->isValidTarget(false) &&
			!unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
	});
	std::ranges::sort(targets, {}, &battle::Unit::unitId);
	return !targets.empty() ? (*RandomGeneratorUtil::nextItem(targets, rng)) : nullptr;
}

}
