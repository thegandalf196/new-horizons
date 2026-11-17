/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArms.h"
#include "Unit.h"
#include "../bonuses/Bonus.h"
#include "../bonuses/BonusList.h"
#include "../bonuses/BonusParameters.h"
#include <array>
#include <stdexcept>

namespace consecratedArms
{
namespace
{
constexpr std::array<const char *, 6> identities = {
	"new-horizons:consecratedArms.divinePurpose",
	"new-horizons:consecratedArms.dauntlessAssault",
	"new-horizons:consecratedArms.steadfastSoul",
	"new-horizons:consecratedArms.vicariousRedemption",
	"new-horizons:consecratedArms.unbrokenVow",
	"new-horizons:consecratedArms.heavensJudgment"};

const BonusSourceID & sourceID()
{
	// Content identifiers are immutable after library initialization. Resolve on
	// first prayer creation/read, never on the prayer-absent damage path.
	static const BonusSourceID source{SecondarySkill(SecondarySkill::decode(SKILL))};
	return source;
}
}

std::optional<Prayer> identity(const Bonus * bonus)
{
	if(!bonus || bonus->source != BonusSource::SECONDARY_SKILL
		|| bonus->duration != BonusDuration::N_TURNS)
		return std::nullopt;
	for(size_t index = 0; index < identities.size(); ++index)
		if(bonus->statusIdentity == identities[index])
			return static_cast<Prayer>(index);
	return std::nullopt;
}

PrayerSnapshot activePrayers(const battle::Unit & unit)
{
	PrayerSnapshot result;
	if(!unit.mayHaveBonusType(BonusType::NONE))
		return result;
	const auto markers = unit.getUnstackedBonuses(CSelector([](const Bonus * bonus)
	{
		return bonus->type == BonusType::NONE && bonus->turnsRemain > 0 && identity(bonus).has_value();
	}));
	for(const auto & entry : *markers)
	{
		const auto & marker = *entry;
		const auto prayer = *identity(&marker);
		if(result.at(static_cast<size_t>(prayer)))
			throw std::runtime_error("Duplicate Consecrated Arms prayer state");
		if(!marker.parameters || !marker.parameters->isVector())
			throw std::runtime_error("Consecrated Arms prayer is missing captured inputs");
		const auto & captured = marker.parameters->toVector();
		if(captured.size() != 3 || captured[0] != 1 || captured[1] < 0 || captured[1] > 0x3ff
			|| captured[2] < 1 || captured[2] > 3 || marker.val < 0 || marker.turnsRemain > 5
			|| marker.sid != sourceID()
			|| marker.valType != BonusValueType::ADDITIVE_VALUE || marker.subtype != BonusSubtypeID()
			|| marker.targetSourceType != BonusSource::OTHER || marker.effectRange != BonusLimitEffect::NO_LIMIT
			|| marker.updater || marker.propagationUpdater || marker.limiter || marker.propagator
			|| marker.statusTags != std::vector<BonusStatusTag>{BonusStatusTag::NON_TRANSFERABLE}
			|| marker.stacking != identities.at(static_cast<size_t>(prayer))
			|| (captured[2] < 2 && (captured[1] & 0x0f0))
			|| (captured[2] < 3 && (captured[1] & 0x300)))
			throw std::runtime_error("Invalid Consecrated Arms prayer inputs");
		Policy policy{captured[2], static_cast<uint16_t>(captured[1])};
		policy.validate();
		result.at(static_cast<size_t>(prayer)) = ActivePrayer{prayer, marker.val, marker.turnsRemain, policy};
	}
	return result;
}

std::optional<ActivePrayer> activePrayer(const battle::Unit & unit, Prayer prayer)
{
	return activePrayers(unit).at(static_cast<size_t>(prayer));
}

int activePrayerCount(const battle::Unit & unit)
{
	int result = 0;
	for(const auto & prayer : activePrayers(unit))
		result += prayer.has_value();
	return result;
}

Values prayerValues(const battle::Unit & unit, Prayer prayer)
{
	const auto prayers = activePrayers(unit);
	const auto & active = prayers.at(static_cast<size_t>(prayer));
	int count = 0;
	for(const auto & entry : prayers)
		count += entry.has_value();
	return active ? values(active->potencyFaith, active->policy, count) : Values{};
}

std::vector<Bonus> prayerBonuses(Prayer prayer, int faith, const Policy & policy,
	int rounds, int distinctPrayers)
{
	policy.validate();
	if(policy.rank == 0 || faith < 0 || rounds < 1 || rounds > 5
		|| (policy.rank < 2 && (policy.perks & 0x0f0)) || (policy.rank < 3 && (policy.perks & 0x300)))
		throw std::runtime_error("Invalid Consecrated Arms prayer application");
	const auto & key = identities.at(static_cast<size_t>(prayer));
	Bonus marker(BonusDuration::N_TURNS, BonusType::NONE, BonusSource::SECONDARY_SKILL,
		faith, sourceID());
	marker.turnsRemain = rounds;
	marker.statusIdentity = key;
	marker.statusTags = {BonusStatusTag::NON_TRANSFERABLE};
	marker.stacking = key;
	marker.parameters = std::make_shared<BonusParameters>(std::vector<int32_t>{1, policy.perks, policy.rank});
	marker.description.appendTextID(textID(prayer));
	std::vector<Bonus> result{marker};
	if(prayer == Prayer::DIVINE_PURPOSE)
	{
		const auto magnitude = values(faith, policy, distinctPrayers);
		for(const auto type : {BonusType::MORALE, BonusType::LUCK, BonusType::STACKS_SPEED})
		{
			auto component = marker;
			component.type = type;
			component.val = type == BonusType::STACKS_SPEED ? magnitude.attributes : magnitude.moraleLuck;
			result.push_back(std::move(component));
		}
	}
	return result;
}
}
