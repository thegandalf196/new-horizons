/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once

#include "PassiveFactionSkillPresentation.h"
#include "../../lib/battle/ConsecratedArms.h"
#include <algorithm>
#include <vector>

namespace passiveFactionSkillPresentation
{
enum class CurrentSkill { NONE, METAMAGIC, CONSECRATED_ARMS };

inline CurrentSkill currentSkill(const std::string & key)
{
	if(key == "new-horizons:metamagic")
		return CurrentSkill::METAMAGIC;
	if(key == consecratedArms::SKILL)
		return CurrentSkill::CONSECRATED_ARMS;
	return CurrentSkill::NONE;
}

struct MetamagicReadout
{
	int rank = 0;
	int overflowPercent = 0;
	int friendlyReductionPercent = 0;
	int retryPercent = 0;
	std::optional<int> unravelChance;
	std::optional<int> reconstituteChance;
	bool arcaneCascade = false;
};

inline std::optional<SkillStatusText> metamagicText(const MetamagicReadout & input)
{
	if(input.rank <= 0 || input.overflowPercent <= 0)
		return std::nullopt;
	std::string details = "Single-target spell overkill transfers " + std::to_string(input.overflowPercent)
		+ "% of excess damage "
		+ (input.arcaneCascade ? "to each of the two nearest other distinct enemies" : "to the nearest other enemy")
		+ ", subject to each recipient's magical defenses."
		"\nOwn area, chain and global spells deal " + std::to_string(100 - input.friendlyReductionPercent)
		+ "% damage to friendly creatures."
		"\nA resisted single-target debuff has a " + std::to_string(input.retryPercent)
		+ "% chance to retry the normal resistance check once. Immunity is not bypassed.";
	if(input.unravelChance)
		details += "\nUnraveling Magic: " + std::to_string(*input.unravelChance)
			+ "% chance to remove each eligible beneficial magical effect from the enemy recipient.";
	if(input.reconstituteChance)
		details += "\nReconstituting Magic: " + std::to_string(*input.reconstituteChance)
			+ "% chance to remove each eligible harmful magical effect from the friendly recipient.";
	return SkillStatusText{std::to_string(input.overflowPercent) + "%", std::move(details), true};
}

inline std::optional<SkillStatusText> consecratedText(const consecratedArms::Policy & policy, int faith)
{
	if(policy.rank <= 0)
		return std::nullopt;
	const auto potency = consecratedArms::potencyFaith(faith, policy);
	const auto rounds = consecratedArms::duration(faith, policy);
	std::string details = "Each issued Order grants "
		+ std::string(policy.has(consecratedArms::Perk::ANTIPHONAL_PRAYER) ? "two different random prayers" : "one random prayer")
		+ " to its recipients. Reapplying a prayer refreshes it without stacking."
		"\n\nCurrent Faith: " + std::to_string(faith) + ". Potency Faith: " + std::to_string(potency)
		+ ". New prayer duration: " + std::to_string(rounds) + " rounds."
		"\n\nPrayer pool: Divine Purpose, Dauntless Assault, Steadfast Soul, Vicarious Redemption, Unbroken Vow, Heaven's Judgment."
		"\nInspect a recipient's status for its active prayers and remaining duration.";
	return SkillStatusText{"F " + std::to_string(faith), std::move(details), true};
}

inline std::string prayerPercent(int basisPoints)
{
	std::string result = std::to_string(basisPoints / 100);
	const int fraction = basisPoints % 100;
	if(fraction)
	{
		result += fraction < 10 ? ".0" : ".";
		result += std::to_string(fraction);
		if(result.back() == '0')
			result.pop_back();
	}
	return result + "%";
}

inline std::string prayerEffect(const consecratedArms::ActivePrayer & prayer, int distinctPrayers)
{
	using namespace consecratedArms;
	const auto value = values(prayer.potencyFaith, prayer.policy, distinctPrayers);
	switch(prayer.prayer)
	{
		case Prayer::DIVINE_PURPOSE:
			return "Morale and Luck: +" + std::to_string(value.moraleLuck) + "; Speed: +"
				+ std::to_string(value.attributes) + ". Ordinary eligibility and attribute caps apply.";
		case Prayer::DAUNTLESS_ASSAULT:
			return "Damage from retaliation provoked by this stack's melee attack: -"
				+ prayerPercent(value.retaliationReductionBP) + "."
				+ (prayer.policy.has(Perk::FEARLESS_ADVANCE) ? " Also has a 20% chance to provoke no retaliation." : "");
		case Prayer::STEADFAST_SOUL:
			return "Additional resistance against hostile non-damaging spells: " + prayerPercent(value.resistanceBP)
				+ ". Uses the normal resistance check."
				+ (prayer.policy.has(Perk::UNSHAKABLE_FAITH) ? " Also protects against qualifying creature afflictions." : "");
		case Prayer::VICARIOUS_REDEMPTION:
			return "After damage, " + prayerPercent(value.redemptionConversionBP)
				+ " of this stack's fallen creatures' maximum HP becomes a shield against later physical and magical damage."
				" Cannot save an eliminated stack. Inspect the shield status for stored HP and capacity."
				+ (prayer.policy.has(Perk::COMMUNION_OF_SAINTS)
					? " Other friendly stacks' casualties also grant shield at half this conversion rate." : "");
		case Prayer::UNBROKEN_VOW:
			return "One attack or damaging spell can kill at most " + prayerPercent(value.casualtyCeilingBP)
				+ " of the creatures currently in this stack, rounded up. A single creature can still die.";
		case Prayer::HEAVENS_JUDGMENT:
			return "Attacks and retaliations add " + prayerPercent(value.judgmentBP)
				+ " magical damage based on actual creature HP lost, excluding temporary HP and shields, subject to the target's magical defenses."
				+ (prayer.policy.has(Perk::RIGHTEOUS_SENTENCE)
					? " This bonus damage is 50% greater against enemies that have already completed an action this round." : "");
	}
	return {};
}

inline std::vector<SkillStatusText> prayerTexts(const consecratedArms::PrayerSnapshot & prayers)
{
	const int count = static_cast<int>(std::count_if(prayers.begin(), prayers.end(), [](const auto & prayer)
	{
		return prayer.has_value();
	}));
	std::vector<SkillStatusText> result;
	for(const auto & prayer : prayers)
	{
		if(!prayer)
			continue;
		const std::string name = consecratedArms::name(prayer->prayer);
		const std::string duration = std::to_string(prayer->remainingRounds)
			+ (prayer->remainingRounds == 1 ? " round" : " rounds");
		result.push_back({name + " (" + duration + ")",
			name + ": " + prayerEffect(*prayer, count)
			+ "\n\nPotency Faith: " + std::to_string(prayer->potencyFaith)
			+ ". Remaining duration: " + duration + ".", true});
	}
	return result;
}
}
