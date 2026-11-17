/*
 * HeroSpellExpectedValueProjection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "../../lib/nativeRules/CompiledMetamagicRules.h"
#include "HeroSpellExpectedValueProjection.h"
#include "PossibleSpellcast.h"
#include "BattleEvaluatorSpellProjection.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/bonuses/BonusSelector.h"
#include "../../lib/bonuses/BonusList.h"

#include <limits>

namespace
{
// Borrow the original field; read its float only after acceptance succeeds.
const float * expectedValueView(const PossibleSpellcast & ps)
{
	// Preserve precedence even for a malformed candidate with multiple populated fields.
	// Hand of Fate and Battle Form cover the full random pool, not RNGStub's one outcome.
	if(ps.command == HeroCommand::NONE && ps.spellHandOfFateExpectedValue.has_value())
		return &*ps.spellHandOfFateExpectedValue;
	if(ps.command == HeroCommand::NONE && ps.spellBattleFormExpectedValue.has_value())
		return &*ps.spellBattleFormExpectedValue;
	if(ps.command == HeroCommand::NONE && ps.spellConfusionExpectedValue.has_value())
		return &*ps.spellConfusionExpectedValue;
	// Control has no immediate HP delta; retain its precomputed activation swing.
	if(ps.command == HeroCommand::NONE && ps.spellPuppetMasterExpectedValue.has_value())
		return &*ps.spellPuppetMasterExpectedValue;
	if(ps.command == HeroCommand::NONE && ps.spellOverflowExpectedValue.has_value())
		return &*ps.spellOverflowExpectedValue;
	return nullptr;
}
}

bool heroSpellProjection::acceptTerminalEffects(HypotheticBattle & state, BattleSide side,
	const PossibleSpellcast & candidate, uint32_t & targetId,

	const HypotheticBattle::ProjectedSpellAllowance & prepared, const std::vector<uint32_t> & recipients,
	battleEvaluatorProjection::SpellProjection * completion)
{
	const auto current = state.prepareHeroSpellAllowance(side, candidate.spell->getId());
	if(!current || *current != prepared) return false;
	const auto * hero = state.getSideHero(side);
	if(arcaneInspiration::profile(hero).corePercent > 0
		|| (hero && nativeRules::metamagicSkillRank(hero) > 0
			&& nativeRules::metamagicRules(hero) && nativeRules::metamagicRules(hero)->policy()))
	{
		heroActionProjection::Admission admission;
		admission.admitted = true; admission.spellAllowance = prepared;
		const auto gift = candidate.spell->getJsonKey() == newHorizonsShadowGift::SPELL_ID;
		const auto giftTarget = gift && candidate.dest.size() == 1 && candidate.dest.front().unitValue
			? state.getForUpdate(candidate.dest.front().unitValue->unitId()) : nullptr;
		const auto giftSelector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(candidate.spell->getId()))
			.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
		const auto previousGift = giftTarget ? giftTarget->getBonuses(giftSelector) : TConstBonusListPtr{};
		auto projected = battleEvaluatorProjection::projectSpell(state, state, hero, side, candidate,
			admission, targetId, recipients);
		const bool accepted = projected.accepted;
		if(completion) *completion = std::move(projected);
		if(accepted && giftTarget)
		{
			const auto currentGift = giftTarget->getBonuses(giftSelector);
			const bool newlyGranted = std::ranges::any_of(*currentGift, [&](const auto & bonus)
			{
				return bonus->val > 0 && bonus->turnsRemain > 0
					&& std::ranges::none_of(*previousGift, [&](const auto & old) { return old.get() == bonus.get(); });
			});
			if(newlyGranted)
			{
				// Native action pays AFTER a real new Gift status, using separate current/max bases.
				const auto rate = newHorizonsShadowGift::getSacrificeCostBasisPoints(
					candidate.spellShadowGiftSacrificePercent, newHorizonsMagic::hasDarkGiftPerk(hero));
				auto paid = newHorizonsShadowGift::getSacrificeHealthAmount(giftTarget->getShadowGiftCurrentHealth(), rate);
				const auto cap = newHorizonsShadowGift::getSacrificeHealthAmount(giftTarget->getShadowGiftMaximumHealth(), rate);
				if(paid <= 0 || cap <= 0) throw std::logic_error("Accepted Shadow Gift has no payable health cost");
				giftTarget->addShadowGiftMaximumHealthLoss(cap);
				giftTarget->damageShadowGiftSacrifice(paid);
			}
		}
		return accepted;
	}
	return state.projectAcceptedHeroSpell(side, candidate.spell->getId(), targetId, prepared, recipients);
}

bool heroSpellProjection::projectPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & sharedPurposeSpellRecipients, uint32_t & targetId)
{
	const auto expected = expectedValueView(ps);
	if(!expected)
		return false;
	if(ps.dest.size() != 1 || !ps.dest.front().unitValue)
	{
		ps.value = std::numeric_limits<float>::lowest();
		return true;
	}
	targetId = ps.dest.front().unitValue->unitId();
	if(!state.projectAcceptedHeroSpell(side, ps.spell->getId(), targetId, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *expected;
	return true;
}

bool heroSpellProjection::projectNoEffectOrPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & recipients, uint32_t & targetId)
{
	return projectPrecomputedValue(ps, state, side, baseline,
		spellAllowance, recipients, targetId);
}

bool heroSpellProjection::projectDeferredDamageValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & recipients, uint32_t & targetId,
	battleEvaluatorProjection::SpellProjection * completion)
{
	if(ps.command != HeroCommand::NONE) return false;
	const float * expected = ps.spellNaturePoisonValue > 0.0f ? &ps.spellNaturePoisonValue
		: ps.spellSoulChainDelayedValue > 0.0f ? &ps.spellSoulChainDelayedValue : nullptr;
	if(!expected) return false;
	if(!acceptTerminalEffects(state, side, ps, targetId,
		*spellAllowance, recipients, completion))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *expected;
	return true;
}
