/*
 * SpellTargetsEvaluator.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/spells/BattleSpellMechanics.h"
#include <vcmi/spells/Magic.h>

class SpellTargetEvaluator
{
public:
	static std::vector<spells::Target> getViableTargets(const spells::Mechanics * spellMechanics);
	/// Returns a deterministic pressure value for a canonical New Horizons Land
	/// Mine placement.  The value is deliberately read-only and only considers
	/// the live battle snapshot; it is used by BattleAI when a mine has no
	/// immediate projected unit-health delta to score.
	static float landMinePlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		std::shared_ptr<CBattleInfoCallback> battleState = {});
	/// Returns the delayed expected value of a canonical Fire Wall footprint.
	/// The value includes a strong friendly-ground exposure penalty because the
	/// authoritative trigger affects both sides.  Zero means the line is not a
	/// worthwhile/safe AI cast.
	static float fireWallPlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		std::shared_ptr<CBattleInfoCallback> battleState = {});

private:
	enum Compare
	{
		EQUAL,
		DIFFERENT,
		BETTER,
		WORSE
	};

	static std::vector<spells::Target> creaturePairTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> creatureLocationTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> defaultLocationSpellHeuristics(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> canonicalLandMineTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> canonicalFireWallTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> allTargetableCreatures(const spells::Mechanics * spellMechanics, bool exactUnit);
	static std::vector<spells::Target> theBestLocationCasts(const spells::Mechanics * spellMechanics);
	static Compare compareAffectedStacks(
	const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newCast, const std::set<const CStack *> & oldCast);
	static Compare compareAffectedStacksSubset(
	const spells::Mechanics * spellMechanics, const std::set<const CStack *> & newSubset, const std::set<const CStack *> & oldSubset);
	static SpellTargetEvaluator::Compare reverse(Compare compare);
	static bool isCastHarmful(const spells::Mechanics * spellMechanics, const std::set<const CStack *> & affectedStacks);
	static bool canBeCastAt(const spells::Mechanics * spellMechanics, BattleHex hex);
	static void addIfCanBeCast(const spells::Mechanics * spellMechanics, BattleHex hex, std::vector<spells::Target> & targets);
};
