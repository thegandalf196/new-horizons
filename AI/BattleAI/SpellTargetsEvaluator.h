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

#include <utility>

class SpellTargetEvaluator
{
public:
	struct PurifySelection
	{
		std::vector<std::pair<int32_t, SpellID>> spellEffectGroups;
		std::vector<int32_t> physicalPoisonStackIds;
		float value = 0.0f;
	};

	static std::vector<spells::Target> getViableTargets(spells::Mechanics * spellMechanics);
	/// Selects the most valuable legal negative source groups for each friendly
	/// stack around a canonical Purify center. The result is a read-only snapshot.
	static PurifySelection purifySelection(const spells::Mechanics * spellMechanics,
		const spells::Target & target);
	/// Returns a deterministic pressure value for a canonical New Horizons Land
	/// Mine placement.  The value is deliberately read-only and only considers
	/// the live battle snapshot; it is used by BattleAI when a mine has no
	/// immediate projected unit-health delta to score.
	static float landMinePlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		std::shared_ptr<CBattleInfoCallback> battleState = {});
	/// Values selected Quicksand as delayed movement denial, including friendly
	/// ground exposure. A non-positive result is not worth a Hero Action.
	static float quicksandPlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target);
	/// Returns the delayed expected value of a canonical Fire Wall footprint.
	/// The value includes a strong friendly-ground exposure penalty because the
	/// authoritative trigger affects both sides.  Zero means the line is not a
	/// worthwhile/safe AI cast.
	static float fireWallPlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		std::shared_ptr<CBattleInfoCallback> battleState = {});
	/// Returns the tactical value of a canonical Time Stop area.  Enemy units
	/// are rewarded for losing their next action; healthy friendly units are
	/// penalized because they lose their action too, while a threatened/injured
	/// ally can make a defensive cast worthwhile.
	static float timeStopPlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target);
	/// Scores cleansing and anti-magic value for a canonical Spell Lock cast.
	/// Zero means the stack is already locked, unreceptive, or not worth sealing.
	static float spellLockPlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target);
	/// Estimates the marginal value of a canonical Nature Poison application by
	/// projecting its three real-activation ticks against the target's current
	/// physical Poison state. The projection uses detached unit states only.
	static float naturePoisonPlacementValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		std::shared_ptr<CBattleInfoCallback> battleState = {});
	/// Estimates canonical Plague's signed, delayed value from three ticks on
	/// the selected stack and the first deterministic adjacent spread recipient.
	/// All projected damage uses detached states and the shared runtime helpers.
	static float plagueDelayedDamageValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		std::shared_ptr<CBattleInfoCallback> battleState = {});
	/// Estimates delayed Soul Chain echo damage from likely friendly attacks over
	/// its two-round duration. One-target casts are legal but intentionally score zero.
	static float soulChainDelayedDamageValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		std::shared_ptr<CBattleInfoCallback> battleState = {});
	/// Values canonical Shadow Gift as three rounds of expected offensive benefit
	/// minus the real sacrificed HP. The forecast reads only the supplied battle.
	static float shadowGiftTradeValue(const spells::Mechanics * spellMechanics,
		const spells::Target & target,
		int32_t sacrificePercent,
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
	static std::vector<spells::Target> canonicalQuicksandTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> canonicalFireWallTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> canonicalTimeStopTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> canonicalSpellLockTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> canonicalNaturePoisonTargets(const spells::Mechanics * spellMechanics);
	static std::vector<spells::Target> canonicalSoulChainTargets(spells::Mechanics * spellMechanics);
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
