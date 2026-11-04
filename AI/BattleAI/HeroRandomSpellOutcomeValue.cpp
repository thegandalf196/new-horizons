/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleEvaluator.h"
#include "HeroActionAdmission.h"
#include "SpellTargetsEvaluator.h"
#include "BlinkPositionValue.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/spells/ArcaneInspirationCompletion.h"
#include "../../lib/spells/NewHorizonsMetamagicPoints.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/Problem.h"
#include "../../lib/spells/effects/BattleForm.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/callback/CBattleCallback.h"

float BattleEvaluator::inspirationOutcome(const PossibleSpellcast & candidate, const spells::Mechanics & mechanics,
	const std::shared_ptr<HypotheticBattle> & board, std::span<const BattleStackAttacked> injuries, bool completed, bool localOnly, uint32_t actorId)
{
	if(!completed)
	{
		const auto admission = heroActionProjection::admitCandidate(*board, side, candidate);
		if(!admission.admitted || admission.counterspellNegated) return 0.0f;
		uint32_t primary = candidate.dest.empty() || !candidate.dest.front().unitValue
			? std::numeric_limits<uint32_t>::max() : candidate.dest.front().unitValue->unitId();
		if(!board->projectAcceptedHeroSpell(side, candidate.spell->getId(), primary,
			candidate.metamagicFollowup, candidate.metamagicGrand, admission.counterspell.wardActive,
			false, *admission.spellAllowance)) return 0.0f;
		arcaneInspiration::publishSpellDamage(*board->getServerCallback(), *board, side, injuries);
		std::vector<uint32_t> resolved;
		for(const auto & hit : injuries) resolved.push_back(hit.stackAttacked);
		newHorizonsMetamagic::publishPointCompletion(*board->getServerCallback(), *board, mechanics, resolved, injuries, side);
	}
	// Only the candidate-wide core baseline uses the existing continuation.
	return localOnly ? 0.0f : conditionalInspirationUtility(board, actorId, false);
}

heroSpellProjection::OutcomeValue BattleEvaluator::inspirationOutcomeEvaluator(
	const PossibleSpellcast & candidate, const spells::Mechanics & mechanics, uint32_t actorId)
{
	if(arcaneInspiration::profile(mechanics.getHeroCaster()).corePercent == 0) return {};
	return [this, &candidate, &mechanics, actorId](const auto & board, auto injuries, bool completed, bool localOnly)
	{
		// The active identity is immutable for this already generated candidate batch.
		if(!board->battleGetUnitByID(actorId)) return 0.0f;
		return inspirationOutcome(candidate, mechanics, board, injuries, completed, localOnly, actorId);
	};
}

namespace
{
float expectedPuppetMasterActivationSwing(const CSpell * spell, const spells::Target & target,
	const CGHeroInstance * caster, bool metamagicFollowup, bool metamagicGrand, int overcharge,
	const std::shared_ptr<HypotheticBattle> & currentBattle, DamageCache & damageCache,
	const Environment * environment, PlayerColor valuedPlayer, BattleSide valuedSide, float * inspirationValue,
	const heroSpellProjection::OutcomeValue & outcomeValue)
{
	if((!spell || spell->getJsonKey() != "new-horizons:puppetMaster") || target.size() != 1 || !target.front().unitValue
		|| !caster || !currentBattle || !environment)
		return 0.0f;

	const auto targetId = target.front().unitValue->unitId();
	const auto * originalTarget = currentBattle->battleGetUnitByID(targetId);
	if(!originalTarget || !originalTarget->alive() || originalTarget->isGhost() || originalTarget->isTurret())
		return 0.0f;

	PotentialTargets ordinaryActions(originalTarget, damageCache, currentBattle);
	const float ordinaryActionValue = std::max(0.0f, static_cast<float>(ordinaryActions.bestActionValue()));

	auto projectedBattle = std::make_shared<HypotheticBattle>(environment, currentBattle);
	const auto * projectedTarget = projectedBattle->battleGetUnitByID(targetId);
	if(!projectedTarget)
		return 0.0f;

	spells::Target projectedAim{spells::Destination(projectedTarget)};
	spells::BattleCast projectedCast(projectedBattle.get(), caster, spells::Mode::HERO, spell);
	projectedCast.setMetamagicFollowup(metamagicFollowup);
	projectedCast.setMetamagicGrand(metamagicGrand);
	projectedCast.setOvercharge(overcharge);
	projectedCast.setMetamagicTargetUnitId(targetId);
	auto projectedMechanics = spell->battleMechanics(&projectedCast);
	spells::detail::ProblemImpl problem;
	if(!projectedMechanics->canBeCastAt(projectedAim, problem))
		return 0.0f;
	const auto castSide = projectedMechanics->getCasterSide();
	const auto prepared = outcomeValue ? projectedBattle->prepareHeroSpellAllowance(castSide, spell->getId(), metamagicFollowup, metamagicGrand) : std::nullopt;
	if(outcomeValue && (!prepared || !projectedBattle->beginProjectedHeroAction(castSide, *prepared))) return 0.0f;
	projectedMechanics->castEval(projectedBattle->getServerCallback(), projectedAim);
	if(prepared && !projectedBattle->projectAcceptedHeroSpell(castSide, spell->getId(), targetId,
		metamagicFollowup, metamagicGrand, false, false, *prepared)) return 0.0f;
	auto without = projectedBattle;
	if(outcomeValue)
	{
		without = std::make_shared<HypotheticBattle>(environment, projectedBattle);
		without->reverseArcaneInspirationEffects(projectedBattle->getArcaneInspirationEffects());
	}
	// The cast can materialize a copy-on-write unit; query its post-cast identity.
	projectedTarget = projectedBattle->battleGetUnitByID(targetId);
	if(!projectedTarget || projectedBattle->battleGetActionController(projectedTarget) != valuedPlayer
		|| projectedBattle->playerToSide(projectedBattle->battleGetActionController(projectedTarget)) != valuedSide)
		return 0.0f;

	DamageCache controlledDamage(&damageCache);
	controlledDamage.buildDamageCache(without, valuedSide);
	PotentialTargets controlledActions(without->battleGetUnitByID(targetId), controlledDamage, without);
	const float controlledActionValue = std::max(0.0f, static_cast<float>(controlledActions.bestActionValue()));
	const float applicationChance = 1.0f - static_cast<float>(std::clamp(originalTarget->magicResistance(), 0, 100)) / 100.0f;

	if(outcomeValue && inspirationValue)
	{
		auto coreBoard = std::make_shared<HypotheticBattle>(environment, currentBattle);
		const auto core = outcomeValue(coreBoard, {}, false, false);
		const auto kernel = [&](const std::shared_ptr<HypotheticBattle> & board)
		{
			const auto * unit = board->battleGetUnitByID(targetId);
			if(!unit || !unit->alive() || unit->isGhost()) return 0.0f;
			DamageCache local;
			PotentialTargets actions(unit, local, board);
			const auto value = std::max(0.0f, static_cast<float>(actions.bestActionValue()));
			return board->battleGetActionController(unit) == valuedPlayer ? value : -value;
		};
		const auto localCore = heroSpellProjection::localReceiptMarginal(coreBoard, kernel);
		outcomeValue(projectedBattle, {}, true, true);
		*inspirationValue = core + applicationChance * (heroSpellProjection::localReceiptMarginal(projectedBattle, kernel) - localCore);
	}
	// The target's ordinary activation would benefit its true owner; Puppet
	// Master replaces that value with a real action selected by its controller.
	return (ordinaryActionValue + controlledActionValue) * applicationChance;
}

}

bool BattleEvaluator::prepareExpectedOutcomes(PossibleSpellcast & candidate, spells::Mechanics & mechanics, uint32_t actorId)
{
	auto & ps = candidate; auto * spell = candidate.spell;
	const auto outcome = inspirationOutcomeEvaluator(candidate, mechanics, actorId);
	const auto view = cb->getBattle(battleID);
	const auto store = [&](float value) { if(outcome && !ps.spellInspirationExpectedValue) ps.spellInspirationExpectedValue = value; };
	if(spell->getJsonKey() == "new-horizons:handOfFate" && newHorizonsMagic::rulesActive(view->getBattle()->getMagicRules())
		&& newHorizonsMagic::spellAllowedBySavedRoster(view->getBattle()->getMagicRules(), spell->getId()))
	{
		const auto damage = SpellTargetEvaluator::handOfFateExpectedDamageValue(&mechanics, ps.dest, playerID, view, env.get(), outcome);
		if(!damage) return false;
		ps.spellHandOfFateExpectedValue = damage->hostileDamageValue * scoreEvaluator.getPositiveEffectMultiplier()
			- 4.0f * damage->friendlyDamageValue * scoreEvaluator.getNegativeEffectMultiplier();
		store(damage->inspirationValue);
	}
	if(const auto * form = mechanics.findEffect<spells::effects::BattleFormEffect>())
	{
		float marginal = 0;
		const auto value = SpellTargetEvaluator::battleFormExpectedOffensiveValue(&mechanics, form, ps.dest, env.get(), view, &marginal, outcome);
		if(!value) return false;
		ps.spellBattleFormExpectedValue = *value * scoreEvaluator.getPositiveEffectMultiplier(); store(marginal);
	}
	if(spell->getJsonKey() == "new-horizons:confusion")
	{
		float marginal = 0;
		const auto value = SpellTargetEvaluator::confusionExpectedActivationValue(&mechanics, ps.dest, env.get(), view, &marginal, outcome);
		if(!value) return false;
		ps.spellConfusionExpectedValue = *value * scoreEvaluator.getPositiveEffectMultiplier(); store(marginal);
		if(*ps.spellConfusionExpectedValue + ps.spellInspirationExpectedValue.value_or(0) <= 0) return false;
	}
	if(spell->getJsonKey() == "new-horizons:puppetMaster")
	{
		float marginal = 0;
		const auto value = expectedPuppetMasterActivationSwing(spell, ps.dest, mechanics.getHeroCaster(),
			ps.metamagicFollowup, ps.metamagicGrand, ps.spellOvercharge, hb, damageCache, env.get(), playerID, side, &marginal, outcome);
		ps.spellPuppetMasterExpectedValue = value * scoreEvaluator.getPositiveEffectMultiplier(); store(marginal);
		if(*ps.spellPuppetMasterExpectedValue + ps.spellInspirationExpectedValue.value_or(0) <= 0) return false;
	}
	return true;
}

bool BattleEvaluator::prepareBlinkOutcome(PossibleSpellcast & ps, spells::Mechanics & mechanics, uint32_t actorId)
{
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue) return false;
	const auto preview = newHorizonsBlink::preview(mechanics, ps.dest.front().unitValue);
	if(!preview) return false;
	const auto view = cb->getBattle(battleID);
	const auto outcome = inspirationOutcomeEvaluator(ps, mechanics, actorId); float marginal = 0;
	ps.spellPlacementHeuristicValue = blinkPositionValue::expected(ps.dest.front().unitValue, *preview, playerID,
  *view, view, damageCache, env.get(), &marginal, outcome) * scoreEvaluator.getPositiveEffectMultiplier();
	if(outcome) ps.spellInspirationExpectedValue = marginal;
	return ps.spellPlacementHeuristicValue + ps.spellInspirationExpectedValue.value_or(0) > 0;
}
