/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsRangefinder.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "BattleHexArray.h"
#include "../bonuses/IBonusBearer.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include "../bonuses/BonusSelector.h"
#include "../bonuses/Limiters.h"
#include <typeinfo>
#include <stdexcept>
namespace newHorizonsRangefinder
{
namespace
{
constexpr std::string_view identity = "new-horizons:ordinary-creature-specialty:core:christian";
}
void configureEnvelope(Bonus & bonus, int capturedDistance)
{
	if(capturedDistance != 0 && capturedDistance != 12)
		throw std::runtime_error("Invalid captured Rangefinder distance");
	bonus.val = GameConstants::BATTLE_SHOOTING_RANGE_DISTANCE;
	bonus.parameters = std::make_shared<BonusParameters>(capturedDistance == 0
		? GameConstants::BATTLE_SHOOTING_PENALTY_DISTANCE : capturedDistance);
}
bool isEnvelope(const Bonus & bonus)
{
	if(bonus.stacking != identity)
		return false;
	const auto * family = dynamic_cast<const CCreatureTypeLimiter *>(bonus.limiter.get());
	if(bonus.source != BonusSource::HERO_SPECIAL
		|| bonus.sid != BonusSourceID(HeroTypeID(HeroTypeID::decode("core:christian")))
		|| bonus.type != BonusType::LIMITED_SHOOTING_RANGE || bonus.duration != BonusDuration::PERMANENT
		|| bonus.val != GameConstants::BATTLE_SHOOTING_RANGE_DISTANCE || !bonus.parameters
		|| bonus.valType != BonusValueType::BASE_NUMBER || !family
		|| typeid(*family) != typeid(CCreatureTypeLimiter)
		|| family->creatureID != CreatureID(CreatureID::decode("core:ballista")) || !family->includeUpgrades
		|| bonus.effectRange != BonusLimitEffect::NO_LIMIT
		|| bonus.propagator || bonus.updater || bonus.propagationUpdater
		|| !bonus.statusIdentity.empty() || !bonus.statusTags.empty())
		throw std::runtime_error("Invalid Rangefinder envelope provenance");
	const auto distance = bonus.parameters->toNumber();
	if(distance != 10 && distance != 12)
		throw std::runtime_error("Invalid Rangefinder envelope distance");
	return true;
}
std::optional<int> fullDamageDistance(const IBonusBearer & shooter)
{
	if(!shooter.hasBonusOfType(BonusType::LIMITED_SHOOTING_RANGE))
		return std::nullopt;
	static const auto selector = Selector::type()(BonusType::LIMITED_SHOOTING_RANGE).And(
		CSelector([](const Bonus * bonus) { return bonus && isEnvelope(*bonus); }));
	const auto envelope = shooter.getAllBonuses(selector, "newHorizonsRangefinderEnvelope")->getFirst(Selector::all);
	if(!envelope)
		return std::nullopt;
	if(shooter.hasBonusOfType(BonusType::NO_DISTANCE_PENALTY))
		return GameConstants::BATTLE_SHOOTING_RANGE_DISTANCE;
	return envelope->parameters->toNumber();
}
std::shared_ptr<const Bonus> legalRangeBonus(const IBonusBearer & shooter)
{
	// Preserve existing selection/ordering over all non-Rangefinder range sources.
	static const auto selector = Selector::type()(BonusType::LIMITED_SHOOTING_RANGE).And(
		CSelector([](const Bonus * bonus) { return bonus && !isEnvelope(*bonus); }));
	return shooter.getAllBonuses(selector, "newHorizonsRangefinderLegalRange")->getFirst(Selector::all);
}
std::optional<bool> distancePenalty(const IBonusBearer & shooter, const BattleHex & source,
	const BattleHex & destination, const CBattleInfoCallback & battle)
{
	const auto range = fullDamageDistance(shooter);
	if(!range)
		return std::nullopt;
	if(shooter.hasBonusOfType(BonusType::NO_DISTANCE_PENALTY))
		return false;
	const auto * target = battle.battleGetUnitByPos(destination, true);
	if(target)
	{
		for(const auto & hex : target->getHexes())
			if(BattleHex::getDistance(source, hex) <= *range)
				return false;
		return true;
	}
	return BattleHex::getDistance(source, destination) > *range;
}
}
