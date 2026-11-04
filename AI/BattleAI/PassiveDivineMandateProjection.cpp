/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "PassiveDivineMandateProjection.h"
#include "FixedActionValue.h"
#include "LightSpellProtectionValue.h"
#include "HeroPurifyProjection.h"
#include "HypotheticBattle.h"
#include "../../lib/battle/PassiveDivineMandateSpellPool.h"
#include "../../lib/battle/PassiveDivineMandateInspiration.h"
#include "../../lib/battle/HeroOrderRecipients.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/SetStackEffect.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/NewHorizonsMetamagicEffects.h"
#include "../../lib/spells/NewHorizonsPurify.h"

namespace passiveDivineMandateAI
{
std::optional<float> projectedValue(const Environment * environment,
	const std::shared_ptr<HypotheticBattle> & postOrder, BattleSide side,
	const HeroOrderState & order, const DamageCache & damage,
	float positiveEffectMultiplier, float negativeEffectMultiplier, Workload * workload)
{
	if(workload) *workload = {};
	if(!postOrder || !order.passiveDivineMandate) return 0.0f;
	const auto * hero = postOrder->battleGetFightingHero(side);
	if(!hero) return std::nullopt;
	const auto next = newHorizonsPassiveDivineMandate::afterAcceptedCast(*order.passiveDivineMandate,
		{postOrder->getPassiveDivineMandateUses(side)});
	if(!next) return 0.0f;
	spells::AutomaticHeroCastReceipt receipt{spells::AutomaticHeroCastKind::DIVINE_MANDATE, next->completedCasts};
	receipt.issuingOrder = order.command;
	receipt.issuingRound = order.issuedRound;
	if(order.passiveDivineMandate->has(newHorizonsPassiveDivineMandate::Perk::INSPIRING_LEADERSHIP))
		receipt.inspiringDuration = order.revision && order.command == HeroCommand::VANGUARD
			? std::clamp(order.revision->expiresAfterRound - order.issuedRound + 1, 1, 3) : 1;
	const auto recipients = heroCommands::recipientIds(*postOrder, side, order);
	const auto pool = newHorizonsPassiveDivineMandate::buildSpellPool(*postOrder, *hero, receipt, recipients);
	if(pool.empty()) return 0.0f;
	int64_t friendlyHealth = 0;
	std::vector<const battle::Unit *> enemies;
	for(const auto * unit : postOrder->battleGetAllUnits(false))
		if(unit && unit->alive())
		{
			if(postOrder->battleGetOwner(unit) == postOrder->sideToPlayer(side)) friendlyHealth += unit->getAvailableHealth();
			else enemies.push_back(unit);
		}
	const float magicalThreat = visibleCreatureSpellThreat(enemies, true);
	fixedActionValue::Reference reference(postOrder, postOrder->sideToPlayer(side), damage,
		positiveEffectMultiplier, negativeEffectMultiplier);
	bool uncertain = false;
	Workload measured;
	const auto expected = expectedOneDraw(pool, [&](const auto & spell, const auto & target) -> std::optional<float>
	{
		auto state = std::make_shared<HypotheticBattle>(environment, postOrder);
		const auto * definition = spell.spell.toSpell();
		const auto ward = state->resolveProjectedCounterspell(side, definition);
		if(!ward.resolutionKnown || !ward.negated)
		{ uncertain = true; return std::nullopt; }
		if(!state->projectAutomaticCounterspell(ward)) return std::nullopt;
		const auto action = newHorizonsPassiveDivineMandate::automaticSpellAction(*state,
			side, spell.spell, target.aimUnitId);
		spells::BattleCast cast(state.get(), hero, spells::Mode::HERO, definition);
		cast.setAutomaticHeroCast(receipt);
		if(ward.wardActive) cast.setCounterspell(ward.wardSide, *ward.negated,
			*ward.negated ? ward.manaCost.value() : 0);
		cast.setCureAffliction(action.spellCureAffliction);
		cast.setCurePhysicalAffliction(action.spellCurePhysicalAffliction);
		cast.setPurifyChoices(action.spellPurifyChoices);
		const auto aim = action.getTarget(state.get());
		const auto mechanics = cast.mechanicsForTarget(aim);
		if(!mechanics->canBeCastAt(aim)) return std::nullopt;
		mechanics->castEval(state->getServerCallback(), aim);
		if(!*ward.negated && spell.spell == newHorizonsPurify::spellID())
		{
			const auto resolved = heroSpellProjection::projectAutomaticPurifyEffects(*state, action, hero);
			if(!resolved) return std::nullopt;
			const auto marks = newHorizonsMetamagic::captureHeroCastProfile(hero, state.get(), true);
			newHorizonsMetamagic::publishResolvedMarks(*state->getServerCallback(),
				marks.perks.rank > 0 ? &marks : nullptr, *state, *resolved);
			auto inspiration = newHorizonsPassiveDivineMandate::inspiringSpeedEffects(*state,
				side, receipt, *resolved);
			if(!inspiration.toAdd.empty()) state->getServerCallback()->apply(inspiration);
		}
		const auto common = reference.marginal(state, target.recipientUnitIds);
		if(!common) return std::nullopt;
		DamageCache cache(&damage);
		float protection = 0;
		const auto * family = spell.family.toSpell();
		if(!family) return std::nullopt;
		const auto key = family->getJsonKey();
		// Gale and Guardian physical protection is already repriced by the fixed attacks above.
		for(const auto id : target.recipientUnitIds)
		{
			const auto * before = postOrder->battleGetUnitByID(id);
			const auto * after = state->battleGetUnitByID(id);
			if(key == "new-horizons:holyArmor")
				protection += holyArmorMitigationValue(side, before, after, spell.family,
					friendlyHealth, magicalThreat, cache, state);
			else if(key == "new-horizons:divineRetribution")
				protection += divineRetributionThreatValue(before, after, side, *postOrder, state, spell.family, cache);
		}
		return *common + protection * positiveEffectMultiplier;
	}, &measured);
	measured.uncertainAutomaticBenefit = uncertain ? 1 : 0;
	if(workload) *workload = measured;
	return expected;
}
}
