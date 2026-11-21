/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsValue.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/battle/ConsecratedArmsCombat.h"
#include "../../lib/battle/HeroOrderRecipients.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/ConsecratedArmsChoice.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <array>
#include <cmath>
#include <limits>

namespace consecratedArmsAI
{
namespace
{
/// Chosen Prayers price three prayers the random-draw expectation leaves unpriced.
double chosenTerm(const consecratedArms::ActivePrayer & prayer, const consecratedArms::Values & value,
	double duration, const Exposure & exposure)
{
	switch(prayer.prayer)
	{
	case consecratedArms::Prayer::HEAVENS_JUDGMENT:
		// Own attacks plus about half as much retaliation against melee pressure.
		return duration * (exposure.outgoing + (exposure.counterStrikes ? 0.5 * exposure.outgoing : 0))
			* value.judgmentBP / 10000.0;
	case consecratedArms::Prayer::VICARIOUS_REDEMPTION:
		// The shield follows casualties, so only a later hit can use it.
		return duration * 0.5 * exposure.incoming * value.redemptionConversionBP / 10000.0;
	case consecratedArms::Prayer::STEADFAST_SOUL:
		// A resisted debuff preserves roughly half a round of this stack's output.
		return duration * exposure.enemySpellThreat * 0.5 * exposure.outgoing * value.resistanceBP / 10000.0;
	default: return 0;
	}
}

double exposureValue(const Prayers & prayers, const Exposure & exposure, bool chosen = false)
{
	const auto count = std::count_if(prayers.begin(), prayers.end(), [](const auto & prayer) { return prayer.has_value(); });
	double result = 0;
	for(const auto & prayer : prayers)
	{
		if(!prayer) continue;
		const auto value = consecratedArms::values(prayer->potencyFaith, prayer->policy, count);
		// One current activation and half a further activation, matching the
		// existing protection heuristics; never simulate another combat turn.
		const double duration = prayer->remainingRounds <= 0 ? 0 : prayer->remainingRounds == 1 ? 1 : 1.5;
		switch(prayer->prayer)
		{
		case consecratedArms::Prayer::DIVINE_PURPOSE:
			result += duration * exposure.outgoing * (value.attributes / static_cast<double>(std::max(1, exposure.movement))
				+ (exposure.moraleEligible ? std::min(value.moraleLuck, exposure.moraleRoom) * 0.01 : 0)
				+ std::min(value.moraleLuck, exposure.luckRoom) * 0.01);
			break;
		case consecratedArms::Prayer::DAUNTLESS_ASSAULT:
		{
			const double provoked = prayer->policy.has(consecratedArms::Perk::FEARLESS_ADVANCE) ? 0.8 : 1;
			result += duration * exposure.retaliation * (1 - provoked * (1 - value.retaliationReductionBP / 10000.0));
			break;
		}
		case consecratedArms::Prayer::UNBROKEN_VOW:
		{
			const auto permitted = consecratedArms::casualtyLimit(exposure.count, value.casualtyCeilingBP);
			// Retain the leading creature's partial HP and ceil casualty rule.
			const auto maximumDamage = permitted >= exposure.count ? exposure.incoming
				: static_cast<double>(exposure.firstHP) + exposure.maximumHP * permitted - 1;
			result += duration * std::max(0.0, exposure.incoming - maximumDamage);
			break;
		}
		default: // Explicit partial estimate, never certainty of zero future benefit.
			if(chosen) result += chosenTerm(*prayer, value, duration, exposure);
			break;
		}
	}
	return result;
}
}

std::optional<float> expectedPrayers(const consecratedArms::Policy & policy, const OutcomeValue & evaluate)
{
	policy.validate();
	if(!policy.rank) return 0.0f;
	if(!evaluate) return std::nullopt;
	const bool pair = policy.has(consecratedArms::Perk::ANTIPHONAL_PRAYER);
	double sum = 0;
	for(int first = 0; first < 6; ++first)
	{
		for(int second = pair ? first + 1 : 6; second <= (pair ? 5 : 6); ++second)
		{
			const std::array prayers{static_cast<consecratedArms::Prayer>(first),
				static_cast<consecratedArms::Prayer>(pair ? second : first)};
			const auto value = evaluate(std::span(prayers).first(pair ? 2 : 1));
			if(!value || !std::isfinite(*value)) return std::nullopt;
			sum += *value;
		}
	}
	const auto result = static_cast<float>(sum / (pair ? 15 : 6));
	return std::isfinite(result) ? std::optional<float>(result) : std::nullopt;
}

namespace
{
Prayers afterGrant(const consecratedArms::Policy & policy, int faith, const Prayers & before,
	std::span<const consecratedArms::Prayer> selected)
{
	auto after = before;
	for(auto & prayer : after)
		if(prayer && policy.has(consecratedArms::Perk::LIVING_LITURGY))
			prayer->remainingRounds = consecratedArms::duration(faith, policy);
	for(const auto prayer : selected)
		after[static_cast<size_t>(prayer)] = consecratedArms::ActivePrayer{prayer,
			consecratedArms::potencyFaith(faith, policy), consecratedArms::duration(faith, policy), policy};
	return after;
}
}

float expectedRecipientValue(const consecratedArms::Policy & policy, int faith,
	const Prayers & before, const Exposure & exposure)
{
	const auto prior = exposureValue(before, exposure);
	const auto expected = expectedPrayers(policy, [&](auto selected) -> std::optional<float>
	{
		return static_cast<float>(exposureValue(afterGrant(policy, faith, before, selected), exposure) - prior);
	});
	return expected.value_or(std::numeric_limits<float>::quiet_NaN());
}

float chosenRecipientValue(const consecratedArms::Policy & policy, int faith,
	const Prayers & before, const Exposure & exposure, std::span<const consecratedArms::Prayer> selected)
{
	return static_cast<float>(exposureValue(afterGrant(policy, faith, before, selected), exposure, true)
		- exposureValue(before, exposure, true));
}

std::optional<Choice> bestChoice(const consecratedArms::Policy & policy, int faith,
	std::span<const RecipientExposure> recipients)
{
	const size_t count = consecratedArms::requiredPrayerChoices(policy);
	if(!count || count > 2 || recipients.empty())
		return std::nullopt;
	std::optional<Choice> best;
	for(const auto & recipient : recipients)
		for(int first = 0; first < 6; ++first)
			for(int second = count == 2 ? first + 1 : 6; second <= (count == 2 ? 5 : 6); ++second)
			{
				const std::array prayers{static_cast<consecratedArms::Prayer>(first),
					static_cast<consecratedArms::Prayer>(count == 2 ? second : first)};
				const auto selected = std::span(prayers).first(count);
				const auto value = chosenRecipientValue(policy, faith, recipient.before, recipient.exposure, selected);
				if(!std::isfinite(value) || (best && value <= best->value))
					continue;
				best = Choice{recipient.recipient, {selected.begin(), selected.end()}, value};
			}
	return best;
}

namespace
{
double enemySpellThreat(const CBattleInfoCallback & battle, BattleSide side, const battle::Units & units)
{
	const auto * enemyHero = battle.battleGetFightingHero(battle.otherSide(side));
	double threat = enemyHero && enemyHero->hasSpellbook() ? 0.75 : 0;
	for(const auto * unit : units)
		if(unit && unit->alive() && unit->unitSide() != side && unit->hasBonusOfType(BonusType::SPELLCASTER))
			threat += 0.25;
	return std::min(1.0, threat);
}

/// A shooter with ammunition that is not blocked shoots instead of meleeing.
bool shoots(const CBattleInfoCallback & battle, const battle::Unit & unit)
{
	return unit.canShoot() && (unit.canShootBlocked() || !battle.battleIsUnitBlocked(&unit));
}

/// Whether `attacker` will melee `defender` and that blow can be retaliated.
bool retaliatedMelee(const CBattleInfoCallback & battle, const battle::Unit & attacker, const battle::Unit & defender)
{
	return !shoots(battle, attacker) && !attacker.hasBonusOfType(BonusType::BLOCKS_RETALIATION)
		&& !defender.hasBonusOfType(BonusType::NO_RETALIATION);
}

/// Exposure against the nearest visible enemy; at most two damage-cache queries.
std::optional<RecipientExposure> measure(const std::shared_ptr<HypotheticBattle> & state, BattleSide side,
	uint32_t id, const battle::Units & units, DamageCache & cache, double spellThreat)
{
	const auto * unit = state->battleGetUnitByID(id);
	if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret()
		|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
		|| state->battleGetOwner(unit) != state->sideToPlayer(side)) return std::nullopt;
	const battle::Unit * nearest = nullptr;
	for(const auto * enemy : units)
		if(enemy && enemy->alive() && !enemy->isGhost() && state->battleGetOwner(enemy) != state->sideToPlayer(side)
			&& (!nearest || std::pair{BattleHex::getDistance(unit->getPosition(), enemy->getPosition()), enemy->unitId()}
				< std::pair{BattleHex::getDistance(unit->getPosition(), nearest->getPosition()), nearest->unitId()})) nearest = enemy;
	if(!nearest) return std::nullopt;
	battleSearch::require(state->searchBudget().get(), battleSearch::WorkKind::Node);
	RecipientExposure result;
	result.recipient = id;
	auto & exposure = result.exposure;
	exposure.outgoing = std::min<int64_t>(nearest->getAvailableHealth(), cache.getDamage(unit, nearest, state));
	exposure.incoming = std::min<int64_t>(unit->getAvailableHealth(), cache.getDamage(nearest, unit, state));
	// Dauntless Assault answers only retaliation after the bearer's own melee.
	exposure.retaliation = retaliatedMelee(*state, *unit, *nearest) ? exposure.incoming : 0;
	exposure.counterStrikes = retaliatedMelee(*state, *nearest, *unit);
	exposure.count = unit->getCount(); exposure.firstHP = unit->getFirstHPleft(); exposure.maximumHP = unit->getMaxHealth();
	exposure.movement = unit->getMovementRange(0); exposure.moraleEligible = !unit->unaffectedByMorale();
	exposure.enemySpellThreat = spellThreat;
	for(size_t index = 0; index < result.before.size(); ++index)
		result.before[index] = consecratedArms::activePrayer(*unit, static_cast<consecratedArms::Prayer>(index));
	const auto purpose = consecratedArms::prayerValues(*unit, consecratedArms::Prayer::DIVINE_PURPOSE);
	exposure.moraleRoom = std::max(0, 10 - state->battleGetMorale(unit) + purpose.moraleLuck);
	exposure.luckRoom = std::max(0, 10
		- state->battleGetAttackLuck(unit, nearest, false) + purpose.moraleLuck);
	return result;
}
}

std::vector<RecipientExposure> measureRecipients(const std::shared_ptr<HypotheticBattle> & state, BattleSide side,
	const std::vector<uint32_t> & recipients, const DamageCache & damage)
{
	DamageCache cache(&damage);
	const auto units = state->battleGetAllUnits(false);
	const auto threat = enemySpellThreat(*state, side, units);
	std::vector<RecipientExposure> result;
	for(const auto id : recipients)
		if(auto measured = measure(state, side, id, units, cache, threat))
			result.push_back(std::move(*measured));
	return result;
}

void applyChosenPrayers(const CBattleInfoCallback & live, const std::shared_ptr<HypotheticBattle> & state,
	const DamageCache & damage, BattleAction & action)
{
	if(!action.prayerChoices.empty() || action.prayerRecipient != BattleAction::NO_PRAYER_RECIPIENT)
		return;
	const auto request = consecratedArms::choiceRequest(live, action);
	if(!request.prayers || request.recipients.empty())
		return;
	const auto * hero = live.battleGetFightingHero(action.side);
	std::optional<Choice> choice;
	if(hero && state)
	{
		try
		{
			const auto measured = measureRecipients(state, action.side, request.recipients, damage);
			choice = bestChoice(request.policy, consecratedArms::actualFaith(*hero), measured);
		}
		catch(const battleSearch::BudgetExhausted &)
		{
			choice.reset(); // The action is already selected; keep it legal.
		}
	}
	if(!choice) // No measurable exposure: any legal choice is equivalent to the heuristic.
	{
		choice.emplace();
		choice->recipient = request.recipients.front();
		for(size_t index = 0; index < request.prayers; ++index)
			choice->prayers.push_back(static_cast<consecratedArms::Prayer>(index));
	}
	action.prayerRecipient = choice->recipient;
	for(const auto prayer : choice->prayers)
		action.prayerChoices.push_back(static_cast<uint8_t>(prayer));
}

std::optional<float> projectedValue(const Environment *,
	const std::shared_ptr<HypotheticBattle> & postOrder, BattleSide side,
	const HeroOrderState & order, const consecratedArms::Policy & policy,
	const DamageCache & damage, float positive, float negative)
{
	if(!postOrder) return std::nullopt;
	const auto * hero = postOrder->battleGetFightingHero(side);
	if(!policy.rank) return 0.0f;
	const auto faith = consecratedArms::actualFaith(*hero);
	if(consecratedArms::requiredPrayerChoices(policy))
	{
		// Chosen Prayers: one recipient receives the commander's best choice.
		const auto measured = measureRecipients(postOrder, side,
			consecratedArms::eligiblePrayerRecipients(*postOrder, side, order), damage);
		const auto choice = bestChoice(policy, faith, measured);
		if(!choice) return 0.0f;
		return choice->value * (choice->value >= 0 ? positive : negative);
	}
	const auto recipients = heroCommands::recipientIds(*postOrder, side, order);
	if(recipients.empty()) return 0.0f;
	double result = 0;
	for(const auto & measured : measureRecipients(postOrder, side, recipients, damage))
	{
		const auto value = expectedRecipientValue(policy, faith, measured.before, measured.exposure);
		result += value * (value >= 0 ? positive : negative);
	}
	const auto value = static_cast<float>(result);
	return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
}
}
