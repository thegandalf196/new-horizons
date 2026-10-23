/*
 * BattleSpellProjectionTargets.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"

#include "BattleSpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellcraft.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsPandemonium.h"
#include "NewHorizonsVengefulVines.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../ScopeGuard.h"

#include <set>

namespace spells
{
bool BattleSpellMechanics::delegateConcentratedProjection(ServerCallback * server, const Target & target)
{
	if(!eventSnapshot || eventSnapshot->hasSpellcraftTargetSnapshot() || mode != Mode::HERO
		|| !getHeroCaster() || !getHeroCaster()->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsSpellcraft::CONCENTRATION)))
		return false;
	auto concentrated = eventSnapshot->mechanicsForTarget(target);
	concentrated->castEval(server, target);
	vinesProjectionReceipt = concentrated->vengefulVinesProjectionReceipt();
	lifeDrainReceipt = concentrated->lifeDrainProjectionReceipt();
	return true;
}

BattleSpellMechanics::PreparedProjectionReceipts BattleSpellMechanics::prepareProjectedEffects(
	const Target & target, bool completedHeroProjection)
{
	if(newHorizonsPandemonium::enabled(*this))
		capturePandemoniumDebuffs();
	const auto spellTarget = transformSpellTarget(target);
	effectsToApply = effects->prepare(this, target, spellTarget);
	filterSpellLockedEffects(target);
	PreparedProjectionReceipts result;
	if(!completedHeroProjection || isCounterspellNegated())
		return result;
	if(newHorizonsVengefulVines::enabled(battle()->getBattle()->getMagicRules(), getSpellId())
		&& newHorizonsVengefulVines::footprint(target).size() == 3)
		result.vines = newHorizonsVengefulVines::captureProjection(*this, effectsToApply, resolvedConcentrationBonusPercent());
	if(newHorizonsLifeDrain::supportsProjection(*this, effectsToApply))
		result.lifeDrain = newHorizonsLifeDrain::captureProjection(*this, effectsToApply, target, resolvedConcentrationBonusPercent());
	return result;
}

size_t BattleSpellMechanics::getTargetedStackCount(const Target & target) const
{
	const bool previous = countingSpellTargets;
	countingSpellTargets = true;
	const auto restore = vstd::makeScopeGuard([this, previous]() { countingSpellTargets = previous; });
	const auto spellTarget = transformSpellTarget(target);
	std::set<uint32_t> ids;
	effects->forEachEffect(getEffectLevel(), [this, &target, &spellTarget, &ids](const effects::Effect * effect, bool &)
	{
		for(const auto & destination : effect->transformTarget(this, target, spellTarget))
			if(destination.unitValue)
				ids.insert(destination.unitValue->unitId());
	});
	// Hand of Fate targets a random additional living stack even when that
	// stack's defenses later negate its spill. Count that before any draw.
	if(owner->getJsonKey() == "new-horizons:handOfFate" && ids.size() == 1)
		for(const auto * unit : battle()->battleGetAllUnits(false))
			if(unit->alive() && unit->getPosition().isValid() && !unit->isTurret() && !ids.contains(unit->unitId()))
				return 2;
	return ids.size();
}

battle::Units BattleSpellMechanics::collectTargets() const
{
	// Preserve effect/hop order while deduplicating, including client reconstruction.
	battle::Units result;
	for(const auto & effect : effectsToApply)
		for(const Destination & destination : effect.second)
			if(destination.unitValue && !vstd::contains(result, destination.unitValue))
				result.push_back(destination.unitValue);
	return result;
}
}
