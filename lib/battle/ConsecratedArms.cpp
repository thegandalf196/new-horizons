/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ConsecratedArms.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../json/JsonNode.h"
#include <algorithm>
#include <array>
#include <bit>
#include <limits>
#include <stdexcept>

namespace consecratedArms
{
bool Policy::has(Perk perk) const
{
	return rank != 0 && (perks & static_cast<uint16_t>(perk)) != 0;
}

void Policy::validate() const
{
	if(rank < 0 || rank > 3 || (perks & ~uint16_t{0x3ff}) || (!rank && perks))
		throw std::runtime_error("Invalid Consecrated Arms policy");
	if(rankFaithBonus < 0 || rankFaithBonus > 100 || ferventAdditionalFaith < 0 || ferventAdditionalFaith > 100)
		throw std::runtime_error("Invalid Consecrated Arms captured Faith bonuses");
	if(std::popcount(static_cast<unsigned>(perks & 0x00f)) > 1
		|| std::popcount(static_cast<unsigned>(perks & 0x0f0)) > 1
		|| std::popcount(static_cast<unsigned>(perks & 0x300)) > 1)
		throw std::runtime_error("Consecrated Arms permits one perk per tier");
}

bool enabled(const JsonNode & rules)
{
	const auto & policy = rules["divineMandatePolicy"];
	return policy.isStruct() && policy["version"].getType() == JsonNode::JsonType::DATA_INTEGER
		&& (policy["version"].Integer() == 3 || policy["version"].Integer() == 4);
}

bool chosenPrayers(const JsonNode & rules)
{
	const auto & policy = rules["divineMandatePolicy"];
	return enabled(rules) && policy["version"].Integer() == 4 && policy["chosen"].isBool() && policy["chosen"].Bool();
}

Policy capturePolicy(const CGHeroInstance * hero)
{
	Policy result;
	if(!hero || hero->getFactionID() != FactionID::CASTLE || !enabled(hero->getPerkState().getRules()))
		return result;
	result.rank = std::clamp(hero->getPerkSkillRank(SKILL), 0, 3);
	const auto & authored = hero->getPerkState().getRules()["divineMandatePolicy"];
	result.rankFaithBonus = static_cast<int>(authored["potencyFaithBonusByRank"].Vector().at(result.rank).Integer());
	result.ferventAdditionalFaith = static_cast<int>(authored["ferventInvocationAdditionalFaith"].Integer());
	result.chosen = chosenPrayers(hero->getPerkState().getRules());
	static constexpr std::array<const char *, 10> keys = {
		"enduringPrayer", "joyfulDevotion", "fearlessAdvance", "unshakableFaith",
		"communionOfSaints", "righteousSentence", "ferventInvocation", "sacredAccord",
		"antiphonalPrayer", "livingLiturgy"};
	if(result.rank)
		for(size_t index = 0; index < keys.size(); ++index)
			if(hero->hasActivePerk(SKILL, std::string(SKILL) + "." + keys[index]))
				result.perks |= uint16_t{1} << index;
	result.validate();
	return result;
}

int actualFaith(const CGHeroInstance & hero)
{
	const int64_t faith = int64_t{hero.getPrimSkillLevel(PrimarySkill::SPELL_POWER)}
		+ hero.getPrimSkillLevel(PrimarySkill::KNOWLEDGE);
	return static_cast<int>(std::clamp<int64_t>(faith, 0, std::numeric_limits<int>::max() - 200));
}

int potencyFaith(int faith, const Policy & policy)
{
	policy.validate();
	return static_cast<int>(std::clamp<int64_t>(faith, 0, std::numeric_limits<int>::max() - 200))
		+ policy.rankFaithBonus + (policy.has(Perk::FERVENT_INVOCATION) ? policy.ferventAdditionalFaith : 0);
}

int duration(int faith, const Policy & policy)
{
	policy.validate();
	return std::min(4, 2 + std::max(0, faith) / 150) + (policy.has(Perk::ENDURING_PRAYER) ? 1 : 0);
}

Values values(int faith, const Policy & policy, int distinctPrayers)
{
	policy.validate();
	const int64_t safeFaith = std::max(0, faith);
	const int accord = policy.has(Perk::SACRED_ACCORD)
		? 100 + 10 * std::clamp(distinctPrayers - 1, 0, 3) : 100;
	const auto amplify = [accord](int64_t magnitude, int cap)
	{
		return static_cast<int>(std::min<int64_t>(cap, magnitude * accord / 100));
	};
	Values result;
	result.attributes = amplify(1 + safeFaith / 150, 3);
	result.moraleLuck = result.attributes + (policy.has(Perk::JOYFUL_DEVOTION) ? 1 : 0);
	result.retaliationReductionBP = amplify(2000 + safeFaith * 10, 6000);
	result.resistanceBP = amplify(1000 + safeFaith * 5, 4000);
	result.redemptionConversionBP = amplify(1000 + safeFaith * 10, 5000);
	result.casualtyCeilingBP = 10000 - amplify(3000 + safeFaith * 5, 6000);
	result.judgmentBP = amplify(500 + safeFaith * 5, 2500);
	return result;
}

std::vector<Prayer> selectPrayers(const Policy & policy, int first, int second)
{
	policy.validate();
	if(!policy.rank || first < 0 || first >= 6 || second < 0 || second >= 5)
		throw std::runtime_error("Invalid Consecrated Arms prayer draw");
	std::vector<Prayer> result{static_cast<Prayer>(first)};
	if(policy.has(Perk::ANTIPHONAL_PRAYER))
		result.push_back(static_cast<Prayer>(second >= first ? second + 1 : second));
	return result;
}

const char * name(Prayer prayer)
{
	static constexpr std::array<const char *, 6> names = {
		"Divine Purpose", "Dauntless Assault", "Steadfast Soul",
		"Vicarious Redemption", "Unbroken Vow", "Heaven's Judgment"};
	return names.at(static_cast<size_t>(prayer));
}

const char * textID(Prayer prayer)
{
	static constexpr std::array<const char *, 6> keys = {
		"new-horizons.consecratedArms.prayers.divinePurpose",
		"new-horizons.consecratedArms.prayers.dauntlessAssault",
		"new-horizons.consecratedArms.prayers.steadfastSoul",
		"new-horizons.consecratedArms.prayers.vicariousRedemption",
		"new-horizons.consecratedArms.prayers.unbrokenVow",
		"new-horizons.consecratedArms.prayers.heavensJudgment"};
	return keys.at(static_cast<size_t>(prayer));
}
}
