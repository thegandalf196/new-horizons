/*
 * NewHorizonsMagicPresentation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsMagicPresentation.h"
#include "NewHorizonsMagicInternal.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSorceryRevision.h"
#include "ISpellMechanics.h"
#include "NewHorizonsSpellAvailability.h"
#include "CSpell.h"
#include "../mapObjects/CGHeroInstance.h"
#include "NewHorizonsImplosion.h"
#include "../GameLibrary.h"
#include "../texts/CGeneralTextHandler.h"

#include <array>

namespace newHorizonsMagic::presentation
{
using detail::hasMaledictionPerk;

std::string percentFromBasisPoints(int basisPoints)
{
	const int wholePercent = basisPoints / 100;
	int hundredths = basisPoints % 100;
	if(hundredths == 0)
		return std::to_string(wholePercent) + "%";
	if(hundredths < 10)
		return std::to_string(wholePercent) + ".0" + std::to_string(hundredths) + "%";
	if(hundredths % 10 == 0)
		return std::to_string(wholePercent) + "." + std::to_string(hundredths / 10) + "%";
	return std::to_string(wholePercent) + "." + std::to_string(hundredths) + "%";
}

std::string fixedPointFromScaledValue(int64_t scaledValue, int decimalPlaces)
{
	int64_t scale = 1;
	for(int index = 0; index < decimalPlaces; ++index)
		scale *= 10;
	const int64_t whole = scaledValue / scale;
	int64_t fractional = scaledValue % scale;
	if(fractional == 0)
		return std::to_string(whole);
	std::string fractionalText = std::to_string(fractional);
	fractionalText.insert(0, static_cast<size_t>(decimalPlaces) - fractionalText.size(), '0');
	while(!fractionalText.empty() && fractionalText.back() == '0')
		fractionalText.pop_back();
	return std::to_string(whole) + "." + fractionalText;
}

namespace
{
bool describeTownPortal(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getId() == SpellID(SpellID::TOWN_PORTAL)
		&& isAdventureSpell(hero->getMagicRules(), spell->getId()))
	{
		result = LIBRARY->generaltexth->translate("new-horizons.adventure.townPortal.targetingHint");
		return true;
	}
	return false;
}

bool describeEarthquake(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && earthquakeRulesEnabled(hero->getMagicRules(), spell->getId()))
	{
		const bool geomancer = hero->hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.geomancer");
		const auto & parameters = hero->getMagicRules()["spells"]["core:earthquake"]["earthquake"];
		const int structuralDamage = parameters["structuralDamage"].Integer() * (geomancer ? 125 : 100) / 100;
		result = "Siege: select an intact fortification section. Damages that section and the nearest eligible "
			"wall, gate or tower sections, up to min(4, 2 + floor(scaled Spell Power / 80)). Each loses "
			+ std::to_string(structuralDamage) + " structural HP. Field combat: select a radius-2 area. "
			"Grounded stacks on either side take 30 + 0.8 x scaled Spell Power damage before defenses. "
			"Fractured Ground lasts " + std::to_string(geomancer ? 4 : 3)
			+ " rounds before eligible duration extensions; each entered hex costs one extra movement point. "
			"Flying travel ignores this cost. No Initiative, Attack, Defense or retaliation penalty. "
			"School rank, Spellcraft, Warcasting and Empower scale only Spell Power terms, not fixed bases or caps.";
		return true;
	}
	return false;
}

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
	const int64_t pool = profile["baseHitPoints"].Integer()
		+ spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
			power * profile["spellPowerCoefficient"].Integer(),
			std::max<int32_t>(1, hero->getEffectPowerDivisor(spell)),
			spellPowerCoefficientBasisPoints(rules, hero, spell->getId()), 0,
			empowerSpellBonusPercent(rules, hero, spell->getId()));
	result = "Summon Living Armor on an empty hex. Current pool: " + std::to_string(pool)
		+ " HP before battle-only Warcasting. Lifetime: " + std::to_string(profile["duration"].Integer())
		+ " rounds, including this round. Eligible Extend Spell and Echoed Duration each add one round. "
		"75% physical damage reduction; takes double magical damage. Time Stop pauses its lifetime. "
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
		const int64_t powerReduction = spellPower * combinedCoefficientBasisPoints
			/ (5LL * SPELL_POWER_COEFFICIENT_BASIS_POINTS);
		const int initiativeReduction = std::min(50, 20 + static_cast<int>(powerReduction));
		const int variantPower = spellVariantPowerPercent(rules, spell->getId());
		const int displayedReduction = initiativeReduction * variantPower / 100;
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
			+ "%. This estimate excludes battle-only Warcasting and the Inferno defender's Brimstone Stormclouds "
			"+20 Spell Power bonus, which can further affect the battle cast.";
		if(variantPower == 60)
			result += " Mass Slow applies 60% of the final ordinary Slow reduction after its cap and target-specific "
				"specialties, rounded toward zero; duration is unchanged. Temporal Field grants this distinct spell, "
				"without a once-per-combat limit. Its listed Mana cost is three times Slow before Wisdom.";
		return true;
	}
	return false;
}

bool describeMisfortune(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getId() == SpellID::MISFORTUNE
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId()))
	{
		result = "Target one enemy stack. Positive Luck cannot trigger; negative Luck is unchanged. "
			"Favorable random creature abilities use max(25%, 75% - 0.25% x scaled Spell Power) "
			"of their normal probability. Deterministic abilities remain deterministic. "
			"Ordinary duration is min(4, 2 + floor(scaled Spell Power / 80)) rounds, "
			"before eligible cast-specific duration extensions. School rank and Spellcraft scale "
			"only the Spell Power terms, not the fixed base or the 25% floor.";
		if(hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.misfortuneWeaver"))
			result += " Misfortune Weaver subtracts another ten percentage points from the probability "
				"multiplier, retaining the 25% floor.";
		return true;
	}
	return false;
}

bool describePolymorph(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getJsonKey() == "new-horizons:polymorph"
		&& rulesActive(hero->getMagicRules())
		&& spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId()))
	{
		result = "Target one enemy stack. Transform it into a random creature of the same tier "
			"from any faction for two rounds. Preserve current aggregate HP, allegiance and Initiative position. "
			"Use the new body's statistics, abilities and attack type; relocate to the nearest legal position if required. "
			"Expiry or Dispel restores surviving HP to the original body. If no original footprint can fit anywhere, "
			"keep the current form, HP and position with restoration pending, and retry at later round boundaries. "
			"Phantom Army's offensive body transforms while its separate Integrity is unchanged. "
			"School rank never makes this a mass spell.";
		if(hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.shapeshifter"))
			result += " Shapeshifter draws two forms independently with replacement, keeping the lower whole-stack "
				"Army Value after exact HP conversion; ties use canonical creature order.";
		return true;
	}
	return false;
}

bool describeHandOfFate(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getJsonKey() == "new-horizons:handOfFate"
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId())
		&& hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.fateDealer"))
	{
		result += " Fate Dealer draws two spill candidates independently with replacement. "
			"If exactly one is hostile to your current side, it receives the spill; otherwise a fair coin "
			"chooses between the two draws, including duplicates. The selected recipient's own defenses "
			"still apply, without a reroll or a second hit.";
		return true;
	}
	return false;
}

bool describeForgetfulness(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getId() == SpellID::FORGETFULNESS
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId()))
	{
		result = "Target one enemy stack. It may only move, make basic melee attacks, Wait and Defend. "
			"Shooting, activated abilities, special attacks, triggered creature abilities and creature spellcasting "
			"are suppressed. Ordinary retaliation and structural creature properties remain unchanged. "
			"Duration = min(3, 1 + floor(scaled Spell Power / 80)) rounds before eligible duration extensions. "
			"School rank scales the Spell Power term, not the fixed base or cap; Expert never makes this a mass spell.";
		if(hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.mindbreaker"))
			result += " Mindbreaker also suppresses intrinsic passive offensive abilities for this duration.";
		return true;
	}
	return false;
}

bool describeImplosion(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && newHorizonsImplosion::rulesFor(hero->getMagicRules(), spell->getId()))
	{
		const auto profile = *newHorizonsImplosion::rulesFor(hero->getMagicRules(), spell->getId());
		result = "Deals min(" + std::to_string(profile.maximumBasisPoints / 100.0)
			+ "%, " + std::to_string(profile.baseBasisPoints / 100.0)
			+ "% + " + std::to_string(profile.powerBasisPointsPerSpellPower / 100.0)
			+ "% x scaled Spell Power) of the target's current aggregate Health before magical defenses. "
			"School rank scales only the Spell Power term, not the base or cap. "
			"After resolved damage, other living stacks within " + std::to_string(profile.pullRadius)
			+ " hexes are pulled one legal hex closer. Closest stacks move first, then clockwise from northeast. "
			"The primary target stays in place; this magical displacement causes no retaliation.";
		return true;
	}
	return false;
}

bool describePoison(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && physicalPoisonEnabled(hero->getMagicRules(), spell->getId()))
	{
		result = "Target one enemy living stack. It suffers physical Poison damage on its next three activations: "
			"20 + 0.5 x Spell Power, then 1.5x and 2x that amount. Nature rank scales only the Spell Power term. "
			"The affliction does not spread; Cure removes it, while Dispel does not.";
		return true;
	}
	return false;
}

bool describeBerserk(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getId() == SpellID::BERSERK
		&& berserkUsesSingleCreatureTarget(hero->getMagicRules()))
	{
		result = "Target one enemy stack. It attacks the nearest creature until its next attack. "
			"Nearest is determined by battlefield movement cost, regardless of allegiance; tied stacks are chosen randomly. "
			"Shooters are forced into melee. If out of range, the stack approaches the selected target. "
			"The ordinary cast targets only the selected stack at every mastery rank.";
		return true;
	}
	return false;
}

bool describeDispel(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getId() == SpellID::DISPEL
		&& dispelUsesNewHorizonsRules(hero->getMagicRules()))
	{
		result = dispelRemovesTemporaryMagicalEffectsOnly(hero->getMagicRules())
			? "Target one friendly or enemy stack. Removes all temporary magical buffs and debuffs. "
			: "Target one friendly or enemy stack. Removes dispellable spell effects. ";
		result += "Does not remove Orders, innate creature states, physical poison, terrain, or summoned creatures. "
			"Sorcery rank does not make Dispel affect multiple stacks.";
		return true;
	}
	return false;
}

bool describeChainLightning(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getId() == SpellID::CHAIN_LIGHTNING
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		result = "Select the first target. Lightning then jumps to the nearest eligible unstruck creature, "
			"including friendly stacks. It can strike up to five different stacks at every mastery rank; "
			"each later jump deals less damage.";
		return true;
	}
	return false;
}

bool describeCurse(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && curseRulesEnabled(hero->getMagicRules(), spell->getId()))
	{
		const auto duration = curseDurationRounds(hero->getMagicRules(), hero, spell->getId())
			.value_or(CURSE_BASE_DURATION_ROUNDS);
		const bool mass = spellVariantBase(hero->getMagicRules(), spell->getId()) != spell->getId();
		result = std::string(mass ? "Targets every eligible enemy stack for " : "Targets one enemy stack for ") + std::to_string(duration)
			+ (duration == 1 ? " round. " : " rounds. ")
			+ "It always rolls the minimum value of its normal creature damage range and changes no other statistic.";
		if(hero->getMagicRules()["spells"]["core:curse"].Struct().contains("schoolRankDurations"))
			result += " School rank sets the base duration to 3/4/4/5 rounds at no rank/Basic/Advanced/Expert; it does not grant Mass Curse.";
		if(hasMaledictionPerk(hero))
			result += " Malediction extends the duration by one round.";
		if(mass)
			result += " Granted by Grand Malediction; costs three times Curse before Mana reductions.";
		return true;
	}
	return false;
}

bool describeSorrow(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && sorrowRulesEnabled(hero->getMagicRules(), spell->getId()))
	{
		const auto & rules = hero->getMagicRules();
		const int32_t spellPower = std::max<int32_t>(0, hero->getEffectPower(spell));
		const int schoolCoefficient = spellPowerCoefficientPercent(rules, hero, spell->getId());
		const int combinedCoefficient = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const auto penalty = sorrowMoralePenalty(rules, hero, spell->getId(), spellPower)
			.value_or(SORROW_BASE_MORALE_PENALTY);
		const auto duration = sorrowDurationRounds(rules, hero, spell->getId())
			.value_or(SORROW_BASE_DURATION_ROUNDS);
		const bool mass = spellVariantBase(rules, spell->getId()) != spell->getId();
		result = std::string(mass ? "Targets every eligible enemy stack for " : "Targets one enemy stack for ") + std::to_string(duration)
			+ (duration == 1 ? " round. " : " rounds. ")
			+ "Morale penalty = min(3, 1 + floor(scaled raw Hero Spell Power / 70)). "
			"Saved v3 Shadow rank scales the Spell Power term by 100% / 115% / 130% / 145% at no rank / Basic / "
			"Advanced / Expert; Spellcraft efficiency multiplies the School factor. "
			+ std::string(mass ? "The Mass cast affects every eligible enemy stack. " : "The ordinary cast is single-target at every rank. ")
			+ "Current Shadow School factor: "
			+ std::to_string(schoolCoefficient) + "%; combined School and Spellcraft factor: "
			+ percentFromBasisPoints(combinedCoefficient) + ". At Spell Power " + std::to_string(spellPower)
			+ ", the ordinary penalty is -" + std::to_string(penalty) + " Morale before battle-only Warcasting. "
			"Morale remains subject to the global legal range.";
		if(duration > SORROW_BASE_DURATION_ROUNDS)
			result += " Malediction extends the duration by one round.";
		if(mass)
			result += " Granted by Grand Malediction; costs three times Sorrow before Mana reductions.";
		return true;
	}
	return false;
}

}

void applyBaseDescription(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(describeTownPortal(result, hero, spell))
		return;
	if(describeEarthquake(result, hero, spell))
		return;
	if(describeLivingArmor(result, hero, spell))
		return;
	if(describeSlow(result, hero, spell))
		return;
	if(describeMisfortune(result, hero, spell))
		return;
	if(describePolymorph(result, hero, spell))
		return;
	if(describeHandOfFate(result, hero, spell))
		return;
	if(describeForgetfulness(result, hero, spell))
		return;
	if(describeImplosion(result, hero, spell))
		return;
	if(describePoison(result, hero, spell))
		return;
	if(describeBerserk(result, hero, spell))
		return;
	if(describeDispel(result, hero, spell))
		return;
	if(describeChainLightning(result, hero, spell))
		return;
	if(describeCurse(result, hero, spell))
		return;
	if(describeSorrow(result, hero, spell))
		return;
}
}
