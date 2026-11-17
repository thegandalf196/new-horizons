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
#include "PhysicalCleanseValue.h"
#include "CreatureActivationValue.h"
#include "../../lib/battle/NewHorizonsStun.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/CObstacleInstance.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/ReachabilityInfo.h"
#include "../../lib/battle/NewHorizonsBulwark.h"
#include "../../lib/battle/BattleForm.h"
#include "../../lib/networkPacks/SetStackEffect.h"
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
#include "../../lib/spells/NewHorizonsFireWall.h"
#include "../../lib/spells/NewHorizonsPurify.h"
#include "../../lib/spells/NewHorizonsSorcery.h"
#include "../../lib/spells/NewHorizonsVengefulVines.h"
#include "../../lib/spells/NewHorizonsNaturesWrath.h"
#include "../../lib/spells/NewHorizonsPandemonium.h"
#include "../../lib/spells/NewHorizonsRealityWarp.h"
#include "../../lib/battle/NewHorizonsPlague.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/CRandomGenerator.h"
#include "SpellTargetsEvaluator.h"
#include "SearchBudget.h"
#include "SummonEvaluation.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"
#include <vcmi/spells/Spell.h>

#include <set>
#include <tuple>
#include "HypotheticBattle.h"

#include "SpellTargetBudgetFallback.h"
using namespace spells;
namespace spellTargetBudgetFallback
{
bool mineHexIsEmpty(const CBattleInfoCallback & battle,
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

std::vector<Target> firstPlacementAim(const Mechanics * mechanics, bool mines,
	battleSearch::SearchBudget * budget)
{
	if(!budget) return {};
	try
	{
		const auto * board = mechanics->battle();
		const auto units = board->battleGetAllUnits(false);
		battleSearch::require(budget, battleSearch::WorkKind::Generation, static_cast<uint32_t>(units.size()));
		int center = GameConstants::BFIELD_SIZE / 2;
		for(const auto * unit : units)
			if(unit->alive() && board->battleGetOwner(unit) != mechanics->getCasterColor())
			{ center = unit->getPosition().toInt(); break; }
		const auto accessibility = board->getAccessibility();
		const int required = mines ? mechanics->getNewHorizonsLandMinePatchCount()
			: mechanics->getNewHorizonsQuicksandPatchCount();
		if(required <= 0) return {};
		Target selected;
		for(int offset = 0; offset < GameConstants::BFIELD_SIZE && selected.size() < static_cast<size_t>(required); ++offset)
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			const int displacement = offset == 0 ? 0 : (offset % 2 ? -(offset + 1) / 2 : offset / 2);
			const int index = center + displacement;
			if(index < 0 || index >= GameConstants::BFIELD_SIZE) continue;
			const BattleHex hex(index);
			const bool legal = mines ? mineHexIsEmpty(*board, accessibility, hex)
				: newHorizonsMagic::quicksandPlacementHexIsLegal(*board, hex);
			if(legal) selected.emplace_back(hex);
		}
		if(selected.size() != static_cast<size_t>(required)) return {};
		battleSearch::require(budget, battleSearch::WorkKind::Generation);
		detail::ProblemImpl problem;
		return mechanics->canBeCastAt(selected, problem) ? std::vector<Target>{std::move(selected)} : std::vector<Target>{};
	}
	catch(const battleSearch::BudgetExhausted &) { return {}; }
}

std::vector<Target> firstUnitAim(Mechanics * mechanics, bool daggers, battleSearch::SearchBudget * budget)
{
	if(!budget) return {};
	try
	{
		if(daggers && !mechanics->setStormOfDaggersTargetCount(1)) return {};
		Target target;
		for(const auto * unit : mechanics->battle()->battleGetAllUnits(false))
		{
			battleSearch::require(budget, battleSearch::WorkKind::Generation);
			if(!unit->alive() || mechanics->battle()->battleGetOwner(unit) == mechanics->getCasterColor()) continue;
			target.emplace_back(unit);
			if(!daggers && target.size() < 2) continue;
			detail::ProblemImpl problem;
			if(mechanics->canBeCastAt(target, problem)) return {std::move(target)};
			target.clear();
		}
		return {};
	}
	catch(const battleSearch::BudgetExhausted &) { return {}; }
}

}
