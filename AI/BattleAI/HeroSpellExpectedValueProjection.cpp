/*
 * HeroSpellExpectedValueProjection.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroSpellExpectedValueProjection.h"
#include "PossibleSpellcast.h"
#include "BattleEvaluatorSpellProjection.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/NewHorizonsShadowGift.h"
#include "../../lib/spells/CSpell.h"
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
	return nullptr;
}
}

bool heroSpellProjection::acceptTerminalEffects(HypotheticBattle & state, BattleSide side,
	const PossibleSpellcast & candidate, uint32_t & targetId,
	const HypotheticBattle::ProjectedCounterspellOutcome & counterspell, bool negated,
	const HypotheticBattle::ProjectedSpellAllowance & prepared, const std::vector<uint32_t> & recipients)
{
	const auto current = state.prepareHeroSpellAllowance(side, candidate.spell->getId(),
		candidate.metamagicFollowup, candidate.metamagicGrand);
	if(!current || *current != prepared) return false;
	const auto * hero = state.getSideHero(side);
	if(!negated && arcaneInspiration::profile(hero).corePercent > 0)
	{
		heroActionProjection::Admission admission;
		admission.admitted = true; admission.spellAllowance = prepared;
		admission.counterspell = counterspell; admission.counterspellNegated = negated;
		const auto gift = candidate.spell->getJsonKey() == newHorizonsShadowGift::SPELL_ID;
		const auto giftTarget = gift && candidate.dest.size() == 1 && candidate.dest.front().unitValue
			? state.getForUpdate(candidate.dest.front().unitValue->unitId()) : nullptr;
		const auto giftSelector = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(candidate.spell->getId()))
			.And(Selector::type()(BonusType::COMBAT_EVENT_TRIGGER));
		const auto previousGift = giftTarget ? giftTarget->getBonuses(giftSelector) : TConstBonusListPtr{};
		const bool accepted = battleEvaluatorProjection::projectSpell(state, state, hero, side, candidate,
			admission, targetId, recipients).accepted;
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
	return state.projectAcceptedHeroSpell(side, candidate.spell->getId(), targetId,
		candidate.metamagicFollowup, candidate.metamagicGrand, counterspell.wardActive,
		negated, prepared, recipients);
}

bool heroSpellProjection::projectPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
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
	if(counterspellNegated
		|| !state.projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, sharedPurposeSpellRecipients))
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *expected;
	return true;
}

bool heroSpellProjection::projectNoEffectOrPrecomputedValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & recipients, uint32_t & targetId)
{
	if(newHorizonsMagic::isCounterspell(ps.spell))
	{
		if(!state.projectAcceptedHeroSpell(side, ps.spell->getId(), targetId,
			ps.metamagicFollowup, ps.metamagicGrand, counterspell.wardActive,
			counterspellNegated, *spellAllowance, recipients) || counterspellNegated)
			ps.value = std::numeric_limits<float>::lowest();
		return true;
	}
	return projectPrecomputedValue(ps, state, side, baseline, counterspell, counterspellNegated,
		spellAllowance, recipients, targetId);
}

bool heroSpellProjection::projectDeferredDamageValue(PossibleSpellcast & ps, HypotheticBattle & state,
	BattleSide side, const float & baseline, const HypotheticBattle::ProjectedCounterspellOutcome & counterspell,
	bool counterspellNegated, const std::optional<HypotheticBattle::ProjectedSpellAllowance> & spellAllowance,
	const std::vector<uint32_t> & recipients, uint32_t & targetId)
{
	if(ps.command != HeroCommand::NONE) return false;
	const float * expected = ps.spellNaturePoisonValue > 0.0f ? &ps.spellNaturePoisonValue
		: ps.spellSoulChainDelayedValue > 0.0f ? &ps.spellSoulChainDelayedValue : nullptr;
	if(!expected) return false;
	if(!acceptTerminalEffects(state, side, ps, targetId, counterspell, counterspellNegated,
		*spellAllowance, recipients) || counterspellNegated)
		ps.value = std::numeric_limits<float>::lowest();
	else
		ps.value = baseline + *expected;
	return true;
}
