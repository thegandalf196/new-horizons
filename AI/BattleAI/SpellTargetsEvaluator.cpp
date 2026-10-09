/*
 * SpellTargetsEvaluator.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/NewHorizonsConfusionResolution.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/battle/NewHorizonsSoulChain.h"
#include "AttackPossibility.h"
#include "PotentialTargets.h"
#include "StackWithBonuses.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/effects/BattleForm.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"
#include "../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../lib/spells/NewHorizonsPandemonium.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/CRandomGenerator.h"
#include "SpellTargetsEvaluator.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/spells/Spell.h>

#include <set>
#include <tuple>

using namespace spells;

namespace
{
// Transfigure Matter is represented as a location-targeted spell by the
// generic mechanics layer, but only physical battlefield obstacles are legal
// aims.  Keep this identity check local to the AI until the curated spell
// receives a public typed helper in the shared spell API.
bool isTransfigureMatter(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == "new-horizons:transfigureMatter";
}

bool isCanonicalSummonTrolls(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && (spell->getJsonKey() == "new-horizons:summonTrolls"
		|| spell->getJsonKey() == "new-horizons:elementalConvergence");
}

bool isCanonicalVerdantPrison(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == "new-horizons:verdantPrison";
}

bool isCanonicalHydrasVitality(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == "new-horizons:hydrasVitality";
}

bool isHandOfFateSpell(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == "new-horizons:handOfFate";
}

bool isCanonicalHandOfFate(const Mechanics * spellMechanics)
{
	if(!isHandOfFateSpell(spellMechanics))
		return false;

	const auto * callback = spellMechanics->battle();
	const auto * battleInfo = callback ? callback->getBattle() : nullptr;
	if(!battleInfo)
		return false;

	const auto & magicRules = battleInfo->getMagicRules();
	return newHorizonsMagic::rulesActive(magicRules)
		&& newHorizonsMagic::spellAllowedBySavedRoster(magicRules, spellMechanics->getSpellId());
}

std::vector<Target> canonicalHandOfFateTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!isCanonicalHandOfFate(spellMechanics) || !spellMechanics->battle()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::CREATURE})
		return result;

	const auto casterSide = spellMechanics->getCasterSide();
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return result;

	const auto enemySide = spellMechanics->battle()->otherSide(casterSide);
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		if(!unit || unit->unitSide() != enemySide || !unit->alive() || !unit->isValidTarget(false))
			continue;

		Target target{Destination(unit)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}
	return result;
}

std::vector<Target> canonicalHydrasVitalityTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!isCanonicalHydrasVitality(spellMechanics) || !spellMechanics->battle()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::CREATURE})
		return result;

	const auto casterSide = spellMechanics->getCasterSide();
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return result;

	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || !unit->isValidTarget(false))
			continue;

		// Shared mechanics owns the living/organic target rules. Keep candidates
		// aligned with execution instead of duplicating creature tags here.
		Target target{Destination(unit)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	return result;
}

std::vector<Target> canonicalVerdantPrisonTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!isCanonicalVerdantPrison(spellMechanics) || !spellMechanics->battle()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::CREATURE})
		return result;

	const auto casterSide = spellMechanics->getCasterSide();
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return result;

	const auto enemySide = spellMechanics->battle()->otherSide(casterSide);
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		if(!unit || unit->unitSide() != enemySide || !unit->alive() || !unit->isValidTarget(false))
			continue;

		// The shared spell mechanics consult Verdant Prison's canonical ring
		// placement when answering canBeCastAt. This keeps the AI's candidate list
		// aligned with execution and rejects targets whose ring has no legal hexes.
		if(spellMechanics->rangeInHexes(unit->getPosition()).empty())
			continue;

		Target target{Destination(unit)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}
	return result;
}

std::vector<Target> canonicalSummonTrollsTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!isCanonicalSummonTrolls(spellMechanics) || !spellMechanics->battle()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::LOCATION})
		return result;

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isAvailable())
			continue;

		Target target{Destination(hex)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	// The placement value is independent of the chosen hex, but BattleEvaluator
	// still projects every candidate through the full hypothetical battle. Sample
	// the ordered legal set across the whole field so that turn cost stays bounded
	// without preferring one edge of the battlefield.
	constexpr size_t MAX_PLACEMENT_CANDIDATES = 12;
	if(result.size() <= MAX_PLACEMENT_CANDIDATES)
		return result;

	std::vector<Target> sampled;
	sampled.reserve(MAX_PLACEMENT_CANDIDATES);
	for(size_t index = 0; index < MAX_PLACEMENT_CANDIDATES; ++index)
	{
		const auto sourceIndex = index * (result.size() - 1) / (MAX_PLACEMENT_CANDIDATES - 1);
		sampled.push_back(std::move(result[sourceIndex]));
	}
	return sampled;
}

bool isCanonicalLifeDrain(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_LIFE_DRAIN_SPELL;
}

std::vector<Target> canonicalLifeDrainTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!spellMechanics || !spellMechanics->battle())
		return result;

	const auto casterSide = spellMechanics->getCasterSide();
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return result;

	std::vector<const battle::Unit *> enemies;
	std::vector<const battle::Unit *> friendlies;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		// Life Drain requires two surviving, targetable stacks. In particular, do
		// not offer corpses to the healing half of the pair: this spell cannot
		// restore dead creatures.
		if(!unit || !unit->alive() || !unit->isValidTarget(false))
			continue;

		if(unit->unitSide() == casterSide)
			friendlies.push_back(unit);
		else if(unit->unitSide() == spellMechanics->battle()->otherSide(casterSide))
			enemies.push_back(unit);
	}

	// Target order is part of the spell contract: the hostile source is first,
	// then the friendly recipient. Validate only complete pairs because Life
	// Drain has no meaningful one-creature prefix target.
	for(const auto * enemy : enemies)
		for(const auto * friendly : friendlies)
		{
			Target target{Destination(enemy), Destination(friendly)};
			detail::ProblemImpl problem;
			if(spellMechanics->canBeCastAt(target, problem))
				result.push_back(std::move(target));
		}

	return result;
}

bool isPhysicalObstacle(const CObstacleInstance & obstacle)
{
	// The curated effect deliberately admits ordinary scenery only. Absolute
	// obstacles cover siege/fortification-like scenery and are rejected by the
	// authoritative Transfigure Matter script.
	return obstacle.obstacleType == CObstacleInstance::USUAL;
}

bool isCanonicalLandMine(const Mechanics * spellMechanics)
{
	return spellMechanics
		&& spellMechanics->usesNewHorizonsMagic()
		&& newHorizonsMagic::isLandMine(spellMechanics->getSpellId());
}

bool isSelectedQuicksand(const Mechanics * spellMechanics)
{
	const auto * callback = spellMechanics ? spellMechanics->battle() : nullptr;
	const auto * battle = callback ? callback->getBattle() : nullptr;
	return battle && newHorizonsMagic::quicksandSelectedPlacementEnabled(
		battle->getMagicRules(), spellMechanics->getSpellId());
}

bool isCanonicalFireWall(const Mechanics * spellMechanics)
{
	return spellMechanics
		&& spellMechanics->usesNewHorizonsMagic()
		&& newHorizonsMagic::isFireWall(spellMechanics->getSpellId());
}

bool isCanonicalTimeStop(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL;
}

bool isCanonicalSanctuary(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == "new-horizons:sanctuary";
}

bool isVengefulVines(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == newHorizonsVengefulVines::SPELL_KEY;
}

std::vector<Target> canonicalVengefulVinesTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!spellMechanics || !spellMechanics->battle()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{
			AimType::LOCATION, AimType::LOCATION, AimType::LOCATION})
		return result;

	const auto * battle = spellMechanics->battle();
	const auto * battleInfo = battle->getBattle();
	if(!battleInfo || !newHorizonsVengefulVines::enabled(battleInfo->getMagicRules(), spellMechanics->getSpellId()))
		return result;

	const auto casterSide = spellMechanics->getCasterSide();
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return result;

	const auto enemySide = battle->otherSide(casterSide);
	std::set<BattleHex> enemyOccupiedHexes;
	for(const auto * unit : battle->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || !unit->isValidTarget(false) || unit->isInvincible()
			|| unit->unitSide() != enemySide || !spellMechanics->isReceptive(unit))
			continue;

		for(const auto & hex : unit->getHexes())
			enemyOccupiedHexes.insert(hex);
	}
	if(enemyOccupiedHexes.empty())
		return result;

	for(const auto & footprint : newHorizonsVengefulVines::connectedTriples())
	{
		const bool intersectsEnemy = std::any_of(footprint.begin(), footprint.end(), [&](const BattleHex & hex)
		{
			return enemyOccupiedHexes.contains(hex);
		});
		if(!intersectsEnemy)
			continue;

		Target target;
		target.reserve(footprint.size());
		for(const auto & hex : footprint)
			target.emplace_back(hex);

		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	return result;
}

std::vector<Target> canonicalSanctuaryTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!isCanonicalSanctuary(spellMechanics) || !spellMechanics->battle())
		return result;

	const auto * battle = spellMechanics->battle();
	const auto * battleInfo = battle->getBattle();
	if(!battleInfo || !newHorizonsMagic::rulesActive(battleInfo->getMagicRules()))
		return result;

	for(const auto * unit : battle->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || !unit->isValidTarget(false) || unit->isGhost() || unit->isTurret()
			|| unit->hasBonusOfType(BonusType::SANCTIFIED)
			|| unit->unitSide() != spellMechanics->getCasterSide())
			continue;

		Target target{Destination(unit)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	return result;
}

bool isCanonicalSpellLock(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return spell && spell->getJsonKey() == newHorizonsSorcery::SPELL_LOCK_SPELL;
}

bool isCanonicalPurify(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	const auto * callback = spellMechanics ? spellMechanics->battle() : nullptr;
	const auto * battle = callback ? callback->getBattle() : nullptr;
	return spell && battle
		&& spell->getJsonKey() == newHorizonsPurify::SPELL_ID
		&& newHorizonsPurify::enabled(battle->getMagicRules(), spell->getId());
}

bool isCanonicalNaturePoison(const Mechanics * spellMechanics)
{
	const auto * battleCallback = spellMechanics ? spellMechanics->battle() : nullptr;
	const auto * battle = battleCallback ? battleCallback->getBattle() : nullptr;
	return battle && newHorizonsMagic::physicalPoisonEnabled(battle->getMagicRules(), spellMechanics->getSpellId());
}

bool isCanonicalPlague(const Mechanics * spellMechanics)
{
	const auto * battleCallback = spellMechanics ? spellMechanics->battle() : nullptr;
	const auto * battle = battleCallback ? battleCallback->getBattle() : nullptr;
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return battle
		&& newHorizonsMagic::rulesActive(battle->getMagicRules())
		&& spell
		&& spell->getJsonKey() == newHorizonsPlague::SPELL_ID;
}

bool isSpellLocked(const battle::Unit * unit);

bool isCanonicalSoulChain(const Mechanics * spellMechanics)
{
	const auto * battleCallback = spellMechanics ? spellMechanics->battle() : nullptr;
	const auto * battle = battleCallback ? battleCallback->getBattle() : nullptr;
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	return battle && spell
		&& spell->getJsonKey() == newHorizonsSoulChain::SPELL_ID
		&& newHorizonsSoulChain::isEnabled(battle->getMagicRules());
}

bool isCanonicalShadowGift(const Mechanics * spellMechanics)
{
	const auto * spell = spellMechanics ? spellMechanics->getSpell() : nullptr;
	const auto * callback = spellMechanics ? spellMechanics->battle() : nullptr;
	const auto * battle = callback ? callback->getBattle() : nullptr;
	if(!battle || !spell || spell->getJsonKey() != newHorizonsShadowGift::SPELL_ID)
		return false;
	const auto & magicRules = battle->getMagicRules();
	return newHorizonsMagic::shadowGiftEnabled(magicRules, spell->getId());
}

int distanceToUnit(const BattleHex & hex, const battle::Unit * unit);

float shadowGiftAttackLikelihood(const Mechanics * spellMechanics, const battle::Unit * attacker,
	const battle::Unit * defender)
{
	const auto * battle = spellMechanics ? spellMechanics->battle() : nullptr;
	if(!battle || !attacker || !defender)
		return 0.0f;
	if(battle->battleCanShoot(attacker, defender->getPosition()))
		return 0.7f;

	const int distance = distanceToUnit(defender->getPosition(), attacker);
	const int movement = std::max(0, static_cast<int>(attacker->getMovementRange(0)));
	if(distance <= 1)
		return 0.8f;
	if(distance <= movement + 1)
		return 0.55f;
	if(distance <= movement + 3)
		return 0.2f;
	return 0.0f;
}

float spellApplicationChance(const Mechanics * spellMechanics, const battle::Unit * unit)
{
	if(!spellMechanics || !unit || !spellMechanics->isNegativeSpell() || !spellMechanics->isMagicalEffect())
		return 1.0f;
	float chance = 1.0f - static_cast<float>(std::clamp(unit->magicResistance(), 0, 100)) / 100.0f;
	const auto * battle = spellMechanics->battle();
	if(!battle || !battle->getBattle() || chance <= 0.0f || chance >= 1.0f)
		return chance;
	const auto recipientSide = battle->playerToSide(battle->battleGetOwner(unit));
	const auto casterSide = spellMechanics->isMagicMirror()
		? battle->otherSide(spellMechanics->getCasterSide()) : spellMechanics->getCasterSide();
	if((recipientSide == BattleSide::ATTACKER || recipientSide == BattleSide::DEFENDER)
		&& (casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER)
		&& recipientSide != casterSide
		&& battle->getBattle()->getAdverseCombatRerollState(recipientSide).available())
		chance *= chance;
	return chance;
}

bool isSoulChainTargetUnit(const Mechanics * spellMechanics, const battle::Unit * unit)
{
	if(!isCanonicalSoulChain(spellMechanics) || !unit || !unit->alive()
		|| !unit->isValidTarget(false) || unit->isInvincible()
		|| isSpellLocked(unit)
		|| !spellMechanics->isReceptive(unit)
		|| unit->hasImmunity(spellMechanics->getSpellId())
		|| unit->hasAbsoluteImmunity(spellMechanics->getSpellId()))
		return false;

	const auto casterSide = spellMechanics->getCasterSide();
	const auto * battle = spellMechanics->battle();
	const battle::Target singleton{battle::Destination(unit)};
	return battle && newHorizonsSoulChain::validEnemyTargetSet(*battle, casterSide, singleton);
}

bool isNaturePoisonTarget(const Mechanics * spellMechanics, const battle::Unit * unit)
{
	return isCanonicalNaturePoison(spellMechanics)
		&& unit
		&& unit->alive()
		&& unit->isValidTarget(false)
		&& !unit->isInvincible()
		&& (spellMechanics->getCasterSide() == BattleSide::ATTACKER
			|| spellMechanics->getCasterSide() == BattleSide::DEFENDER)
		&& unit->unitSide() != spellMechanics->getCasterSide()
		&& spellMechanics->isReceptive(unit)
		&& !unit->hasImmunity(spellMechanics->getSpellId())
		&& !unit->hasAbsoluteImmunity(spellMechanics->getSpellId());
}

bool isSpellLocked(const battle::Unit * unit)
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

bool canonicalLandMineHexIsEmpty(const CBattleInfoCallback & battle,
	const AccessibilityInfo & accessibility, const BattleHex & hex)
{
	// Keep this predicate in lockstep with the authoritative action validator.
	// In particular, ACCESSIBLE is intentionally stricter than merely
	// passable: a gate, wall, side column, moat, or destructible wall is not a
	// legal canonical mine tile.
	if(!hex.isAvailable()
		|| accessibility[hex.toInt()] != EAccessibility::ACCESSIBLE
		|| battle.battleGetUnitByPos(hex, true)
		|| !battle.battleGetAllObstaclesOnPos(hex, false).empty())
		return false;

	if(!battle.hasFortifications())
		return true;

	const auto wallPart = battle.battleHexToWallPart(hex);
	if(wallPart == EWallPart::INVALID)
		return true;
	if(wallPart == EWallPart::INDESTRUCTIBLE_PART
		|| wallPart == EWallPart::INDESTRUCTIBLE_PART_OF_GATE
		|| wallPart == EWallPart::BOTTOM_TOWER
		|| wallPart == EWallPart::UPPER_TOWER)
		return false;

	const auto wallState = battle.battleGetWallState(wallPart);
	return wallState == EWallState::NONE || wallState == EWallState::DESTROYED;
}

struct LandMineHexScore
{
	BattleHex hex;
	int64_t score = 0;
	std::vector<uint32_t> adjacentEnemies;
};

bool isGroundHostile(const Mechanics * spellMechanics, const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->isGhost() || !unit->isValidTarget() || unit->isTurret())
		return false;
	if(unit->hasBonusOfType(BonusType::FLYING))
		return false;
	return spellMechanics->battle()->battleGetOwner(unit) != spellMechanics->getCasterColor();
}

bool isGroundAlly(const Mechanics * spellMechanics, const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->isGhost() || !unit->isValidTarget() || unit->isTurret())
		return false;
	if(unit->hasBonusOfType(BonusType::FLYING))
		return false;
	return spellMechanics->battle()->battleGetOwner(unit) == spellMechanics->getCasterColor();
}

uint64_t landMineDamagePotential(const Mechanics * spellMechanics, const battle::Unit * enemy)
{
	const auto adjustedDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(enemy));
	return std::min<uint64_t>(static_cast<uint64_t>(adjustedDamage), enemy->getAvailableHealth());
}

bool isLandMineAffectable(const Mechanics * spellMechanics, const battle::Unit * enemy)
{
	if(!isGroundHostile(spellMechanics, enemy)
		|| !spellMechanics->isReceptive(enemy)
		|| enemy->hasImmunity(spellMechanics->getSpellId())
		|| enemy->hasAbsoluteImmunity(spellMechanics->getSpellId())
		|| enemy->isInvincible())
		return false;

	const int resistance = std::clamp(enemy->magicResistance(), 0, 100);
	return resistance < 100 && landMineDamagePotential(spellMechanics, enemy) > 0;
}

std::vector<const battle::Unit *> landMineEnemies(const Mechanics * spellMechanics)
{
	std::vector<const battle::Unit *> result;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
		if(isLandMineAffectable(spellMechanics, unit))
			result.push_back(unit);
	return result;
}

std::vector<const battle::Unit *> landMineAllies(const Mechanics * spellMechanics)
{
	std::vector<const battle::Unit *> result;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
		if(isGroundAlly(spellMechanics, unit))
			result.push_back(unit);
	return result;
}

int distanceToUnit(const BattleHex & hex, const battle::Unit * unit)
{
	int result = std::numeric_limits<int>::max();
	for(const auto & occupied : unit->getHexes())
		if(occupied.isValid())
			result = std::min(result, static_cast<int>(BattleHex::getDistance(hex, occupied)));
	return result;
}

int64_t projectedSoulChainTriggerDamage(const Mechanics * spellMechanics,
	const battle::Unit * secondary, DamageCache & damageCache,
	const std::shared_ptr<CBattleInfoCallback> & battleState)
{
	if(!spellMechanics || !secondary || !secondary->alive() || !secondary->isValidTarget(false))
		return 0;

	const auto * battle = spellMechanics->battle();
	const auto casterSide = spellMechanics->getCasterSide();
	if(!battle || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return 0;

	double expectedDamage = 0.0;
	for(const auto * attacker : battle->battleGetAllUnits(false))
	{
		if(!attacker || attacker->unitSide() != casterSide || !attacker->alive()
			|| !attacker->isValidTarget(false) || attacker->isGhost() || attacker->isTurret()
			|| (!attacker->willMove() && !attacker->willMove(1)))
			continue;

		const bool shooting = battle->battleCanShoot(attacker, secondary->getPosition());
		const auto attackCount = AttackPossibility::getAttackCount(*attacker, shooting, *battleState);
		const auto damage = damageCache.getDamage(attacker, secondary, battleState);
		if(damage <= 0 || attackCount <= 0)
			continue;

		float attackLikelihood = 0.0f;
		if(shooting)
			attackLikelihood = 0.75f;
		else
		{
			const int distance = distanceToUnit(secondary->getPosition(), attacker);
			const int movement = std::max(0, static_cast<int>(attacker->getMovementRange(0)));
			if(distance <= 1)
				attackLikelihood = 0.90f;
			else if(distance <= movement + 1)
				attackLikelihood = 0.60f;
			else if(distance <= movement + 3)
				attackLikelihood = 0.25f;
		}
		const auto damagePerActivation = static_cast<double>(damage) * attackCount;
		for(const int turn : {0, 1})
			if(attacker->willMove(turn))
				expectedDamage += damagePerActivation * attackLikelihood;
	}

	// A stack may be attacked several times, but expected incoming damage cannot
	// exceed its remaining health over Soul Chain's two-round window.
	expectedDamage = std::min(expectedDamage,
		static_cast<double>(std::max<int64_t>(0, secondary->getAvailableHealth())));
	const auto applicationChance = spellApplicationChance(spellMechanics, secondary);
	const auto resistedExpectedDamage = expectedDamage * applicationChance;
	return static_cast<int64_t>(std::llround(resistedExpectedDamage));
}

int32_t soulChainEchoBasisPoints(const Mechanics * spellMechanics)
{
	const auto * hero = spellMechanics ? spellMechanics->getHeroCaster() : nullptr;
	const bool soulBinder = hero && hero->hasActivePerk(
		"new-horizons:shadowMagic", "new-horizons:shadowMagic.soulBinder");
	return spellMechanics
		? newHorizonsSoulChain::echoPercentBasisPoints(
			std::max(0, spellMechanics->getEffectPower()),
			spellMechanics->getSpellPowerCoefficientBasisPoints(), soulBinder)
		: 0;
}

float expectedSoulChainEchoValue(const Mechanics * spellMechanics,
	const battle::Unit * primary, const battle::Unit * secondary, int64_t expectedSecondaryDamage,
	int32_t echoBasisPoints, DamageCache & damageCache,
	const std::shared_ptr<CBattleInfoCallback> & battleState)
{
	if(!spellMechanics || !primary || !secondary || expectedSecondaryDamage <= 0
		|| !primary->alive() || !secondary->alive())
		return 0.0f;

	const auto * battle = spellMechanics->battle();
	if(!battle)
		return 0.0f;

	auto projectedPrimary = primary->acquireState();
	const auto capturedPenetration = spellMechanics->getCapturedMdrPenetration();
	const auto adjustedEcho = newHorizonsSoulChain::adjustedEchoDamage(
		*battle, spellMechanics->getCasterSide(), projectedPrimary.get(),
		expectedSecondaryDamage, echoBasisPoints, &capturedPenetration);
	const auto primaryApplicationChance = spellApplicationChance(spellMechanics, primary);
	const auto expectedEcho = static_cast<int64_t>(std::llround(
		static_cast<double>(adjustedEcho) * primaryApplicationChance));
	const auto actualEcho = std::min<int64_t>(
		std::max<int64_t>(0, expectedEcho), std::max<int64_t>(0, projectedPrimary->getAvailableHealth()));
	if(actualEcho <= 0)
		return 0.0f;

	return AttackPossibility::calculateDamageReduce(nullptr, projectedPrimary.get(),
		static_cast<uint64_t>(actualEcho), damageCache, battleState);
}

int distanceToFootprint(const spells::Target & target, const battle::Unit * unit)
{
	int result = std::numeric_limits<int>::max();
	for(const auto & destination : target)
	{
		if(destination.unitValue != nullptr || !destination.hexValue.isValid())
			continue;
		result = std::min(result, distanceToUnit(destination.hexValue, unit));
	}
	return result;
}

float fireWallTriggerLikelihood(const spells::Target & target, const battle::Unit * unit)
{
	const int distance = distanceToFootprint(target, unit);
	if(distance == std::numeric_limits<int>::max())
		return 0.0f;

	const int movement = std::max(0, static_cast<int>(unit->getMovementRange(0)));
	if(distance <= 1)
		return 0.75f;
	if(distance <= movement + 1)
		return 0.45f;
	if(distance <= movement + 3)
		return 0.20f;
	return 0.05f;
}

LandMineHexScore scoreLandMineHex(const Mechanics * spellMechanics, const BattleHex & hex,
	const std::vector<const battle::Unit *> & enemies,
	const std::vector<const battle::Unit *> & allies)
{
	LandMineHexScore result;
	result.hex = hex;

	// A mine directly adjacent to a ground enemy is the strongest pressure: it
	// punishes the next step and blocks the most obvious melee approach.  The
	// remaining terms keep mines useful when no adjacent tile is available by
	// favouring reachable-looking ground paths toward our army.
	for(const auto * enemy : enemies)
	{
		const int enemyDistance = distanceToUnit(hex, enemy);
		if(enemyDistance == std::numeric_limits<int>::max())
			continue;
		// Placement must follow the value of the delayed damage, rather than the
		// number of hostile stacks alone.  In particular, immune and fully
		// resistant clusters are absent from `enemies` and cannot pull every mine
		// away from a susceptible stack elsewhere on the battlefield.
		const auto damageValue = landMineDamagePotential(spellMechanics, enemy);
		if(damageValue == 0)
			continue;

		const bool adjacent = enemyDistance == 1;
		const int movement = static_cast<int>(enemy->getMovementRange(0));
		if(adjacent)
			result.score += 100000 * static_cast<int64_t>(damageValue);
		else if(enemyDistance <= movement + 1)
			result.score += (25000 + (movement + 1 - enemyDistance) * 1000)
				* static_cast<int64_t>(damageValue);
		else
			result.score += static_cast<int64_t>(std::max(0, 6000 - enemyDistance * 250))
				* static_cast<int64_t>(damageValue);

		if(adjacent)
			result.adjacentEnemies.push_back(enemy->unitId());

		// Prefer a hex on the geometric shortest corridor from an enemy to one of
		// our ground units.  This is only a tie-breaker behind adjacency/range,
		// but avoids putting every mine in an irrelevant corner of the field.
		int closestAllyDistance = std::numeric_limits<int>::max();
		for(const auto * ally : allies)
			closestAllyDistance = std::min(closestAllyDistance, distanceToUnit(enemy->getPosition(), ally));
		if(closestAllyDistance != std::numeric_limits<int>::max())
		{
			const int candidateToAlly = [&]()
			{
				int distance = std::numeric_limits<int>::max();
				for(const auto * ally : allies)
					distance = std::min(distance, distanceToUnit(hex, ally));
				return distance;
			}();
			if(candidateToAlly < closestAllyDistance)
				result.score += (closestAllyDistance - candidateToAlly) * 100;
		}
	}

	return result;
}

float landMineTriggerLikelihood(const BattleHex & hex, const battle::Unit * enemy)
{
	const int distance = distanceToUnit(hex, enemy);
	if(distance == std::numeric_limits<int>::max())
		return 0.0f;

	// A placed mine is a delayed threat, not an immediate attack.  Keep the
	// expected trigger contribution on the same scale as a direct attack and
	// deliberately discount tiles which are not on the enemy's next approach.
	// The exact values are a stable ranking heuristic; the authoritative script
	// still decides whether and when a unit actually enters a mine.
	const int movement = std::max(0, static_cast<int>(enemy->getMovementRange(0)));
	if(distance <= 1)
		return 0.75f;
	if(distance <= movement + 1)
		return 0.45f;
	if(distance <= movement + 3)
		return 0.20f;
	return 0.05f;
}

double maximumWeightLandMineAssignment(const std::vector<std::vector<double>> & enemyValues,
	size_t mineCount)
{
	if(enemyValues.empty() || mineCount == 0)
		return 0.0;

	// Canonical Land Mine casts contain two to four mines.  A bitmask DP is
	// therefore small enough to enumerate every one-enemy/one-mine assignment,
	// while avoiding greedy collisions such as A:[.75,.75], B:[.75,.45].
	const size_t assignmentCount = size_t{1} << mineCount;
	std::vector<double> best(assignmentCount, 0.0);
	for(const auto & enemy : enemyValues)
	{
		std::vector<double> next = best; // This enemy may remain unassigned.
		for(size_t assignment = 0; assignment < assignmentCount; ++assignment)
		{
			for(size_t mine = 0; mine < mineCount; ++mine)
			{
				if(assignment & (size_t{1} << mine))
					continue;
				const size_t withMine = assignment | (size_t{1} << mine);
				next[withMine] = std::max(next[withMine], best[assignment] + enemy[mine]);
			}
		}
		best.swap(next);
	}

	return *std::max_element(best.begin(), best.end());
}

std::vector<LandMineHexScore> legalLandMineHexes(const Mechanics * spellMechanics)
{
	std::vector<LandMineHexScore> result;
	const auto * battle = spellMechanics->battle();
	const auto accessibility = battle->getAccessibility();
	const auto enemies = landMineEnemies(spellMechanics);
	const auto allies = landMineAllies(spellMechanics);
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!canonicalLandMineHexIsEmpty(*battle, accessibility, hex))
			continue;
		result.push_back(scoreLandMineHex(spellMechanics, hex, enemies, allies));
	}
	return result;
}

bool targetUsesDistinctLegalLandMineHexes(const Mechanics * spellMechanics, const Target & target)
{
	const auto * battle = spellMechanics->battle();
	const auto accessibility = battle->getAccessibility();
	std::set<int> seen;
	for(const auto & destination : target)
	{
		if(destination.unitValue != nullptr
			|| !destination.hexValue.isValid()
			|| !seen.insert(destination.hexValue.toInt()).second
			|| !canonicalLandMineHexIsEmpty(*battle, accessibility, destination.hexValue))
			return false;
	}
	return true;
}

std::vector<Target> physicalObstacleTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	std::set<BattleHex> seen;

	for(const auto & obstacle : spellMechanics->battle()->battleGetAllObstacles())
	{
		if(!obstacle || !isPhysicalObstacle(*obstacle))
			continue;

		// Use an affected tile so the aim is a real battlefield hex and the
		// authoritative validator can resolve the object by position.
		const auto affectedTiles = obstacle->getAffectedTiles();
		if(affectedTiles.empty())
			continue;

		const auto aimHex = affectedTiles.front();
		if(!aimHex.isValid() || !seen.insert(aimHex).second)
			continue;

		Target target{Destination(aimHex)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	return result;
}

std::vector<Target> stormOfDaggersTargets(Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!spellMechanics || !spellMechanics->battle())
		return result;

	constexpr size_t maxTargets = 5;
	std::vector<const battle::Unit *> enemies;
	std::set<uint32_t> seenUnitIds;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || !unit->isValidTarget(false)
			|| spellMechanics->battle()->battleGetOwner(unit) == spellMechanics->getCasterColor()
			|| !seenUnitIds.insert(unit->unitId()).second)
			continue;
		enemies.push_back(unit);
	}
	std::sort(enemies.begin(), enemies.end(), [](const battle::Unit * lhs, const battle::Unit * rhs)
	{
		return lhs->unitId() < rhs->unitId();
	});

	// Keep one best direct-damage subset for each target count. The shared
	// mechanics supplies the ranked, target-adjusted damage for that count, and
	// BattleAI's ordinary damage-reduction score ranks the independent targets.
	// This is exact for the separable expected direct-health score at a fixed N:
	// selecting the N highest individual values maximizes their sum. The full
	// hypothetical pass compares at most five casts instead of projecting every
	// combination (C(21, 1..5) = 27,895 at a full enemy stack count). Final AI
	// valuation remains approximate for cross-target effects: castEval projects
	// all targets as hits while MR discounts direct hostile-health score, omits
	// reflection branches, and different kills can change the active stack's
	// follow-up action. Record MR/reflection projection and kill/follow-up search
	// as Phase 2 interactions.
	const auto * battle = spellMechanics->battle();
	auto battleState = std::shared_ptr<CBattleInfoCallback>(
		const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	DamageCache damageCache;
	struct Candidate
	{
		const battle::Unit * unit;
		float value;
	};
	for(size_t targetCount = 1; targetCount <= std::min(maxTargets, enemies.size()); ++targetCount)
	{
		if(!spellMechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(targetCount)))
			continue;

		std::vector<Candidate> rankedEnemies;
		rankedEnemies.reserve(enemies.size());
		for(const auto * enemy : enemies)
		{
			// Filter individually before ranking: an illegal high-value target must
			// not occupy a slot in the best-N set and hide legal alternatives.
			Target singleton{Destination(enemy)};
			detail::ProblemImpl singletonProblem;
			if(!spellMechanics->canBeCastAt(singleton, singletonProblem))
				continue;

			const auto adjustedDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(enemy));
			const auto cappedDamage = std::min<uint64_t>(static_cast<uint64_t>(adjustedDamage),
				static_cast<uint64_t>(std::max<int64_t>(0, enemy->getAvailableHealth())));
			const auto damageValue = AttackPossibility::calculateDamageReduce(
				nullptr, enemy, cappedDamage, damageCache, battleState);
			const float expectedHitChance = spellApplicationChance(spellMechanics, enemy);
			rankedEnemies.push_back({enemy, damageValue * expectedHitChance});
		}
		if(rankedEnemies.size() < targetCount)
			continue;
		std::sort(rankedEnemies.begin(), rankedEnemies.end(), [](const Candidate & lhs, const Candidate & rhs)
		{
			if(lhs.value != rhs.value)
				return lhs.value > rhs.value;
			return lhs.unit->unitId() < rhs.unit->unitId();
		});

		Target selected;
		selected.reserve(targetCount);
		for(size_t index = 0; index < targetCount; ++index)
			selected.emplace_back(rankedEnemies[index].unit);

		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(selected, problem))
			result.push_back(std::move(selected));
	}
	return result;
}

std::vector<Target> enumerateSoulChainTargets(Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!isCanonicalSoulChain(spellMechanics) || !spellMechanics->battle())
		return result;

	const auto casterSide = spellMechanics->getCasterSide();
	const auto * battle = spellMechanics->battle();
	std::vector<const battle::Unit *> enemies;
	for(const auto * unit : battle->battleGetAllUnits(false))
	{
		if(!isSoulChainTargetUnit(spellMechanics, unit))
			continue;
		enemies.push_back(unit);
	}
	std::sort(enemies.begin(), enemies.end(), [](const battle::Unit * lhs, const battle::Unit * rhs)
	{
		return lhs->unitId() < rhs->unitId();
	});
	if(enemies.empty())
		return result;

	auto battleState = std::shared_ptr<CBattleInfoCallback>(
		const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	DamageCache damageCache;
	std::map<uint32_t, int64_t> expectedTriggerDamage;
	for(const auto * enemy : enemies)
		expectedTriggerDamage.emplace(enemy->unitId(), projectedSoulChainTriggerDamage(
			spellMechanics, enemy, damageCache, battleState));

	const auto echoBasisPoints = soulChainEchoBasisPoints(spellMechanics);
	const auto completeTargetIsLegal = [&](const Target & target)
	{
		if(!newHorizonsSoulChain::validEnemyTargetSet(*battle, casterSide, target))
			return false;
		detail::ProblemImpl problem;
		return spellMechanics->canBeCastAt(target, problem);
	};

	struct SecondaryCandidate
	{
		const battle::Unit * unit = nullptr;
		float value = 0.0f;
	};
	constexpr size_t MAX_SECONDARY_CANDIDATES_PER_PRIMARY = 6;

	for(const auto * primary : enemies)
	{
		Target primaryOnly{Destination(primary)};
		if(!completeTargetIsLegal(primaryOnly))
			continue;

		// Runtime permits one primary with no secondary; it is intentionally a
		// zero-value option, but keeping it here reflects the canonical 1–3 shape.
		result.push_back(primaryOnly);

		std::vector<SecondaryCandidate> rankedSecondaries;
		for(const auto * secondary : enemies)
		{
			if(secondary->unitId() == primary->unitId())
				continue;
			const auto triggerDamage = expectedTriggerDamage.at(secondary->unitId());
			if(triggerDamage <= 0)
				continue;

			const float value = expectedSoulChainEchoValue(spellMechanics, primary, secondary,
				triggerDamage, echoBasisPoints, damageCache, battleState);
			if(value > 0.0f)
				rankedSecondaries.push_back({secondary, value});
		}
		std::sort(rankedSecondaries.begin(), rankedSecondaries.end(), [](const auto & lhs, const auto & rhs)
		{
			if(lhs.value != rhs.value)
				return lhs.value > rhs.value;
			return lhs.unit->unitId() < rhs.unit->unitId();
		});

		// Retain only the best legal one-secondary set for each primary. This
		// keeps BattleEvaluator from projecting every ordered pair.
		for(const auto & candidate : rankedSecondaries)
		{
			Target selected{Destination(primary), Destination(candidate.unit)};
			if(completeTargetIsLegal(selected))
			{
				result.push_back(std::move(selected));
				break;
			}
		}

		// The two-secondary value is separable before the primary's health cap,
		// so the strongest individual echo contributions supply the best pair.
		// Search a small fallback prefix in case spell-specific validation rejects
		// the first combination; candidate count remains bounded by 3*N.
		const auto fallbackCount = std::min(MAX_SECONDARY_CANDIDATES_PER_PRIMARY,
			rankedSecondaries.size());
		float bestPairValue = 0.0f;
		Target bestPair;
		for(size_t first = 0; first < fallbackCount; ++first)
			for(size_t second = first + 1; second < fallbackCount; ++second)
			{
				Target selected{Destination(primary), Destination(rankedSecondaries[first].unit),
					Destination(rankedSecondaries[second].unit)};
				const float value = rankedSecondaries[first].value + rankedSecondaries[second].value;
				if(value <= bestPairValue || !completeTargetIsLegal(selected))
					continue;
				bestPairValue = value;
				bestPair = std::move(selected);
			}
		if(!bestPair.empty())
			result.push_back(std::move(bestPair));
	}

	return result;
}
}

std::vector<Target> SpellTargetEvaluator::canonicalSoulChainTargets(Mechanics * spellMechanics)
{
	return enumerateSoulChainTargets(spellMechanics);
}

std::vector<Target> SpellTargetEvaluator::getViableTargets(Mechanics * spellMechanics)
{
	if(spellMechanics && spellMechanics->getSpell()
		&& spellMechanics->getSpell()->getJsonKey() == newHorizonsPandemonium::SPELL_KEY)
	{
		if(!newHorizonsPandemonium::enabled(*spellMechanics))
			return {};
		detail::ProblemImpl problem;
		const Target target;
		return spellMechanics->canBeCastAt(target, problem)
			? std::vector<Target>{target} : std::vector<Target>{};
	}
	if(spellMechanics && spellMechanics->getSpell()
		&& spellMechanics->getSpell()->getJsonKey() == newHorizonsNaturesWrath::SPELL_KEY)
	{
		std::vector<Target> result;
		if(!newHorizonsNaturesWrath::enabled(*spellMechanics))
			return result;
		// A healthy or immune first conductor can still produce valuable later
		// recipients. Only the shared cast legality may exclude a starting stack.
		for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
		{
			if(!newHorizonsNaturesWrath::validConductor(unit))
				continue;
			Target target{Destination(unit)};
			detail::ProblemImpl problem;
			if(spellMechanics->canBeCastAt(target, problem))
				result.push_back(std::move(target));
		}
		return result;
	}
	// Installed spell content must not broaden a pre-v3 saved battle's target
	// roster. For active v3 casts, let the authoritative heal effect decide which
	// friendly stacks have usable remains, wounds, and an accessible corpse hex.
	if(spellMechanics)
	{
		const auto * spell = spellMechanics->getSpell();
		const auto * callback = spellMechanics->battle();
		const auto * battle = callback ? callback->getBattle() : nullptr;
		if(spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_REANIMATE_SPELL
			&& (!battle || !newHorizonsMagic::reanimateEnabled(battle->getMagicRules(), spell->getId())))
			return {};
		if(spell && spell->getJsonKey() == newHorizonsMagic::SHADOW_SOUL_REAPER_SPELL
			&& (!battle || !newHorizonsMagic::soulReaperEnabled(battle->getMagicRules(), spell->getId())))
			return {};
	}

	// Handle this identity before the saved-profile helpers below query the
	// numeric spell ID. The canonical New Horizons spell has a content key of
	// its own and needs no legacy/core-ID translation for target enumeration.
	if(isCanonicalLifeDrain(spellMechanics))
	{
		if(spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::CREATURE, AimType::CREATURE})
			return {};
		return canonicalLifeDrainTargets(spellMechanics);
	}
	if(isHandOfFateSpell(spellMechanics))
		return canonicalHandOfFateTargets(spellMechanics);
	if(isCanonicalHydrasVitality(spellMechanics))
		return canonicalHydrasVitalityTargets(spellMechanics);
	if(isCanonicalVerdantPrison(spellMechanics))
		return canonicalVerdantPrisonTargets(spellMechanics);
	if(isCanonicalSanctuary(spellMechanics))
		return canonicalSanctuaryTargets(spellMechanics);
	if(isVengefulVines(spellMechanics))
		return canonicalVengefulVinesTargets(spellMechanics);
	if(isCanonicalPurify(spellMechanics))
	{
		std::vector<Target> purifyTargets;
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex center(index);
			if(!center.isValid())
				continue;

			Target target{Destination(center)};
			detail::ProblemImpl problem;
			if(spellMechanics->canBeCastAt(target, problem)
				&& purifySelection(spellMechanics, target).value > 0.0f)
				purifyTargets.push_back(std::move(target));
		}
		return purifyTargets;
	}
	if(spellMechanics && spellMechanics->isNewHorizonsStormOfDaggers())
		return stormOfDaggersTargets(spellMechanics);
	if(isCanonicalSoulChain(spellMechanics))
		return canonicalSoulChainTargets(spellMechanics);
	if(isCanonicalNaturePoison(spellMechanics))
		return canonicalNaturePoisonTargets(spellMechanics);
	if(isCanonicalSpellLock(spellMechanics))
		return canonicalSpellLockTargets(spellMechanics);
	if(isCanonicalTimeStop(spellMechanics))
		return canonicalTimeStopTargets(spellMechanics);
	if(isCanonicalFireWall(spellMechanics))
		return canonicalFireWallTargets(spellMechanics);
	if(isCanonicalLandMine(spellMechanics))
		return canonicalLandMineTargets(spellMechanics);
	if(spellMechanics && spellMechanics->usesNewHorizonsHavocStructures()
		&& spellMechanics->getTargetTypes() == std::vector<AimType>{AimType::LOCATION})
		return canonicalHavocStructureTargets(spellMechanics);
	if(spellMechanics && spellMechanics->usesNewHorizonsEarthquake())
		return canonicalEarthquakeTargets(spellMechanics);
	if(isSelectedQuicksand(spellMechanics))
		return canonicalQuicksandTargets(spellMechanics);

	std::vector<Target> result;
	std::vector<AimType> targetTypes = spellMechanics->getTargetTypes();
	if(isTransfigureMatter(spellMechanics))
		return physicalObstacleTargets(spellMechanics);
	if(isCanonicalSummonTrolls(spellMechanics))
		return canonicalSummonTrollsTargets(spellMechanics);

	if(targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::LOCATION})
		return creatureLocationTargets(spellMechanics);
	if(targetTypes == std::vector<AimType>{AimType::CREATURE, AimType::CREATURE})
		return creaturePairTargets(spellMechanics);
	if(targetTypes.size() != 1)
		return result;

	auto targetType = targetTypes.front();

	switch(targetType)
	{
		case AimType::CREATURE:
			return allTargetableCreatures(spellMechanics, true);
		case AimType::LOCATION:
		{
			if(spellMechanics->isNeutralSpell())
				return defaultLocationSpellHeuristics(
					spellMechanics
				); // theoretically anything can be a useful destination, so we balance performance and validity
			else
				return theBestLocationCasts(spellMechanics);
		}
		case AimType::NOTHING:
			return std::vector<Target>(1); //default-constructed target means cast without destination
		default:
			return result;
	}
}

std::vector<Target> SpellTargetEvaluator::canonicalEarthquakeTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!spellMechanics || !spellMechanics->usesNewHorizonsEarthquake()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::LOCATION})
		return result;

	// A location candidate is the selected center for field mode and the
	// selected wall section for siege mode. Keep every geometrically distinct
	// legal hex: terrain footprints can differ even when the creature victims do
	// not, and an empty unit footprint is still meaningful in siege mode.
	result.reserve(GameConstants::BFIELD_SIZE);
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isValid())
			continue;

		Target target{Destination(hex)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::canonicalHavocStructureTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!spellMechanics || !spellMechanics->usesNewHorizonsHavocStructures()
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::LOCATION})
		return result;

	const auto * battle = spellMechanics->battle();
	if(!battle)
		return result;

	// Preserve the existing creature-damage optimums, then add only geometry
	// signatures that change the affected units, live fortification sections, or
	// ordinary-scene obstacles. This avoids detached castEval for every hex.
	result = theBestLocationCasts(spellMechanics);
	using GeometrySignature = std::tuple<std::vector<uint32_t>, std::vector<int>, std::vector<int32_t>>;
	std::set<GeometrySignature> structuralSignatures;
	std::set<BattleHex> selectedHexes;

	auto signatureFor = [&](const Target & target)
	{
		GeometrySignature signature;
		bool hasStructure = false;
		if(target.size() != 1 || !target.front().hexValue.isValid())
			return std::pair{signature, hasStructure};

		std::set<uint32_t> affectedUnits;
		for(const auto * unit : spellMechanics->getAffectedStacks(target))
			if(unit)
				affectedUnits.insert(unit->unitId());
		std::get<0>(signature).assign(affectedUnits.begin(), affectedUnits.end());

		const auto affectedHexes = spellMechanics->rangeInHexes(target.front().hexValue);
		std::set<int> affectedWallParts;
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto wallPart = static_cast<EWallPart>(index);
			const auto wallHex = battle->wallPartToBattleHex(wallPart);
			if(battle->isWallPartAttackable(wallPart) && battle->getWallStructuralHP(wallPart) > 0
				&& wallHex.isValid() && affectedHexes.contains(wallHex))
				affectedWallParts.insert(static_cast<int>(wallPart));
		}
		std::get<1>(signature).assign(affectedWallParts.begin(), affectedWallParts.end());

		std::set<int32_t> affectedObstacles;
		for(const auto & obstacle : battle->battleGetAllObstacles())
		{
			if(!obstacle || obstacle->obstacleType != CObstacleInstance::USUAL)
				continue;
			for(const auto & hex : obstacle->getAffectedTiles())
				if(affectedHexes.contains(hex))
				{
					affectedObstacles.insert(obstacle->uniqueID);
					break;
				}
		}
		std::get<2>(signature).assign(affectedObstacles.begin(), affectedObstacles.end());
		hasStructure = !affectedWallParts.empty() || !affectedObstacles.empty();
		return std::pair{std::move(signature), hasStructure};
	};

	for(const auto & target : result)
	{
		if(target.size() != 1 || !target.front().hexValue.isValid())
			continue;
		selectedHexes.insert(target.front().hexValue);
		auto [signature, hasStructure] = signatureFor(target);
		if(hasStructure)
			structuralSignatures.insert(std::move(signature));
	}

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isValid() || selectedHexes.contains(hex))
			continue;

		Target target{Destination(hex)};
		detail::ProblemImpl problem;
		if(!spellMechanics->canBeCastAt(target, problem))
			continue;

		auto [signature, hasStructure] = signatureFor(target);
		if(hasStructure && structuralSignatures.insert(std::move(signature)).second)
		{
			selectedHexes.insert(hex);
			result.push_back(std::move(target));
		}
	}

	return result;
}

int SpellTargetEvaluator::physicalTravelDistance(const ReachabilityInfo & reachability, BattleHex destination)
{
	const auto start = reachability.params.startPosition;
	if(!start.isValid() || !destination.isValid() || !reachability.isReachable(destination))
		return -1;

	if(reachability.params.flying)
		return BattleHex::getDistance(start, destination);

	int distance = 0;
	BattleHex current = destination;
	while(current != start && distance < GameConstants::BFIELD_SIZE)
	{
		current = reachability.predecessors[current.toInt()];
		if(!current.isValid())
			return -1;
		++distance;
	}
	return current == start ? distance : -1;
}

std::optional<float> SpellTargetEvaluator::earthquakeStructuralHPValue(
	const Mechanics * spellMechanics,
	const Target & target,
	const Environment * environment,
	std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!spellMechanics || !spellMechanics->usesNewHorizonsEarthquake()
		|| spellMechanics->getUnitCaster() != nullptr || target.size() != 1
		|| target.front().unitValue || !target.front().hexValue.isValid())
		return std::nullopt;

	const auto * callback = spellMechanics->battle();
	const auto * spell = dynamic_cast<const CSpell *>(spellMechanics->getSpell());
	if(!callback || !spell || !environment)
		return std::nullopt;
	if(!battleState)
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(callback), [](CBattleInfoCallback *) {});
	if(!battleState->hasFortifications())
		return std::nullopt;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	const auto casterSide = spellMechanics->getCasterSide();
	if((casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		|| !spellMechanics->getHeroCaster())
		return std::nullopt;

	// Re-resolve the same scripted spell against a detached battle. This leaves
	// section selection, nearest-section ordering, Geomancer scaling and actual
	// structural damage in the Lua/runtime implementation instead of duplicating
	// an Earthquake planner in the AI.
	HypotheticBattle projectedBattle(environment, battleState);
	BattleCast projectedCast(&projectedBattle, spellMechanics->getHeroCaster(), Mode::HERO, spell);
	auto projectedMechanics = spell->battleMechanics(&projectedCast);
	detail::ProblemImpl projectedProblem;
	if(!projectedMechanics->canBeCastAt(target, projectedProblem))
		return 0.0f;
	projectedMechanics->castEval(projectedBattle.getServerCallback(), target);

	float structuralValue = 0.0f;
	for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
	{
		const auto part = static_cast<EWallPart>(index);
		const auto before = std::max(0, battleState->getWallStructuralHP(part));
		const auto after = std::max(0, projectedBattle.getWallStructuralHP(part));
		if(before <= 0 || after >= before)
			continue;

		const auto actualDamage = std::min(before, before - after);
		structuralValue += static_cast<float>(actualDamage) / static_cast<float>(before);
	}

	// Fortifications belong to the defending side: structural progress advances
	// the attacker and weakens the defender. Keep this a distinct structure score
	// rather than disguising wall HP as creature health or scoring the wrong side.
	return casterSide == BattleSide::ATTACKER ? structuralValue : -structuralValue;
}

std::optional<float> SpellTargetEvaluator::havocStructuralHPValue(
	const Mechanics * spellMechanics,
	const Target & target)
{
	if(!spellMechanics || !spellMechanics->usesNewHorizonsHavocStructures())
		return std::nullopt;

	const auto * callback = spellMechanics->battle();
	if(!callback || !callback->hasFortifications())
		return 0.0f;

	const auto casterSide = spellMechanics->getCasterSide();
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return std::nullopt;

	std::set<EWallPart> affectedParts;
	if(spellMechanics->isMassive())
	{
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto part = static_cast<EWallPart>(index);
			if(callback->isWallPartAttackable(part) && callback->getWallStructuralHP(part) > 0
				&& callback->wallPartToBattleHex(part).isValid())
				affectedParts.insert(part);
		}
	}
	else
	{
		if(target.size() != 1 || !target.front().hexValue.isValid())
			return 0.0f;
		detail::ProblemImpl problem;
		if(!spellMechanics->canBeCastAt(target, problem))
			return 0.0f;

		const auto affectedHexes = spellMechanics->rangeInHexes(target.front().hexValue);
		for(int index = 0; index < static_cast<int>(EWallPart::PARTS_COUNT); ++index)
		{
			const auto part = static_cast<EWallPart>(index);
			const auto wallHex = callback->wallPartToBattleHex(part);
			if(callback->isWallPartAttackable(part) && callback->getWallStructuralHP(part) > 0
				&& wallHex.isValid() && affectedHexes.contains(wallHex))
				affectedParts.insert(part);
		}
	}

	const int32_t structuralDamage = spellMechanics->getNewHorizonsHavocStructuralDamage();
	if(structuralDamage <= 0)
		return 0.0f;

	float structuralValue = 0.0f;
	for(const auto part : affectedParts)
	{
		const auto currentHP = std::max(0, callback->getWallStructuralHP(part));
		if(currentHP <= 0)
			continue;
		const auto actualDamage = std::min(currentHP, structuralDamage);
		structuralValue += static_cast<float>(actualDamage) / static_cast<float>(currentHP);
	}

	// Fortifications are the defender's property, regardless of the side
	// currently considering the cast.
	return casterSide == BattleSide::ATTACKER ? structuralValue : -structuralValue;
}

SpellTargetEvaluator::PurifySelection SpellTargetEvaluator::purifySelection(
	const Mechanics * spellMechanics, const Target & target)
{
	PurifySelection selection;
	if(!isCanonicalPurify(spellMechanics) || target.size() != 1
		|| target.front().unitValue != nullptr || !target.front().hexValue.isValid())
		return selection;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return selection;

	const auto * callback = spellMechanics->battle();
	const auto * hero = spellMechanics->getHeroCaster();
	const int32_t spellPower = hero
		? std::max<int32_t>(0, hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER))
		: std::max<int32_t>(0, spellMechanics->getEffectPower());
	const bool purifierPerk = newHorizonsPurify::hasPurifierPerk(hero);
	const auto eligibleStacks = newHorizonsPurify::eligibleStacks(*callback,
		spellMechanics->getCasterSide(), target.front().hexValue, spellPower, purifierPerk);
	auto battleState = std::shared_ptr<CBattleInfoCallback>(
		const_cast<CBattleInfoCallback *>(callback), [](CBattleInfoCallback *) {});
	DamageCache damageCache;

	struct RankedGroup
	{
		SpellID spell = SpellID::NONE;
		float value = 0.0f;
	};

	for(const auto & eligible : eligibleStacks)
	{
		const auto * unit = callback->battleGetUnitByID(static_cast<uint32_t>(eligible.unitId));
		if(!unit || !unit->alive() || unit->unitSide() != spellMechanics->getCasterSide())
			continue;

		float physicalPoisonValue = 0.0f;
		if(eligible.physicalPoison || eligible.physicalPoisonAutomaticallyCleared)
		{
			const auto * liveState = dynamic_cast<const battle::CUnitState *>(unit);
			if(liveState)
			{
				auto poisonState = liveState->acquireState();
				int64_t remainingHealth = std::max<int64_t>(0, unit->getAvailableHealth());
				int64_t preventedDamage = 0;
				while(remainingHealth > 0 && poisonState->physicalPoisonActivationsRemaining > 0)
				{
					const auto tick = newHorizonsBulwark::physicalPoisonTickDamage(poisonState.get());
					const auto applied = std::min(remainingHealth, tick);
					preventedDamage += applied;
					remainingHealth -= applied;
					newHorizonsBulwark::advancePhysicalPoison(poisonState.get());
				}
				if(preventedDamage > 0)
				{
					physicalPoisonValue = AttackPossibility::calculateDamageReduce(nullptr, unit,
						static_cast<uint64_t>(preventedDamage), damageCache, battleState);
					if(physicalPoisonValue <= 0.0f)
						physicalPoisonValue = static_cast<float>(preventedDamage);
				}
			}
		}
		if(eligible.physicalPoisonAutomaticallyCleared && physicalPoisonValue > 0.0f)
		{
			selection.value += physicalPoisonValue;
			selection.physicalPoisonStackIds.push_back(eligible.unitId);
		}

		std::vector<RankedGroup> groups;
		if(eligible.physicalPoison && !eligible.physicalPoisonAutomaticallyCleared
			&& physicalPoisonValue > 0.0f)
			groups.push_back({newHorizonsPurify::physicalPoisonChoiceID(), physicalPoisonValue});
		for(const auto effectSpell : eligible.spellEffectGroups)
		{
			const auto * statusSpell = effectSpell.toSpell();
			if(!statusSpell || !statusSpell->isMagical() || !statusSpell->isNegative())
				continue;

			const auto bonuses = newHorizonsPurify::spellEffectGroupBonuses(unit, effectSpell);
			if(bonuses.empty())
				continue;

			int remainingRounds = 0;
			bool timed = false;
			for(const auto & bonus : bonuses)
				if(Bonus::NTurns(&bonus))
				{
					timed = true;
					remainingRounds = std::max(remainingRounds,
						std::max<int32_t>(0, bonus.turnsRemain));
				}

			// Reuse Spell Lock's stack-health scale, then prioritize higher-rank and
			// longer-lived groups when a stack has more eligible effects than its cap.
			const float health = static_cast<float>(std::max<int64_t>(1, unit->getAvailableHealth()));
			const float levelFactor = 0.75f + 0.05f
				* static_cast<float>(std::clamp(statusSpell->getLevel(), 1, 5));
			const float durationFactor = timed
				? std::clamp(0.4f + 0.2f * static_cast<float>(remainingRounds), 0.4f, 1.0f)
				: 1.0f;
			groups.push_back({effectSpell, health * 0.9f * levelFactor * durationFactor});
		}
		std::sort(groups.begin(), groups.end(), [](const RankedGroup & lhs, const RankedGroup & rhs)
		{
			if(lhs.value != rhs.value)
				return lhs.value > rhs.value;
			return lhs.spell.getNum() < rhs.spell.getNum();
		});

		const auto choiceCount = std::min<size_t>(
			static_cast<size_t>(std::max(0, eligible.maximumSpellEffectChoices)), groups.size());
		for(size_t index = 0; index < choiceCount; ++index)
		{
			selection.spellEffectGroups.emplace_back(eligible.unitId, groups[index].spell);
			selection.value += groups[index].value;
			if(groups[index].spell == newHorizonsPurify::physicalPoisonChoiceID())
				selection.physicalPoisonStackIds.push_back(eligible.unitId);
		}
	}

	std::sort(selection.spellEffectGroups.begin(), selection.spellEffectGroups.end(),
		[](const auto & lhs, const auto & rhs)
		{
			if(lhs.first != rhs.first)
				return lhs.first < rhs.first;
			return lhs.second.getNum() < rhs.second.getNum();
		});
	std::sort(selection.physicalPoisonStackIds.begin(), selection.physicalPoisonStackIds.end());
	selection.physicalPoisonStackIds.erase(std::unique(selection.physicalPoisonStackIds.begin(),
		selection.physicalPoisonStackIds.end()), selection.physicalPoisonStackIds.end());
	return selection;
}

std::vector<Target> SpellTargetEvaluator::canonicalNaturePoisonTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	if(!isCanonicalNaturePoison(spellMechanics))
		return result;

	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		if(!isNaturePoisonTarget(spellMechanics, unit))
			continue;

		Target target{Destination(unit)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}

	return result;
}

std::vector<Target> SpellTargetEvaluator::canonicalSpellLockTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		if(!unit || !unit->alive() || !unit->isValidTarget(false) || isSpellLocked(unit))
			continue;

		Target target{Destination(unit)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::canonicalTimeStopTargets(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(!hex.isValid())
			continue;

		Target target{Destination(hex)};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(std::move(target));
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::canonicalFireWallTargets(const Mechanics * spellMechanics)
{
	std::vector<Target> result;
	const auto * battle = spellMechanics->battle();
	const auto accessibility = battle->getAccessibility();

	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex start(index);
		if(!canonicalLandMineHexIsEmpty(*battle, accessibility, start))
			continue;

		for(const auto direction : BattleHex::hexagonalDirections())
		{
			Target line;
			BattleHex current = start;
			bool legal = true;
			for(int length = 0; length < 3; ++length)
			{
				if(!canonicalLandMineHexIsEmpty(*battle, accessibility, current))
				{
					legal = false;
					break;
				}
				line.emplace_back(current);
				if(length != 2)
					current = current.cloneInDirection(direction, false);
			}
			if(!legal)
				continue;

			detail::ProblemImpl problem;
			if(spellMechanics->canBeCastAt(line, problem))
				result.push_back(std::move(line));
		}
	}

	return result;
}

std::vector<Target> SpellTargetEvaluator::canonicalLandMineTargets(const Mechanics * spellMechanics)
{
	const int required = spellMechanics->getNewHorizonsLandMinePatchCount();
	auto candidates = legalLandMineHexes(spellMechanics);
	if(candidates.size() < static_cast<size_t>(required))
		return {};

	// Greedy coverage keeps the target vector useful against multiple hostile
	// ground stacks instead of selecting four adjacent hexes around one stack
	// solely because they happen to have the same local score.  Ties are broken
	// by battlefield index, making replay/network output deterministic.
	std::vector<LandMineHexScore> selected;
	std::set<uint32_t> coveredEnemies;
	for(int index = 0; index < required; ++index)
	{
		auto best = candidates.end();
		int64_t bestScore = std::numeric_limits<int64_t>::min();
		for(auto candidate = candidates.begin(); candidate != candidates.end(); ++candidate)
		{
			if(vstd::contains_if(selected, [&](const LandMineHexScore & previous)
			{
				return previous.hex == candidate->hex;
			}))
				continue;

			int64_t score = candidate->score;
			for(const auto enemy : candidate->adjacentEnemies)
				if(!coveredEnemies.contains(enemy))
					score += 5000;

			if(best == candidates.end() || score > bestScore
				|| (score == bestScore && candidate->hex.toInt() < best->hex.toInt()))
			{
				best = candidate;
				bestScore = score;
			}
		}

		if(best == candidates.end())
			return {};
		selected.push_back(*best);
		coveredEnemies.insert(best->adjacentEnemies.begin(), best->adjacentEnemies.end());
	}

	Target result;
	result.reserve(selected.size());
	for(const auto & candidate : selected)
		result.emplace_back(candidate.hex);

	// The strict live-state predicate above mirrors the server's fast rejection
	// path.  The spell script remains the final local check for any content-level
	// applicability rule, and only a complete vector is ever returned.
	detail::ProblemImpl problem;
	if(!targetUsesDistinctLegalLandMineHexes(spellMechanics, result)
		|| !spellMechanics->canBeCastAt(result, problem))
		return {};

	return {std::move(result)};
}

namespace
{
float quicksandHexPressure(const Mechanics * mechanics, const BattleHex & hex)
{
	float pressure = 0.0f;
	for(const auto * unit : mechanics->battle()->battleGetAllUnits(false))
	{
		if(!isGroundHostile(mechanics, unit) && !isGroundAlly(mechanics, unit))
			continue;

		const int distance = distanceToUnit(hex, unit);
		if(distance == std::numeric_limits<int>::max())
			continue;
		const int movement = std::max(0, static_cast<int>(unit->getMovementRange(0)));
		const float likelihood = distance <= 1 ? 0.75f
			: distance <= movement + 1 ? 0.40f
			: distance <= movement + 3 ? 0.15f : 0.0f;
		const float unitValue = std::max<int64_t>(1, unit->getAvailableHealth())
			* likelihood * (unit->willMove() ? 1.0f : 0.5f);
		pressure += isGroundHostile(mechanics, unit) ? unitValue : -1.5f * unitValue;
	}
	return pressure;
}
}

std::vector<Target> SpellTargetEvaluator::canonicalQuicksandTargets(const Mechanics * spellMechanics)
{
	const int required = spellMechanics->getNewHorizonsQuicksandPatchCount();
	if(!isSelectedQuicksand(spellMechanics) || required <= 0)
		return {};

	std::vector<std::pair<float, BattleHex>> candidates;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		const BattleHex hex(index);
		if(newHorizonsMagic::quicksandPlacementHexIsLegal(*spellMechanics->battle(), hex))
			candidates.emplace_back(quicksandHexPressure(spellMechanics, hex), hex);
	}
	if(candidates.size() < static_cast<size_t>(required))
		return {};
	std::sort(candidates.begin(), candidates.end(), [](const auto & left, const auto & right)
	{
		return left.first == right.first
			? left.second.toInt() < right.second.toInt()
			: left.first > right.first;
	});
	if(candidates.front().first <= 0.0f)
		return {};

	Target result;
	result.reserve(required);
	for(int index = 0; index < required; ++index)
		result.emplace_back(candidates[index].second);

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(result, problem))
		return {};
	return {std::move(result)};
}

float SpellTargetEvaluator::quicksandPlacementValue(const Mechanics * spellMechanics,
	const Target & target)
{
	if(!isSelectedQuicksand(spellMechanics)
		|| static_cast<int>(target.size()) != spellMechanics->getNewHorizonsQuicksandPatchCount())
		return 0.0f;

	std::set<int> seen;
	float pressure = 0.0f;
	for(const auto & destination : target)
	{
		if(destination.unitValue != nullptr
			|| !destination.hexValue.isValid()
			|| !seen.insert(destination.hexValue.toInt()).second
			|| !newHorizonsMagic::quicksandPlacementHexIsLegal(*spellMechanics->battle(), destination.hexValue))
			return 0.0f;
		pressure += quicksandHexPressure(spellMechanics, destination.hexValue);
	}

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;
	// The same delayed-effect discount applies to every patch, keeping this
	// comparable to a direct attack without pretending that it deals damage.
	return std::max(0.0f, pressure * 0.20f);
}

float SpellTargetEvaluator::landMinePlacementValue(const Mechanics * spellMechanics,
	const Target & target, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalLandMine(spellMechanics)
		|| target.empty()
		|| static_cast<int>(target.size()) != spellMechanics->getNewHorizonsLandMinePatchCount()
		|| !targetUsesDistinctLegalLandMineHexes(spellMechanics, target))
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	const auto enemies = landMineEnemies(spellMechanics);
	if(enemies.empty())
		return 0.0f;

	// AttackPossibility is the BattleAI's common damage-reduction currency.  Use
	// the live callback when the caller owns it (BattleEvaluator does), while
	// retaining a non-owning fallback for small targeting-only callers/tests.
	if(!battleState)
	{
		const auto * battle = spellMechanics->battle();
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	}
	DamageCache damageCache;
	std::vector<size_t> mineOrder(target.size());
	for(size_t index = 0; index < mineOrder.size(); ++index)
		mineOrder[index] = index;
	std::sort(mineOrder.begin(), mineOrder.end(), [&](size_t left, size_t right)
	{
		return target[left].hexValue.toInt() < target[right].hexValue.toInt();
	});
	std::vector<std::vector<double>> enemyValues;
	enemyValues.reserve(enemies.size());

	for(const auto * enemy : enemies)
	{
		// `landMineEnemies` has already removed permanent/absolute immunity,
		// invincibility, zero damage, and 100% resistance.  Magic resistance below
		// remains a probabilistic trigger discount for the surviving targets.
		const int resistance = std::clamp(enemy->magicResistance(), 0, 100);
		const float resistanceFactor = 1.0f - static_cast<float>(resistance) / 100.0f;
		if(resistanceFactor <= 0.0f)
			continue;

		// Evaluate the target-specific damage before the health cap.  This keeps
		// spell damage reduction, protections and caster bonuses consistent with
		// the direct-damage path, then prevents overkill from making a tiny stack
		// look more valuable than the health it can actually lose.
		const auto damage = landMineDamagePotential(spellMechanics, enemy);
		if(damage == 0)
			continue;

		const auto directScale = AttackPossibility::calculateDamageReduce(
			nullptr, enemy, damage, damageCache, battleState);
		const auto enemyValue = directScale * resistanceFactor;
		std::vector<double> contributions;
		contributions.reserve(mineOrder.size());
		for(const auto index : mineOrder)
		{
			const auto triggerChance = landMineTriggerLikelihood(target[index].hexValue, enemy);
			contributions.push_back(static_cast<double>(enemyValue) * triggerChance);
		}
		enemyValues.push_back(std::move(contributions));
	}

	// Normalize row order as well as mine order so equivalent live snapshots
	// produce the same floating-point result even if their enumeration order
	// differs.
	std::sort(enemyValues.begin(), enemyValues.end(), [](const auto & left, const auto & right)
	{
		return std::lexicographical_compare(left.begin(), left.end(), right.begin(), right.end());
	});
	return static_cast<float>(maximumWeightLandMineAssignment(enemyValues, mineOrder.size()));
}

float SpellTargetEvaluator::fireWallPlacementValue(const Mechanics * spellMechanics,
	const Target & target, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalFireWall(spellMechanics) || target.size() != 3)
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	if(!battleState)
	{
		const auto * battle = spellMechanics->battle();
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	}

	const auto enemies = landMineEnemies(spellMechanics);
	const auto allies = landMineAllies(spellMechanics);
	if(enemies.empty())
		return 0.0f;

	DamageCache damageCache;
	float hostileValue = 0.0f;
	for(const auto * enemy : enemies)
	{
		if(!spellMechanics->isReceptive(enemy)
			|| enemy->hasImmunity(spellMechanics->getSpellId())
			|| enemy->hasAbsoluteImmunity(spellMechanics->getSpellId())
			|| enemy->isInvincible())
			continue;

		const int resistance = std::clamp(enemy->magicResistance(), 0, 100);
		const float resistanceFactor = 1.0f - static_cast<float>(resistance) / 100.0f;
		const float triggerChance = fireWallTriggerLikelihood(target, enemy);
		if(resistanceFactor <= 0.0f || triggerChance <= 0.0f)
			continue;

		const auto adjustedDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(enemy));
		const auto damage = std::min<uint64_t>(static_cast<uint64_t>(adjustedDamage), enemy->getAvailableHealth());
		if(damage == 0)
			continue;

		const auto directScale = AttackPossibility::calculateDamageReduce(
			nullptr, enemy, damage, damageCache, battleState);
		hostileValue += directScale * triggerChance * resistanceFactor;
	}

	// Friendly units can also walk through a canonical wall.  Penalize an
	// exposed line more strongly than a hostile line is rewarded so the AI
	// chooses a safer orientation whenever one is available, and declines the
	// spell entirely when every useful line would endanger our army.
	float friendlyPenalty = 0.0f;
	for(const auto * ally : allies)
	{
		if(ally->isInvincible() || !spellMechanics->isReceptive(ally))
			continue;
		const float triggerChance = fireWallTriggerLikelihood(target, ally);
		if(triggerChance <= 0.0f)
			continue;

		const auto adjustedDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(ally));
		const auto damage = std::min<uint64_t>(static_cast<uint64_t>(adjustedDamage), ally->getAvailableHealth());
		if(damage == 0)
			continue;

		const auto directScale = AttackPossibility::calculateDamageReduce(
			nullptr, ally, damage, damageCache, battleState);
		friendlyPenalty += directScale * triggerChance * 1.5f;
	}

	return std::max(0.0f, hostileValue - friendlyPenalty);
}

float SpellTargetEvaluator::timeStopPlacementValue(const Mechanics * spellMechanics,
	const Target & target)
{
	if(!isCanonicalTimeStop(spellMechanics) || target.size() != 1
		|| target.front().unitValue != nullptr || !target.front().hexValue.isValid())
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	float value = 0.0f;
	for(const auto * unit : spellMechanics->getAffectedStacks(target))
	{
		if(!unit || !unit->alive() || unit->isTimeStopped())
			continue;

		const float health = static_cast<float>(std::max<int64_t>(1, unit->getAvailableHealth()));
		const float totalHealth = static_cast<float>(std::max<int64_t>(1, unit->getTotalHealth()));
		const bool enemy = spellMechanics->battle()->battleGetOwner(unit) != spellMechanics->getCasterColor();
		if(enemy)
		{
			// A unit that is ready to move is a more immediate tactical threat.
			const float actionPressure = unit->willMove() ? 1.25f : 0.85f;
			value += health * actionPressure;
		}
		else
		{
			// Stopping a healthy ally is usually a bad exchange.  An injured ally
			// can still be protected from the next enemy action, so let that use
			// become positive once enough health is missing.
			const float missingHealth = std::clamp(1.0f - health / totalHealth, 0.0f, 1.0f);
			value += health * (missingHealth * 1.8f - 0.65f);
		}
	}

	return value;
}

float SpellTargetEvaluator::spellLockPlacementValue(const Mechanics * spellMechanics,
	const Target & target)
{
	if(!isCanonicalSpellLock(spellMechanics) || target.size() != 1 || !target.front().unitValue)
		return 0.0f;

	const auto * unit = target.front().unitValue;
	if(!unit->alive() || !unit->isValidTarget(false) || unit->isInvincible() || isSpellLocked(unit)
		|| !spellMechanics->isReceptive(unit))
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	const bool enemy = spellMechanics->battle()->battleGetOwner(unit) != spellMechanics->getCasterColor();
	const float health = static_cast<float>(std::max<int64_t>(1, unit->getAvailableHealth()));
	float value = 0.0f;
	std::set<SpellID> countedEffects;
	const auto spellBonuses = unit->getBonuses(Selector::sourceTypeSel(BonusSource::SPELL_EFFECT));
	if(spellBonuses)
	{
		for(const auto & bonus : *spellBonuses)
		{
			if(!bonus || !bonus->sid.as<SpellID>().hasValue())
				continue;

			const auto spellId = bonus->sid.as<SpellID>();
			const auto * statusSpell = spellId.toSpell();
			if(!statusSpell || statusSpell->isAdventure() || !statusSpell->isMagical()
				|| !countedEffects.insert(spellId).second)
				continue;

			if(enemy)
			{
				// Strip enemy enchantments and preserve our own active afflictions.
				if(statusSpell->isPositive())
					value += health * 0.65f;
				if(statusSpell->isNegative())
					value += health * 0.45f;
			}
			else
			{
				// Cleanse hostile effects while retaining and protecting friendly
				// enchantments; the latter are useful but less valuable than a cleanse.
				if(statusSpell->isNegative())
					value += health * 0.90f;
				if(statusSpell->isPositive())
					value += health * 0.15f;
			}
		}
	}

	const auto * hero = spellMechanics->getHeroCaster();
	const bool spellbinder = hero && hero->hasActivePerk(
		newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::SPELLBINDER_PERK);
	int rounds = newHorizonsSorcery::spellLockDurationBasisPoints(
		std::max(0, spellMechanics->getEffectPower()), spellbinder,
		spellMechanics->getSpellPowerCoefficientBasisPoints(),
		spellMechanics->getWarcastingBonusPercent());
	rounds = spellMechanics->adjustEffectDuration(rounds);
	rounds = std::clamp(rounds, 1, 5);
	return value * (0.7f + 0.1f * static_cast<float>(rounds));
}

float SpellTargetEvaluator::naturePoisonPlacementValue(const Mechanics * spellMechanics,
	const Target & target, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalNaturePoison(spellMechanics) || target.size() != 1
		|| !isNaturePoisonTarget(spellMechanics, target.front().unitValue))
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	const auto * liveTarget = dynamic_cast<const battle::CUnitState *>(target.front().unitValue);
	if(!liveTarget)
		return 0.0f;

	const int64_t baseDamage = newHorizonsMagic::poisonBaseDamageBasisPoints(
		std::max(0, spellMechanics->getEffectPower()),
		spellMechanics->getSpellPowerCoefficientBasisPoints(),
		spellMechanics->getEmpowerSpellBonusPercent(),
		newHorizonsMagic::poisonBaseBonusPercent(spellMechanics->getHeroCaster()));
	if(baseDamage <= 0)
		return 0.0f;

	// CUnitState disables value-copy construction because it owns bonus and
	// environment proxies. acquireState() creates detached states that can be
	// projected without touching the authoritative battle.
	auto currentPoison = liveTarget->acquireState();
	auto appliedPoison = liveTarget->acquireState();
	if(!newHorizonsBulwark::applyPhysicalPoison(appliedPoison.get(), baseDamage, -1))
		return 0.0f;

	if(!battleState)
	{
		const auto * battle = spellMechanics->battle();
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	}

	DamageCache damageCache;
	const auto projectedPoisonValue = [&](const std::shared_ptr<battle::CUnitState> & state)
	{
		float value = 0.0f;
		for(int activation = 0; activation < 3 && state->alive(); ++activation)
		{
			const auto tickDamage = newHorizonsBulwark::physicalPoisonTickDamage(state.get());
			const auto damage = std::min<int64_t>(tickDamage, state->getAvailableHealth());
			if(damage > 0)
			{
				value += AttackPossibility::calculateDamageReduce(
					nullptr, state.get(), static_cast<uint64_t>(damage), damageCache, battleState);
				auto appliedDamage = damage;
				state->damage(appliedDamage);
			}
			newHorizonsBulwark::advancePhysicalPoison(state.get());
		}
		return value;
	};

	const float currentValue = projectedPoisonValue(currentPoison);
	const float refreshedValue = projectedPoisonValue(appliedPoison);
	const float incrementalValue = std::max(0.0f, refreshedValue - currentValue);

	// The authoritative cast uses the ordinary negative magical spell
	// resistance roll before applying this physical status. Existing Poison
	// continues regardless of this cast's outcome, so discount only the
	// incremental candidate value.
	const float applicationChance = spellApplicationChance(spellMechanics, liveTarget);

	return incrementalValue * applicationChance;
}

float SpellTargetEvaluator::plagueDelayedDamageValue(const Mechanics * spellMechanics,
	const Target & target, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalPlague(spellMechanics) || target.size() != 1 || !target.front().unitValue)
		return 0.0f;

	const auto casterSide = spellMechanics->getCasterSide();
	if(casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER)
		return 0.0f;

	const auto * liveTarget = target.front().unitValue;
	if(!liveTarget->alive() || !liveTarget->isValidTarget(false) || liveTarget->isInvincible()
		|| newHorizonsPlague::hasPlague(liveTarget) || !spellMechanics->isReceptive(liveTarget))
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	const auto * liveState = dynamic_cast<const battle::CUnitState *>(liveTarget);
	if(!liveState)
		return 0.0f;

	// getEffectPower() is the captured raw cast Spell Power. The hero's separate
	// effect-power divisor is for legacy effect formulas and must not reduce
	// Plague's canonical raw-SP term.
	const auto rawDamage = newHorizonsPlague::rawTickDamage(
		std::max(0, spellMechanics->getEffectPower()),
		spellMechanics->getSpellPowerCoefficientBasisPoints());
	if(rawDamage <= 0)
		return 0.0f;

	const auto * battleCallback = spellMechanics->battle();
	if(!battleCallback)
		return 0.0f;
	if(!battleState)
	{
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battleCallback), [](CBattleInfoCallback *) {});
	}

	DamageCache damageCache;
	const auto projectedTicksValue = [&](const battle::Unit * unit)
	{
		const auto * unitState = dynamic_cast<const battle::CUnitState *>(unit);
		if(!unitState || !unitState->alive())
			return 0.0f;

		auto projectedState = unitState->acquireState();
		float value = 0.0f;
		for(int tick = 0; tick < 3 && projectedState->alive(); ++tick)
		{
			const auto capturedPenetration = spellMechanics->getCapturedMdrPenetration();
			const auto adjustedDamage = newHorizonsPlague::adjustedTickDamage(
				*battleCallback, casterSide, unit, rawDamage, &capturedPenetration);
			const auto actualDamage = std::min<int64_t>(
				std::max<int64_t>(0, adjustedDamage),
				std::max<int64_t>(0, projectedState->getAvailableHealth()));
			if(actualDamage > 0)
			{
				value += AttackPossibility::calculateDamageReduce(nullptr, projectedState.get(),
					static_cast<uint64_t>(actualDamage), damageCache, battleState);
				auto appliedDamage = actualDamage;
				projectedState->damage(appliedDamage);
			}
		}
		return value;
	};

	const auto signedValue = [&](const battle::Unit * unit, float value)
	{
		if(unit->unitSide() == casterSide)
			return -value;
		if(unit->unitSide() == battleCallback->otherSide(casterSide))
			return value;
		return 0.0f;
	};

	float totalValue = signedValue(liveTarget, projectedTicksValue(liveTarget));
	// Runtime ticks apply damage first and then attempt spread, even if that
	// tick killed the afflicted stack; its occupied battlefield position still
	// supplies the deterministic adjacency source for this propagation.
	{
		const auto acceptsSpread = [&](const battle::Unit * recipient)
		{
			return newHorizonsPlague::isSpreadRecipientReceptive(
				*battleCallback, casterSide, recipient);
		};
		const auto spreadTargetId = newHorizonsPlague::selectNextSpreadTarget(
			*battleCallback, liveTarget, acceptsSpread);
		if(spreadTargetId)
		{
			const auto * spreadTarget = battleCallback->battleGetUnitByID(*spreadTargetId);
			if(spreadTarget)
				totalValue += signedValue(spreadTarget, projectedTicksValue(spreadTarget));
		}
	}

	// The initial hostile magical application can be resisted. Automatic spread
	// is a propagation event, so it uses the runtime's receptor filter without a
	// second probabilistic resistance roll.
	const float applicationChance = spellApplicationChance(spellMechanics, liveTarget);

	return totalValue * applicationChance;
}

float SpellTargetEvaluator::soulChainDelayedDamageValue(const Mechanics * spellMechanics,
	const Target & target, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalSoulChain(spellMechanics) || target.size() < 2
		|| target.size() > static_cast<size_t>(newHorizonsSoulChain::MAX_TARGETS))
		return 0.0f;

	const auto * battle = spellMechanics->battle();
	const auto casterSide = spellMechanics->getCasterSide();
	if(!battle || !newHorizonsSoulChain::validEnemyTargetSet(*battle, casterSide, target))
		return 0.0f;

	for(const auto & destination : target)
		if(!isSoulChainTargetUnit(spellMechanics, destination.unitValue))
			return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	if(!battleState)
	{
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});
	}

	const auto * primary = target.front().unitValue;
	auto projectedPrimary = primary->acquireState();
	const auto echoBasisPoints = soulChainEchoBasisPoints(spellMechanics);
	const auto primaryApplicationChance = spellApplicationChance(spellMechanics, primary);
	DamageCache damageCache;
	float value = 0.0f;

	for(size_t index = 1; index < target.size() && projectedPrimary->alive(); ++index)
	{
		const auto * secondary = target[index].unitValue;
		const auto expectedSecondaryDamage = projectedSoulChainTriggerDamage(
			spellMechanics, secondary, damageCache, battleState);
		if(expectedSecondaryDamage <= 0)
			continue;

		const auto capturedPenetration = spellMechanics->getCapturedMdrPenetration();
		const auto adjustedEcho = newHorizonsSoulChain::adjustedEchoDamage(
			*battle, casterSide, projectedPrimary.get(), expectedSecondaryDamage, echoBasisPoints, &capturedPenetration);
		const auto expectedEcho = static_cast<int64_t>(std::llround(
			static_cast<double>(adjustedEcho) * primaryApplicationChance));
		const auto actualEcho = std::min<int64_t>(
			std::max<int64_t>(0, expectedEcho),
			std::max<int64_t>(0, projectedPrimary->getAvailableHealth()));
		if(actualEcho <= 0)
			continue;

		value += AttackPossibility::calculateDamageReduce(nullptr, projectedPrimary.get(),
			static_cast<uint64_t>(actualEcho), damageCache, battleState);
		auto damage = actualEcho;
		projectedPrimary->damage(damage);
	}

	return value;
}

float SpellTargetEvaluator::shadowGiftTradeValue(const Mechanics * spellMechanics,
	const Target & target, int32_t sacrificePercent, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalShadowGift(spellMechanics) || target.size() != 1
		|| !target.front().unitValue
		|| !newHorizonsShadowGift::isValidSacrificePercent(sacrificePercent))
		return 0.0f;

	const auto * battle = spellMechanics->battle();
	const auto * recipient = target.front().unitValue;
	const auto casterSide = spellMechanics->getCasterSide();
	if(!battle || !recipient->alive() || !recipient->isValidTarget(false)
		|| recipient->isClone() || recipient->isTimeStopped()
		|| recipient->unitSide() != casterSide
		|| !spellMechanics->isReceptive(recipient))
		return 0.0f;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return 0.0f;

	if(!battleState)
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});

	const auto * hero = spellMechanics->getHeroCaster();
	const bool darkGift = hero && hero->hasActivePerk(
		"new-horizons:shadowMagic", "new-horizons:shadowMagic.darkGift");
	const auto costBasisPoints = newHorizonsShadowGift::getSacrificeCostBasisPoints(
		sacrificePercent, darkGift);
	const auto currentHealth = std::max<int64_t>(0, recipient->getShadowGiftCurrentHealth());
	const auto maximumHealth = std::max<int64_t>(0, recipient->getShadowGiftMaximumHealth());
	const auto sacrificedHealth = newHorizonsShadowGift::getSacrificeHealthAmount(
		currentHealth, costBasisPoints);
	if(sacrificedHealth <= 0 || sacrificedHealth >= currentHealth || sacrificedHealth >= maximumHealth)
		return 0.0f;

	// The authoritative sacrifice removes this same amount from current and
	// aggregate maximum HP. Price the real current-HP loss once; charging the
	// coincident cap reduction a second time would double count it. A later
	// healing opportunity limited by the temporary cap remains a Phase 2 case.
	DamageCache costCache;
	float healthCost = AttackPossibility::calculateDamageReduce(
		nullptr, recipient, static_cast<uint64_t>(sacrificedHealth), costCache, battleState);
	if(recipient->getPhantomInitialIntegrity() > 0)
	{
		// Partial Phantom Army integrity loss does not remove creatures, so the
		// ordinary damage-reduction scorer prices it as zero until lethal. For a
		// sacrifice, value the lost durability as the same fraction of the copy's
		// kill value; otherwise Shadow Gift would treat integrity as free.
		DamageCache fullIntegrityCache;
		const float fullIntegrityValue = AttackPossibility::calculateDamageReduce(
			nullptr, recipient, static_cast<uint64_t>(currentHealth), fullIntegrityCache, battleState);
		healthCost = fullIntegrityValue * static_cast<float>(sacrificedHealth)
			/ static_cast<float>(currentHealth);
	}
	const auto * liveRecipient = dynamic_cast<const battle::CUnitState *>(recipient);
	if(!liveRecipient)
		return 0.0f;
	auto projectedRecipient = liveRecipient->acquireState();
	auto projectedSacrifice = sacrificedHealth;
	projectedRecipient->damageShadowGiftSacrifice(projectedSacrifice);
	if(!projectedRecipient->alive())
		return 0.0f;

	DamageCache originalDamageCache;
	DamageCache projectedDamageCache;
	const auto damageBonusBasisPoints = newHorizonsShadowGift::getDamageBonusBasisPoints(
		sacrificePercent,
		std::max(0, spellMechanics->getEffectPower()),
		spellMechanics->getSpellPowerCoefficientBasisPoints(),
		spellMechanics->getWarcastingBonusPercent(),
		spellMechanics->getEmpowerSpellBonusPercent());
	if(damageBonusBasisPoints <= 0)
		return 0.0f;

	float bestExpectedBenefit = 0.0f;
	for(const auto * enemy : battle->battleGetAllUnits(false))
	{
		if(!enemy || !enemy->alive() || !enemy->isValidTarget(false)
			|| enemy->unitSide() == casterSide || enemy->isInvincible())
			continue;

		const auto originalPerActivationDamage = originalDamageCache.getDamage(recipient, enemy, battleState);
		const auto projectedPerActivationDamage = projectedDamageCache.getDamage(
			projectedRecipient.get(), enemy, battleState);
		if(originalPerActivationDamage <= 0 || projectedPerActivationDamage <= 0)
			continue;

		const int attackCount = AttackPossibility::getAttackCount(
			*projectedRecipient, battle->battleCanShoot(recipient, enemy->getPosition()), *battleState);
		if(attackCount <= 0)
			continue;

		const float likelihood = shadowGiftAttackLikelihood(spellMechanics, projectedRecipient.get(), enemy);
		if(likelihood <= 0.0f)
			continue;

		float projectedAttacks = 0.0f;
		for(int round = 0; round < 3; ++round)
			projectedAttacks += static_cast<float>(attackCount)
				* likelihood * (recipient->willMove(round) ? 1.0f : (round == 0 ? 0.0f : 0.65f));
		const auto enemyHealth = static_cast<float>(std::max<int64_t>(0, enemy->getAvailableHealth()));
		const float normalDamage = std::min(enemyHealth,
			static_cast<float>(originalPerActivationDamage) * projectedAttacks);
		const float giftedDamage = std::min(enemyHealth,
			static_cast<float>(projectedPerActivationDamage) * projectedAttacks
				* (1.0f + static_cast<float>(damageBonusBasisPoints) / 10'000.0f));
		const auto extraDamage = static_cast<uint64_t>(std::max(0.0f, giftedDamage - normalDamage));
		if(extraDamage == 0)
			continue;

		// This is a bounded health forecast. It uses the expected Shadow bonus
		// after the target's physical damage projection, but does not model each
		// enemy's Shadow-specific mitigation/resistance pipeline; record that as a
		// Phase 2 interaction rather than implying exact cast parity.
		const float expectedBenefit = AttackPossibility::calculateDamageReduce(
			projectedRecipient.get(), enemy, extraDamage, projectedDamageCache, battleState);
		bestExpectedBenefit = std::max(bestExpectedBenefit, expectedBenefit);
	}

	return std::max(0.0f, bestExpectedBenefit - healthCost);
}

std::optional<SpellTargetEvaluator::HandOfFateExpectedDamageValue>
SpellTargetEvaluator::handOfFateExpectedDamageValue(const Mechanics * spellMechanics,
	const Target & target, PlayerColor scoringPlayer, std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!isCanonicalHandOfFate(spellMechanics) || target.size() != 1 || !target.front().unitValue
		|| spellMechanics->getTargetTypes() != std::vector<AimType>{AimType::CREATURE})
		return std::nullopt;

	const auto * battle = spellMechanics->battle();
	const auto casterSide = spellMechanics->getCasterSide();
	if(!battle || (casterSide != BattleSide::ATTACKER && casterSide != BattleSide::DEFENDER))
		return std::nullopt;

	if(!battleState)
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(battle), [](CBattleInfoCallback *) {});

	const auto * primary = target.front().unitValue;
	if(primary->unitSide() != battle->otherSide(casterSide)
		|| !primary->alive() || !primary->isValidTarget(false))
		return std::nullopt;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return std::nullopt;

	// Spell resistance is resolved once for the selected target by the regular
	// cast path.  Forecast its probability without drawing from the live RNG.
	const float primaryApplicationChance = spellApplicationChance(spellMechanics, primary);
	const auto primaryDamage = std::max<int64_t>(0, spellMechanics->adjustEffectValue(primary));
	auto projectedPrimary = primary->acquireState();
	const auto primaryHealthBefore = projectedPrimary->getAvailableHealth();
	auto primaryDamageToApply = primaryDamage;
	projectedPrimary->damage(primaryDamageToApply);
	const auto actualPrimaryDamage = std::max<int64_t>(0,
		primaryHealthBefore - projectedPrimary->getAvailableHealth());

	DamageCache damageCache;
	HandOfFateExpectedDamageValue result;
	if(actualPrimaryDamage > 0 && primaryApplicationChance > 0.0f)
	{
		result.hostileDamageValue += AttackPossibility::calculateDamageReduce(nullptr, primary,
			static_cast<uint64_t>(actualPrimaryDamage), damageCache, battleState) * primaryApplicationChance;
	}

	// Every living, on-field, non-turret stack remains in the draw pool,
	// including stacks that will reject the secondary packet (for example, a
	// Time Stopped or immune stack).  Rejection contributes zero value but never
	// changes the probability of another recipient being chosen.
	std::vector<const battle::Unit *> collateralPool;
	for(const auto * unit : battle->battleGetAllUnits(false))
	{
		if(!unit || unit == primary || !unit->alive() || !unit->getPosition().isValid() || unit->isTurret())
			continue;
		collateralPool.push_back(unit);
	}

	const auto spillRawDamage = actualPrimaryDamage / 2;
	if(collateralPool.empty() || spillRawDamage <= 0 || primaryApplicationChance <= 0.0f)
		return result;
	const auto * hero = spellMechanics->getHeroCaster();
	const bool fateDealer = spellMechanics->usesNewHorizonsMagicV3() && hero && hero->hasActivePerk(
		"new-horizons:chaosMagic", "new-horizons:chaosMagic.fateDealer");
	const auto hostileToCaster = [&](const battle::Unit * recipient)
	{
		return spellMechanics->battle()->battleGetOwner(recipient) != spellMechanics->getCasterColor();
	};
	const auto hostileCount = std::ranges::count_if(collateralPool, [&](const battle::Unit * recipient)
	{
		return hostileToCaster(recipient);
	});
	const auto friendlyCount = collateralPool.size() - hostileCount;
	const double poolSize = static_cast<double>(collateralPool.size());

	float expectedHostileSpillValue = 0.0f;
	float expectedFriendlySpillValue = 0.0f;
	for(const auto * recipient : collateralPool)
	{
		// This check is deliberately after pool construction: an invalid or
		// unreceptive recipient absorbs its share of the random selection chance.
		if(!recipient->isValidTarget(false) || recipient->isInvincible()
			|| recipient->hasImmunity(spellMechanics->getSpellId())
			|| recipient->hasAbsoluteImmunity(spellMechanics->getSpellId())
			|| !spellMechanics->isReceptive(recipient))
			continue;

		const auto adjustedDamage = std::max<int64_t>(0,
			spellMechanics->adjustRecipientDamage(recipient, spillRawDamage));
		if(adjustedDamage <= 0)
			continue;

		auto projectedRecipient = recipient->acquireState();
		const auto recipientHealthBefore = projectedRecipient->getAvailableHealth();
		auto recipientDamageToApply = adjustedDamage;
		projectedRecipient->damage(recipientDamageToApply);
		const auto actualRecipientDamage = std::max<int64_t>(0,
			recipientHealthBefore - projectedRecipient->getAvailableHealth());
		if(actualRecipientDamage <= 0)
			continue;

		const float recipientValue = AttackPossibility::calculateDamageReduce(nullptr, recipient,
			static_cast<uint64_t>(actualRecipientDamage), damageCache, battleState)
			* spellApplicationChance(spellMechanics, recipient);
		// Selection is relative to the caster, scoring relative to the evaluating
		// player. They need not be the same perspective on a controlled stack.
		const double probability = fateDealer
			? static_cast<double>(hostileToCaster(recipient)
				? hostileCount + 2 * friendlyCount : friendlyCount) / (poolSize * poolSize)
			: 1.0 / poolSize;
		if(battle->battleGetOwner(recipient) == scoringPlayer)
			expectedFriendlySpillValue += recipientValue * probability;
		else
			expectedHostileSpillValue += recipientValue * probability;
	}

	result.hostileDamageValue += expectedHostileSpillValue * primaryApplicationChance;
	result.friendlyDamageValue += expectedFriendlySpillValue * primaryApplicationChance;
	return result;
}

std::optional<float> SpellTargetEvaluator::confusionExpectedActivationValue(
	Mechanics * spellMechanics, const Target & target, const Environment * environment,
	std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!spellMechanics || !spellMechanics->getSpell()
		|| spellMechanics->getSpell()->getJsonKey() != "new-horizons:confusion")
		return std::nullopt;
	if(!environment || !spellMechanics->battle() || target.size() != 1 || !target.front().unitValue)
		return std::nullopt;
	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return std::nullopt;
	if(!battleState)
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(spellMechanics->battle()), [](CBattleInfoCallback *) {});
	const auto unitID = target.front().unitValue->unitId();
	const auto pressure = [unitID](const std::shared_ptr<HypotheticBattle> & board) -> float
	{
		const auto unit = board->getForUpdate(unitID);
		if(!unit || !unit->alive() || (!unit->willMove() && !unit->willMove(1)))
			return 0.0f;
		DamageCache damage;
		if(!unit->confusionState.pending)
		{
			PotentialTargets attacks(unit.get(), damage, board);
			return attacks.berserk ? attacks.expectedBerserkActionValue()
				: attacks.possibleAttacks.empty() ? 0.0f : static_cast<float>(attacks.bestActionValue());
		}
		const auto reachability = board->getReachability(unit.get());
		double value = 0.0;
		for(const auto & outcome : newHorizonsConfusion::enumerateOutcomes(*board, unit.get(),
			unit->confusionState.previousResolved, unit->confusionState.pendingConfounder))
		{
			const auto & action = outcome.action;
			if(!action.target || (action.type != EActionType::SHOOT && action.type != EActionType::WALK_AND_ATTACK))
				continue;
			const bool shooting = action.type == EActionType::SHOOT;
			const auto from = shooting && !action.skirmisher ? BattleHex::INVALID : action.position;
			const int distance = from.isValid() ? std::max(0, physicalTravelDistance(reachability, from)) : 0;
			BattleAttackInfo attack(unit.get(), action.target, distance, shooting);
			if(action.skirmisher)
				attack.archeryRangedDamageMultiplierPercent = newHorizonsArchery::SKIRMISHER_DAMAGE_PERCENT;
			value += outcome.probability * AttackPossibility::evaluate(attack, from, damage, board).attackValue();
		}
		return static_cast<float>(value);
	};
	auto baseline = std::make_shared<HypotheticBattle>(environment, battleState);
	const float before = pressure(baseline);
	auto projected = std::make_shared<HypotheticBattle>(environment, battleState);
	const auto projectedUnit = projected->getForUpdate(unitID);
	// Use the production effect for source-bound Confounder provenance and
	// Berserk removal. The pending producer does not consume a random draw.
	spellMechanics->castEval(projected->getServerCallback(), Target{Destination(projectedUnit.get())});
	return (before - pressure(projected)) * spellApplicationChance(spellMechanics, target.front().unitValue);
}

std::optional<float> SpellTargetEvaluator::battleFormExpectedOffensiveValue(
	const Mechanics * spellMechanics,
	const spells::effects::BattleFormEffect * battleFormEffect,
	const Target & target,
	const Environment * environment,
	std::shared_ptr<CBattleInfoCallback> battleState)
{
	if(!spellMechanics || !battleFormEffect || !environment || target.size() != 1
		|| !target.front().unitValue)
		return std::nullopt;

	const auto * callback = spellMechanics->battle();
	if(!callback)
		return std::nullopt;

	if(!battleState)
		battleState = std::shared_ptr<CBattleInfoCallback>(
			const_cast<CBattleInfoCallback *>(callback), [](CBattleInfoCallback *) {});

	const auto * originalTarget = target.front().unitValue;
	if(!originalTarget->alive() || !originalTarget->getPosition().isValid()
		|| (spellMechanics->getCasterSide() != BattleSide::ATTACKER
			&& spellMechanics->getCasterSide() != BattleSide::DEFENDER)
		|| originalTarget->unitSide() == spellMechanics->getCasterSide())
		return std::nullopt;

	detail::ProblemImpl problem;
	if(!spellMechanics->canBeCastAt(target, problem))
		return std::nullopt;

	const auto forms = battleFormEffect->weightedFormsForTarget(spellMechanics, originalTarget);
	if(forms.empty())
		return std::nullopt;

	// Polymorph's canonical duration is two rounds. Longer custom battle-form
	// durations keep their runtime lifetime, but valuation stays bounded to two
	// projected activations so candidate scoring remains predictable.
	constexpr int32_t MAX_FORECAST_ROUNDS = 2;
	const auto forecastRounds = std::clamp(battleFormEffect->getDuration(), 0, MAX_FORECAST_ROUNDS);
	if(forecastRounds == 0)
		return 0.0f;

	const auto unitId = originalTarget->unitId();
	const auto offensivePressure = [&](const std::shared_ptr<HypotheticBattle> & projectedBattle,
		DamageCache & damageCache) -> float
	{
		const auto * projectedTarget = projectedBattle->battleGetUnitByID(unitId);
		if(!projectedTarget || !projectedTarget->alive() || projectedTarget->getCount() <= 0
			|| projectedTarget->isGhost() || projectedTarget->isTurret())
			return 0.0f;

		// PotentialTargets uses this detached board's actual footprint, firing
		// line, movement range, and legal melee positions. Its attack value is in
		// AttackPossibility's damage-reduction units, including the real attack
		// count and counter/collateral outcomes for the chosen action.
		PotentialTargets actions(projectedTarget, damageCache, projectedBattle);
		const float actionValue = actions.berserk
			? actions.expectedBerserkActionValue()
			: (actions.possibleAttacks.empty() ? 0.0f : actions.possibleAttacks.front().attackValue());

		float value = 0.0f;
		for(int turn = 0; turn < forecastRounds; ++turn)
			if(projectedTarget->willMove(turn))
				value += actionValue;
		return value;
	};

	// DamageCache keys by unit ID; never share one across form outcomes because
	// each candidate changes effective creature bonuses, count, and attack stats.
	// Leave each cache lazy: buildDamageCache eagerly scores every obstacle against
	// every stack and every army pair, which is unnecessary for this scoped attack
	// forecast and would multiply that work by the size of the form pool.
	auto baselineBattle = std::make_shared<HypotheticBattle>(environment, battleState);
	auto baselineTarget = baselineBattle->getForUpdate(unitId);
	DamageCache baselineDamage;
	const float baselinePressure = offensivePressure(baselineBattle, baselineDamage);

	double signedPressureReduction = 0.0;
	for(const auto & candidate : forms)
	{
		auto projectedBattle = std::make_shared<HypotheticBattle>(environment, battleState);
		auto projectedTarget = projectedBattle->getForUpdate(unitId);
		try
		{
			projectedTarget->beginBattleForm(candidate.form.creature, battleFormEffect->getDuration());
			projectedTarget->setPosition(candidate.form.landing);
		}
		catch(const std::exception &)
		{
			return std::nullopt;
		}

		DamageCache projectedDamage;
		const float candidatePressure = offensivePressure(projectedBattle, projectedDamage);
		// Preserve both weakening rewards and strengthening penalties. In
		// particular, do not clamp an unfavorable random form before averaging.
		signedPressureReduction += static_cast<double>(baselinePressure - candidatePressure)
			* static_cast<double>(candidate.weight) / static_cast<double>(candidate.totalWeight);
	}

	const auto applicationChance = spellApplicationChance(spellMechanics, originalTarget);
	return static_cast<float>(signedPressureReduction) * applicationChance;
}

std::vector<Target> SpellTargetEvaluator::creaturePairTargets(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	// This query includes corpses. Sacrifice chooses a corpse BEFORE a living
	// victim; do not replace it with an alive-only target list or reorder pairs.
	const auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * first : units)
	{
		Target target{Destination(first)};
		detail::ProblemImpl prefixProblem;
		if(!spellMechanics->canBeCastAt(target, prefixProblem))
			continue;
		for(const auto * second : units)
		{
			target.resize(1);
			target.emplace_back(second);
			detail::ProblemImpl pairProblem;
			if(spellMechanics->canBeCastAt(target, pairProblem))
				result.push_back(target);
		}
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::creatureLocationTargets(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	for(const auto * unit : spellMechanics->battle()->battleGetAllUnits(false))
	{
		Target target{Destination(unit)};
		detail::ProblemImpl sourceProblem;
		if(!spellMechanics->canBeCastAt(target, sourceProblem))
			continue;

		// Preserve the exact source unit. The spell validator decides occupancy,
		// double-wide placement, walls/moats and rank restrictions for each pair.
		for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
		{
			const BattleHex destination(index);
			if(destination == unit->getPosition())
				continue; // Moving nowhere cannot improve the hypothetical battle.
			target.resize(1);
			target.emplace_back(destination);
			detail::ProblemImpl destinationProblem;
			if(spellMechanics->canBeCastAt(target, destinationProblem))
				result.push_back(target);
		}
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::defaultLocationSpellHeuristics(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result = allTargetableCreatures(spellMechanics, false);
	auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * unit : units) //insert a random surrounding hex
	{
		auto surroundingHexes = unit->getSurroundingHexes();
		if(!surroundingHexes.empty())
		{
			auto randomSurroundingHex = *RandomGeneratorUtil::nextItem(surroundingHexes, CRandomGenerator::getDefault()); // don't think this method bias matter with such small numbers
			addIfCanBeCast(spellMechanics, randomSurroundingHex, result);
		}
	}
	// OBSTACLE spells (including Remove Obstacle) are normalized to LOCATION
	// by BaseMechanics. Unit-neighbour sampling alone misses distant obstacles.
	std::set<BattleHex> considered;
	for(const auto & target : result)
		considered.insert(target.front().hexValue);
	for(const auto & obstacle : spellMechanics->battle()->battleGetAllObstacles())
		for(const auto hex : obstacle->getAffectedTiles())
			if(considered.insert(hex).second)
				addIfCanBeCast(spellMechanics, hex, result);
	return result;
}

std::vector<Target> SpellTargetEvaluator::allTargetableCreatures(const spells::Mechanics * spellMechanics, bool exactUnit)
{
	std::vector<Target> result;
	const auto units = spellMechanics->battle()->battleGetAllUnits(false);
	for(const auto * unit : units)
	{
		Target target{exactUnit ? Destination(unit) : Destination(unit->getPosition())};
		detail::ProblemImpl problem;
		if(spellMechanics->canBeCastAt(target, problem))
			result.push_back(target);
	}
	return result;
}

std::vector<Target> SpellTargetEvaluator::theBestLocationCasts(const spells::Mechanics * spellMechanics)
{
	std::vector<Target> result;
	std::map<BattleHex, std::set<const CStack *>> allCasts;
	std::map<BattleHex, std::set<const CStack *>> bestCasts;
	for(int i = 0; i < GameConstants::BFIELD_SIZE; i++)
	{
		BattleHex dest(i);
		if(canBeCastAt(spellMechanics, dest))
		{
			Target target;
			target.emplace_back(dest);
			auto temp = spellMechanics->getAffectedStacks(target);
			std::set<const CStack *> affectedStacks(temp.begin(), temp.end());
			allCasts[dest] = affectedStacks;
		}
	}

	for(const auto & cast : allCasts)
	{
		std::set<BattleHex> worseCasts;
		if(isCastHarmful(spellMechanics, cast.second))
			continue;

		bool isBestCast = true;
		for(const auto & bestCast : bestCasts)
		{
			Compare compare = compareAffectedStacks(spellMechanics, cast.second, bestCast.second);

			if(compare == Compare::WORSE || compare == Compare::EQUAL)
			{
				isBestCast = false;
				break;
			}

			if(compare == Compare::BETTER)
			{
				worseCasts.insert(bestCast.first);
			}
		}

		if(isBestCast)
		{
			bestCasts.insert(cast);
			for(BattleHex worseCast : worseCasts)
				bestCasts.erase(worseCast);
		}
	}

	for(const auto & cast : bestCasts)
	{
		Destination des(cast.first);
		result.push_back({des});
	}
	return result;
}

bool SpellTargetEvaluator::isCastHarmful(const spells::Mechanics * spellMechanics, const std::set<const CStack *> & affectedStacks)
{

	bool isAffectedAlly = false;
	bool isAffectedEnemy = false;

	for(const CStack * affectedUnit : affectedStacks)
	{
		// Hypnotize changes control without changing the unit's original side.
		if(spellMechanics->battle()->battleGetOwner(affectedUnit) == spellMechanics->getCasterColor())
			isAffectedAlly = true;
		else
			isAffectedEnemy = true;
	}

	return (spellMechanics->isPositiveSpell() && !isAffectedAlly) || (spellMechanics->isNegativeSpell() && !isAffectedEnemy);
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::compareAffectedStacks(
	const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newCast, const std::set<const CStack *> & oldCast)
{
	if(newCast == oldCast)
		return Compare::EQUAL;

	auto getAlliedUnits = [&spellMechanics](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> alliedUnits;
		for(auto stack : allUnits)
		{
			if(spellMechanics->battle()->battleGetOwner(stack) == spellMechanics->getCasterColor())
				alliedUnits.insert(stack);
		}
		return alliedUnits;
	};

	auto getEnemyUnits = [&spellMechanics](const std::set<const CStack *> & allUnits) -> std::set<const CStack *>
	{
		std::set<const CStack *> enemyUnits;
		for(auto stack : allUnits)
		{
			if(spellMechanics->battle()->battleGetOwner(stack) != spellMechanics->getCasterColor())
				enemyUnits.insert(stack);
		}
		return enemyUnits;
	};

	Compare alliedSubsetComparison = compareAffectedStacksSubset(spellMechanics, getAlliedUnits(newCast), getAlliedUnits(oldCast));
	Compare enemySubsetComparison = compareAffectedStacksSubset(spellMechanics, getEnemyUnits(newCast), getEnemyUnits(oldCast));

	if(spellMechanics->isPositiveSpell())
		enemySubsetComparison = reverse(enemySubsetComparison);
	else if(spellMechanics->isNegativeSpell())
		alliedSubsetComparison = reverse(alliedSubsetComparison);

	std::set<Compare> comparisonResults = {alliedSubsetComparison, enemySubsetComparison};
	std::set<std::set<Compare>> possibleBetterResults = {
		{Compare::BETTER, Compare::BETTER},
        {Compare::BETTER, Compare::EQUAL }
	};
	std::set<std::set<Compare>> possibleWorstResults = {
		{Compare::WORSE, Compare::WORSE},
        {Compare::WORSE, Compare::EQUAL}
	};

	if(possibleBetterResults.find(comparisonResults) != possibleBetterResults.end())
		return Compare::BETTER;
	if(possibleWorstResults.find(comparisonResults) != possibleWorstResults.end())
		return Compare::WORSE;

	return Compare::DIFFERENT;
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::compareAffectedStacksSubset(
    const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newSubset, const std::set<const CStack *> & oldSubset)
{
	if(newSubset.size() == oldSubset.size())
		return newSubset == oldSubset ? Compare::EQUAL : Compare::DIFFERENT;

	if(oldSubset.size() > newSubset.size())
		return reverse(compareAffectedStacksSubset(spellMechanics, oldSubset, newSubset));

	const std::set<const CStack *> & biggerSet = newSubset;
	const std::set<const CStack *> & smallerSet = oldSubset;

	if(std::includes(biggerSet.begin(), biggerSet.end(), smallerSet.begin(), smallerSet.end()))
		return Compare::BETTER;
	else
		return Compare::DIFFERENT;
}

SpellTargetEvaluator::Compare SpellTargetEvaluator::reverse(SpellTargetEvaluator::Compare compare)
{
	switch(compare)
	{
		case Compare::BETTER:
			return Compare::WORSE;
		case Compare::WORSE:
			return Compare::BETTER;
		default:
			return compare;
	}
}

bool SpellTargetEvaluator::canBeCastAt(const spells::Mechanics * spellMechanics, BattleHex hex)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	return spellMechanics->canBeCastAt({des}, ignored);
}

void SpellTargetEvaluator::addIfCanBeCast(const spells::Mechanics * spellMechanics, BattleHex hex, std::vector<Target> & targets)
{
	detail::ProblemImpl ignored;
	Destination des(hex);
	if(spellMechanics->canBeCastAt({des}, ignored))
		targets.push_back({des});
}
