/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsMagicPresentation.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSorceryRevision.h"
#include "NewHorizonsTimedBuffMagnitude.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
#include <algorithm>
#include <array>

namespace newHorizonsMagic::presentation
{
namespace
{
bool describeLivingArmor(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(!hero || spell->getJsonKey() != "new-horizons:summonLivingArmor"
		|| !spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId()))
		return false;
	const auto & rules = hero->getMagicRules();
	const auto & profile = rules["spells"][spell->getJsonKey()]["livingArmor"];
	if(!profile.isStruct())
		return false;
	const auto power = static_cast<int64_t>(std::max<int32_t>(0, hero->getEffectPower(spell)));
	const int64_t pool = newHorizonsSorceryRevision::livingArmorHitPoints(
		profile["baseHitPoints"].Integer(), profile["spellPowerCoefficient"].Integer(), static_cast<int32_t>(power),
		std::max<int32_t>(1, hero->getEffectPowerDivisor(spell)),
		spellPowerCoefficientBasisPoints(rules, hero, spell->getId()),
		empowerSpellBonusPercent(rules, hero, spell->getId()));
	result = "Summon Living Armor on an empty hex. Current pool: " + std::to_string(pool)
		+ " HP. Lifetime: " + std::to_string(profile["duration"].Integer())
		+ " rounds, including this round, before eligible Extend Spell, Spellbinder and Wisdom duration modifiers. "
		"75% physical damage reduction; takes double magical damage. "
		"Disappears after combat.";
	return true;
}

bool describeSlow(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spellVariantBase(hero->getMagicRules(), spell->getId()) == SpellID::SLOW
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId()))
	{
		const auto & rules = hero->getMagicRules();
		int schoolRank = MasteryLevel::NONE;
		for(const auto skill : spellSchoolSkills(rules, spell->getId()))
			schoolRank = std::max(schoolRank, static_cast<int>(hero->getSecSkillLevel(skill)));
		const int schoolCoefficientPercent = spellPowerCoefficientPercent(rules, hero, spell->getId());
		const int combinedCoefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int64_t spellPower = std::max<int32_t>(0, hero->getEffectPower(spell));
		const int initiativeReduction = newHorizonsSorceryRevision::slowBaseReduction(static_cast<int32_t>(spellPower),
			combinedCoefficientBasisPoints, empowerSpellBonusPercent(rules, hero, spell->getId()));
		const int variantPower = spellVariantPowerPercent(rules, spell->getId());
		const bool chronomancer = newHorizonsSorceryRevision::hasChronomancer(hero);
		const auto displayedReduction = static_cast<int64_t>(-newHorizonsSorceryRevision::finalSlowValue(
			casterEnchantValue(hero, SpellID::SLOW, -initiativeReduction), variantPower, chronomancer));
		constexpr std::array<std::string_view, 4> rankNames{"No rank", "Basic", "Advanced", "Expert"};
		const auto rankName = rankNames.at(static_cast<size_t>(std::clamp(schoolRank,
			static_cast<int>(MasteryLevel::NONE), static_cast<int>(MasteryLevel::EXPERT))));

		result = std::string(variantPower == 60 ? "Affects every eligible enemy stack. " : "Target one enemy stack. ")
			+ (newHorizonsSorceryRevision::slowAffectsSpeed(rules)
				? "Reduces Speed and Initiative equally. Fixed base duration: "
				: "Reduces Initiative only, not Speed or movement. Fixed base duration: ")
			+ std::to_string(SLOW_BASE_DURATION_ROUNDS) + " rounds before Temporalist, other Spell Duration bonuses, "
			"and eligible cast-specific extensions. Current Sorcery rank: "
			+ std::string(rankName) + " (School factor " + std::to_string(schoolCoefficientPercent)
			+ "%, combined School and Spellcraft factor " + percentFromBasisPoints(combinedCoefficientBasisPoints)
			+ "). " + (newHorizonsSorceryRevision::slowAffectsSpeed(rules) ? "Speed and Initiative reduction" : "Initiative reduction")
			+ " = min(50%, 20% + floor(Spell Power x combined coefficient / 5)); "
			"current reduction before target-specific specialties at Spell Power "
			+ std::to_string(spellPower) + ": " + std::to_string(displayedReduction)
			+ "%. This estimate excludes the Inferno defender's Brimstone Stormclouds "
			"+20 Spell Power bonus, which can further affect the battle cast.";
		if(chronomancer) result += " Chronomancer strengthens the reduction by 20%, capped at 60%, before mass normalization.";
		if(newHorizonsSorceryRevision::spellbinderBonus(hero, spell->getId()) > 0)
			result += " Spellbinder adds 2 ordinary rounds.";
		if(variantPower == 60)
			result += " Mass Slow applies 60% of the final ordinary Slow reduction after its cap and target-specific "
				"specialties, rounded toward zero; duration is unchanged. Temporal Field grants this distinct spell, "
				"without a once-per-combat limit. Its listed Mana cost is three times Slow before Wisdom.";
		return true;
	}
	return false;
}

}

bool describeSorcerySpell(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	return describeLivingArmor(result, hero, spell) || describeSlow(result, hero, spell);
}
}
