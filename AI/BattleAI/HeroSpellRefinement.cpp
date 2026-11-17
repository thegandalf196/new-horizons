/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "HeroSpellRefinement.h"
#include "HeroDecisionPressure.h"
#include "HeroActionUtility.h"
#include "BattleEvaluatorSpellProjection.h"
#include "BattleExchangeVariant.h"
#include "PossibleSpellcast.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/spells/BattleSpellMechanics.h"
#include "../../lib/spells/CSpell.h"
#include "../../lib/spells/effects/native/Damage.h"
#include "../../lib/spells/effects/native/Timed.h"
#include <cmath>
#include <limits>

namespace heroDecisionPressure
{
namespace
{
bool numeric(const Bonus & bonus)
{
	if(bonus.parameters || bonus.updater || bonus.propagationUpdater)
		return false;
	switch(bonus.type)
	{
		case BonusType::PRIMARY_SKILL:
		case BonusType::CREATURE_DAMAGE:
		case BonusType::GENERAL_DAMAGE_PREMY:
		case BonusType::PERCENTAGE_DAMAGE_BOOST:
		case BonusType::GENERAL_DAMAGE_REDUCTION:
		case BonusType::PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS:
		case BonusType::PHYSICAL_DEFENSE:
		case BonusType::ALWAYS_MAXIMUM_DAMAGE:
		case BonusType::ALWAYS_MINIMUM_DAMAGE:
		case BonusType::ENEMY_ATTACK_REDUCTION:
		case BonusType::ENEMY_DEFENCE_REDUCTION:
		case BonusType::GENERAL_ATTACK_REDUCTION:
		case BonusType::LUCK:
			return true;
		default: return false;
	}
}
std::optional<float> immediateValue(const std::shared_ptr<CBattleInfoCallback> & source,
	const std::shared_ptr<HypotheticBattle> & state, const PossibleSpellcast & candidate,
	DamageCache & damage, const BattleExchangeEvaluator & utility, PlayerColor player)
{
	heroActionUtility::ImmediateSignedUtility immediate{0, 0, 0};
	for(const auto * before : source->battleGetAllUnits(false))
	{
		const auto * after = state->battleGetUnitByID(before->unitId());
		if(!after) return {};
		const auto oldHealth = before->getAvailableHealth(), newHealth = after->getAvailableHealth();
		if(oldHealth != newHealth)
			heroActionUtility::accumulateHealthDelta({nullptr, before, after, oldHealth, newHealth, false},
				candidate, player, damage, state, utility, immediate);
	}
	return heroActionUtility::composeAbsoluteUtility({0}, immediate).value;
}
}

SpellScoutDomain spellScoutDomain(const spells::Mechanics & mechanics, const spells::Target & target)
{
	SpellScoutDomain kind = SpellScoutDomain::Unknown;
	size_t count = 0;
	mechanics.forEachEffect([&](const spells::effects::Effect & effect)
	{
		++count;
		if(effect.indirect || effect.optional) return false;
		if(typeid(effect) == typeid(spells::effects::native::Damage))
		{
			const auto & damage = static_cast<const spells::effects::native::Damage &>(effect);
			if(damage.supportsFiniteForecast() && damage.supportsFiniteForecastTargeting(mechanics))
				kind = SpellScoutDomain::Damage;
		}
		else if(const auto * timed = dynamic_cast<const spells::effects::native::Timed *>(&effect))
		{
			if(typeid(effect) != typeid(spells::effects::native::Timed)
				|| !timed->supportsFiniteForecastPayload()
				|| !timed->supportsFiniteForecastTargeting(mechanics)) return false;
			kind = SpellScoutDomain::NumericTimed;
			const auto recipients = mechanics.getAffectedStacks(target);
			if(recipients.empty()) kind = SpellScoutDomain::Unknown;
			for(const auto * unit : recipients)
			{
				const auto bonuses = timed->preparedRecipientBonuses(&mechanics, unit);
				if(!bonuses || bonuses->empty() || !std::ranges::all_of(*bonuses, numeric))
					kind = SpellScoutDomain::Unknown;
			}
		}
		return false;
	});
	return count == 1 ? kind : SpellScoutDomain::Unknown;
}

bool spellScoutCandidate(const PossibleSpellcast & candidate)
{
	return candidate.command == HeroCommand::NONE && candidate.spell
		&& !candidate.divineMandateFollowup && !candidate.spellRunicEstimatePartial
		&& !candidate.spellHandOfFateExpectedValue && !candidate.spellBattleFormExpectedValue
		&& !candidate.spellPuppetMasterExpectedValue && !candidate.spellConfusionExpectedValue
		&& !candidate.spellInspirationExpectedValue && !candidate.spellOverflowExpectedValue
		&& candidate.spellPlacementHeuristicValue == 0
		&& candidate.spellNaturePoisonValue == 0 && candidate.spellSoulChainDelayedValue == 0
		&& candidate.spellShadowGiftHeuristicValue == 0 && candidate.spellPurifyHeuristicValue == 0
		&& candidate.spell->counteredSpells.empty();
}

std::optional<float> spellScoutUtility(const Environment * environment,
	const std::shared_ptr<CBattleInfoCallback> & source, BattleSide side,
	const PossibleSpellcast & candidate, const DamageCache & originalDamage,
	const BattleExchangeEvaluator & utility, Baseline * pressure,
	const std::shared_ptr<battleSearch::SearchBudget> & budget)
{
	if(!environment || !source || !spellScoutCandidate(candidate)) return {};
	const auto * hero = source->battleGetFightingHero(side);
	if(!hero || newHorizonsDivineMandate::needsRecipientCapture(hero)) return {};
	// Validate effect/target descriptors before admission cleanup or target
	// enumeration can invoke unsupported native conversion/shape handlers.
	spells::BattleCast probe(source.get(), hero, spells::Mode::HERO, candidate.spell);
	probe.setOvercharge(candidate.spellOvercharge);
	auto probeMechanics = candidate.spell->battleMechanics(&probe);
	if(spellScoutDomain(*probeMechanics, candidate.dest) == SpellScoutDomain::Unknown) return {};
	auto state = std::make_shared<HypotheticBattle>(environment, source,
		std::shared_ptr<const battle::OwnedBattleFrameBatch>(), budget);
	const auto admission = heroActionProjection::admitCandidate(*state, side, candidate);
	if(!admission.admitted || !admission.spellAllowance) return {};
	const auto & receipt = admission.spellAllowance->action.receipt;
	const auto & future = state->getDoubleCommandState(side);
	if(receipt.allowance != HeroActionAllowanceState::AllowanceKind::HERO
		|| receipt.source != HeroActionAllowanceState::GrantSource::ROUND
		|| future.orderPending() || future.secondWindReady()) return {};
	auto target = candidate.dest;
	for(auto & destination : target)
		if(destination.unitValue)
			{
			destination.unitValue = state->battleGetUnitByID(destination.unitValue->unitId());
			if(!destination.unitValue) return {};
		}
	spells::BattleCast cast(state.get(), hero, spells::Mode::HERO, candidate.spell);
	cast.setOvercharge(candidate.spellOvercharge);
	auto mechanics = candidate.spell->battleMechanics(&cast);
	const auto available = state->getProjectedManaAvailable(side);
	if(!available || mechanics->getTargetAwareManaCost(target) > *available
		|| !mechanics->canBeCastAt(target)) return {};
	const auto kind = spellScoutDomain(*mechanics, target);
	if(kind == SpellScoutDomain::Unknown || (kind == SpellScoutDomain::NumericTimed && !pressure)) return {};
	const auto units = state->battleGetAllUnits(false);
	if(units.size() < 2) return {};
	heroActionProjection::CanonicalSourceAdmission sources(*units.front(), *state);
	if(!sources.admits(*units.back())) return {};
	uint32_t targetId = std::numeric_limits<uint32_t>::max();
	const auto projected = battleEvaluatorProjection::projectSpell(*state, *source, hero,
		side, candidate, admission, targetId, {});
	if(!projected.accepted || state->hasObstacleChanges() || state->hasWallChanges()) return {};
	DamageCache cache(&originalDamage);
	const auto immediate = immediateValue(source, state, candidate, cache, utility, hero->getCasterOwner());
	const auto completion = metamagicAI::completionValue(environment, state, hero, candidate.spell,
		projected.metamagic, originalDamage, utility.getPositiveEffectMultiplier(), utility.getNegativeEffectMultiplier());
	const auto marginal = kind == SpellScoutDomain::NumericTimed ? pressure->spellUtility(state) : std::optional<float>(0);
	if(!immediate || !completion || !marginal) return {};
	const float value = *immediate + *marginal + *completion + candidate.spellRunicExpectedValue;
	return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
}
}
