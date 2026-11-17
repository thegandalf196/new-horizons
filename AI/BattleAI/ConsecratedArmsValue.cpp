/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArmsValue.h"
#include "AttackPossibility.h"
#include "HypotheticBattle.h"
#include "SearchBudget.h"
#include "../../lib/battle/ConsecratedArmsCombat.h"
#include "../../lib/battle/HeroOrderRecipients.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <array>
#include <cmath>
#include <limits>

namespace consecratedArmsAI
{
namespace
{
double exposureValue(const Prayers & prayers, const Exposure & exposure)
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
		default: break; // Explicit partial estimate, never certainty of zero future benefit.
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

float expectedRecipientValue(const consecratedArms::Policy & policy, int faith,
	const Prayers & before, const Exposure & exposure)
{
	const auto prior = exposureValue(before, exposure);
	const auto expected = expectedPrayers(policy, [&](auto selected) -> std::optional<float>
	{
		auto after = before;
		for(auto & prayer : after)
			if(prayer && policy.has(consecratedArms::Perk::LIVING_LITURGY))
				prayer->remainingRounds = consecratedArms::duration(faith, policy);
		for(const auto prayer : selected)
			after[static_cast<size_t>(prayer)] = consecratedArms::ActivePrayer{prayer,
				consecratedArms::potencyFaith(faith, policy), consecratedArms::duration(faith, policy), policy};
		return static_cast<float>(exposureValue(after, exposure) - prior);
	});
	return expected.value_or(std::numeric_limits<float>::quiet_NaN());
}

std::optional<float> projectedValue(const Environment *,
	const std::shared_ptr<HypotheticBattle> & postOrder, BattleSide side,
	const HeroOrderState & order, const consecratedArms::Policy & policy,
	const DamageCache & damage, float positive, float negative)
{
	if(!postOrder) return std::nullopt;
	const auto * hero = postOrder->battleGetFightingHero(side);
	if(!policy.rank) return 0.0f;
	const auto recipients = heroCommands::recipientIds(*postOrder, side, order);
	if(recipients.empty()) return 0.0f;
	const auto faith = consecratedArms::actualFaith(*hero);
	DamageCache cache(&damage);
	const auto units = postOrder->battleGetAllUnits(false);
	double result = 0;
	for(const auto id : recipients)
	{
		const auto * unit = postOrder->battleGetUnitByID(id);
		if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret()
			|| unit->hasBonusOfType(BonusType::SIEGE_WEAPON)
			|| postOrder->battleGetOwner(unit) != postOrder->sideToPlayer(side)) continue;
		const battle::Unit * nearest = nullptr;
		for(const auto * enemy : units)
			if(enemy && enemy->alive() && !enemy->isGhost() && postOrder->battleGetOwner(enemy) != postOrder->sideToPlayer(side)
				&& (!nearest || std::pair{BattleHex::getDistance(unit->getPosition(), enemy->getPosition()), enemy->unitId()}
					< std::pair{BattleHex::getDistance(unit->getPosition(), nearest->getPosition()), nearest->unitId()})) nearest = enemy;
		if(!nearest) continue;
		battleSearch::require(postOrder->searchBudget().get(), battleSearch::WorkKind::Node);
		Exposure exposure;
		exposure.outgoing = std::min<int64_t>(nearest->getAvailableHealth(), cache.getDamage(unit, nearest, postOrder));
		exposure.incoming = std::min<int64_t>(unit->getAvailableHealth(), cache.getDamage(nearest, unit, postOrder));
		exposure.retaliation = unit->isMeleeAttacker() && nearest->isMeleeAttacker() ? exposure.incoming : 0;
		exposure.count = unit->getCount(); exposure.firstHP = unit->getFirstHPleft(); exposure.maximumHP = unit->getMaxHealth();
		exposure.movement = unit->getMovementRange(0); exposure.moraleEligible = !unit->unaffectedByMorale();
		Prayers before;
		for(size_t index = 0; index < before.size(); ++index)
			before[index] = consecratedArms::activePrayer(*unit, static_cast<consecratedArms::Prayer>(index));
		const auto purpose = consecratedArms::prayerValues(*unit, consecratedArms::Prayer::DIVINE_PURPOSE);
		exposure.moraleRoom = std::max(0, 10 - postOrder->battleGetMorale(unit) + purpose.moraleLuck);
		exposure.luckRoom = std::max(0, 10
			- postOrder->battleGetAttackLuck(unit, nearest, false) + purpose.moraleLuck);
		const auto value = expectedRecipientValue(policy, faith, before, exposure);
		result += value * (value >= 0 ? positive : negative);
	}
	const auto value = static_cast<float>(result);
	return std::isfinite(value) ? std::optional<float>(value) : std::nullopt;
}
}
