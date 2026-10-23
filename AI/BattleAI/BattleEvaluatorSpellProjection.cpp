/*
 * BattleEvaluatorSpellProjection.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "BattleEvaluatorSpellProjection.h"
#include "PossibleSpellcast.h"
#include "../../lib/ScopeGuard.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSpellAvailability.h"

namespace battleEvaluatorProjection
{
bool isCanonicalVerdantPrison(const CSpell * spell)
{
	return spell && spell->getJsonKey() == "new-horizons:verdantPrison";
}

bool isCanonicalRegeneration(const CSpell * spell, const JsonNode & rules)
{
	return spell && newHorizonsMagic::spellVariantBase(rules, spell->getId()).toSpell()->getJsonKey()
		== newHorizonsMagic::NATURE_REGENERATION_SPELL;
}

namespace
{
void projectRegenerationRateSnapshot(HypotheticBattle & projectedBattle,
	const spells::Mechanics & mechanics, const CSpell * spell,
	const spells::Target & acceptedTarget)
{
	const auto & savedRules = projectedBattle.getBattle()->getMagicRules();
	if(!isCanonicalRegeneration(spell, savedRules) || !newHorizonsMagic::rulesActive(savedRules))
		return;

	const auto regenerationMarker = Selector::source(BonusSource::SPELL_EFFECT,
		BonusSourceID(newHorizonsMagic::spellVariantBase(savedRules, spell->getId())))
		.And(Selector::type()(BonusType::HP_REGENERATION));
	const auto * hero = mechanics.getHeroCaster();
	const bool herbalist = hero && hero->hasActivePerk(
		std::string(newHorizonsMagic::NATURE_MAGIC_SKILL),
		std::string(newHorizonsMagic::NATURE_HERBALIST));
	const auto rate = newHorizonsMagic::regenerationRateMillionthsBasisPoints(
		std::max<int32_t>(0, mechanics.getEffectPower()),
		mechanics.getSpellPowerCoefficientBasisPoints(), herbalist,
		mechanics.getWarcastingBonusPercent(), mechanics.getEmpowerSpellBonusPercent());
	for(const auto * unit : mechanics.getAffectedStacks(acceptedTarget))
	{
		if(!unit)
			continue;
		auto targetState = projectedBattle.getForUpdate(unit->unitId());
		if(targetState->alive() && targetState->hasBonus(regenerationMarker))
			targetState->regenerationRateMillionths = rate;
	}
}
}

SpellProjection projectSpell(HypotheticBattle & state, const CBattleInfoCallback & live,
	const CGHeroInstance * hero, BattleSide side, const PossibleSpellcast & candidate,
	const heroActionProjection::Admission & admission, uint32_t & targetId,
	const std::vector<uint32_t> & sharedPurposeRecipients)
{
	auto candidateTarget = candidate.dest;
	bool missingProjectedTarget = false;
	if(!candidateTarget.empty() && candidateTarget.front().unitValue)
		targetId = candidateTarget.front().unitValue->unitId();
	for(auto & destination : candidateTarget)
	{
		if(!destination.unitValue)
			continue;
		const auto destinationId = destination.unitValue->unitId();
		destination.unitValue = state.battleGetUnitByID(destinationId);
		missingProjectedTarget |= destination.unitValue == nullptr;
	}
	if(missingProjectedTarget)
		return {};
	spells::BattleCast cast(&state, hero, spells::Mode::HERO, candidate.spell);
	cast.setMetamagicFollowup(candidate.metamagicFollowup);
	cast.setMetamagicGrand(candidate.metamagicGrand);
	if(targetId != std::numeric_limits<uint32_t>::max())
		cast.setMetamagicTargetUnitId(targetId);
	cast.setOvercharge(candidate.spellOvercharge);
	cast.setSelectiveDispel(candidate.spellSelectiveDispel);
	cast.setCureAffliction(candidate.spellCureAffliction);
	cast.setCurePhysicalAffliction(candidate.spellCurePhysicalAffliction);
	cast.setMassSlow(candidate.spellMassSlow);
	if(admission.counterspell.wardActive)
		cast.setCounterspell(admission.counterspell.wardSide, admission.counterspellNegated);
	const bool observing = !admission.counterspellNegated && candidate.spell->getJsonKey() == newHorizonsLifeDrain::SPELL_KEY
		&& candidateTarget.size() == 2 && candidateTarget[1].unitValue
		&& state.beginLifeDrainFriendlyObservation(candidateTarget[1].unitValue->unitId());
	bool observationFinished = false;
	const auto cancelObservation = vstd::makeScopeGuard([&]()
	{
		if(observing && !observationFinished)
			state.cancelLifeDrainFriendlyObservation();
	});
	SpellProjection result;
	std::optional<newHorizonsLifeDrain::ProjectionReceipt> preparedLifeDrain;
	if(!admission.counterspellNegated)
	{
		auto mechanics = candidate.spell->battleMechanics(&cast);
		if(isCanonicalVerdantPrison(candidate.spell))
		{
			const auto * liveTarget = live.battleGetUnitByID(targetId);
			if(!liveTarget || !liveTarget->alive() || !liveTarget->unitType())
				return {};
		}
		if(mechanics->isNewHorizonsStormOfDaggers())
		{
			// The selected subset remains part of the ordinary shared forecast.
			if(!mechanics->setStormOfDaggersTargetCount(static_cast<int32_t>(candidateTarget.size())))
				return {};
			mechanics->castEval(state.getServerCallback(), candidateTarget);
		}
		else
		{
			mechanics->castEval(state.getServerCallback(), candidateTarget);
			// Mirror the authoritative fixed-point regeneration rate on the clone.
			projectRegenerationRateSnapshot(state, *mechanics, candidate.spell, candidateTarget);
		}
		result.vines = mechanics->vengefulVinesProjectionReceipt();
		preparedLifeDrain = mechanics->lifeDrainProjectionReceipt();
	}
	result.accepted = state.projectAcceptedHeroSpell(side, candidate.spell->getId(), targetId,
		candidate.metamagicFollowup, candidate.metamagicGrand, admission.counterspell.wardActive,
		admission.counterspellNegated, *admission.spellAllowance, sharedPurposeRecipients);
	if(observing)
	{
		const bool noOp = state.finishLifeDrainFriendlyObservation();
		observationFinished = true;
		if(result.accepted && noOp)
			result.lifeDrain = std::move(preparedLifeDrain);
	}
	if(!result.accepted)
		result.vines.reset();
	return result;
}
}
