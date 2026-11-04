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
#include "NewHorizonsTimedBuffMagnitude.h"
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
			"School rank, Spellcraft and Empower scale only Spell Power terms, not fixed bases or caps.";
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
	const int64_t pool = newHorizonsSorceryRevision::livingArmorHitPoints(
		profile["baseHitPoints"].Integer(), profile["spellPowerCoefficient"].Integer(), static_cast<int32_t>(power),
		std::max<int32_t>(1, hero->getEffectPowerDivisor(spell)),
		spellPowerCoefficientBasisPoints(rules, hero, spell->getId()),
		empowerSpellBonusPercent(rules, hero, spell->getId()));
	result = "Summon Living Armor on an empty hex. Current pool: " + std::to_string(pool)
		+ " HP. Lifetime: " + std::to_string(profile["duration"].Integer())
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
		const int initiativeReduction = newHorizonsSorceryRevision::slowBaseReduction(static_cast<int32_t>(spellPower),
			combinedCoefficientBasisPoints, empowerSpellBonusPercent(rules, hero, spell->getId()));
		const int variantPower = spellVariantPowerPercent(rules, spell->getId());
		const auto displayedReduction = static_cast<int64_t>(-newHorizonsSorceryRevision::scaleSlowVariantValue(
			casterEnchantValue(hero, SpellID::SLOW, -initiativeReduction), variantPower));
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
			+ ", the ordinary penalty is -" + std::to_string(penalty) + " Morale. "
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

namespace newHorizonsMagic
{
namespace
{
// Effect summaries only. Values, duration calculations and exceptions stay in
// spellDescriptionForHero and its existing contextual presentation stages.
constexpr std::pair<std::string_view, std::string_view> spellSummaries[] = {
	{"core:armageddon", "Burn every creature on the battlefield, including allies, and damage fortifications."},
	{"core:berserk", "Force one enemy stack to approach and attack the nearest creature, friend or foe, in melee."},
	{"core:bless", "Make one friendly stack always roll its maximum creature damage."},
	{"new-horizons:massBless", "Make all eligible friendly stacks always roll their maximum creature damage."},
	{"core:chainLightning", "Strike one stack with lightning that jumps to nearby creatures, including allies, weakening with each jump."},
	{"new-horizons:masterChainLightning", "Strike up to four stacks with lightning that jumps between nearby creatures, including allies, weakening with each jump."},
	{"new-horizons:frailty", "Reduce one enemy stack's Creature Defense for the rest of battle; repeated casts strengthen the reduction."},
	{"new-horizons:summonLivingArmor", "Summon temporary Living Armor that resists physical damage but takes extra magical damage."},
	{"new-horizons:focusMagic", "Enchant a friendly shooter so its damaging shots mark enemies, helping all friendly shooters ignore part of their Defense."},
	{"new-horizons:hexOfPain", "Curse one enemy stack to suffer Shadow damage after each attack or retaliation it makes."},
	{"new-horizons:plague", "Afflict one stack with recurring magical damage that spreads to adjacent creatures, including allies."},
	{"new-horizons:soulChain", "Link enemy stacks so damage to the secondary targets also harms the primary target."},
	{"new-horizons:shadowGift", "Sacrifice part of a friendly stack's health for the rest of battle to temporarily add Shadow damage to its attacks."},
	{"new-horizons:vampirism", "Let one friendly stack heal surviving creatures through damage from its attacks and retaliations; fallen creatures stay dead."},
	{"new-horizons:reanimate", "Heal one friendly stack and temporarily restore fallen creatures from usable remains; restored creatures disappear after battle."},
	{"new-horizons:soulReaper", "Deal Shadow damage that increases with the enemy stack's missing health, then finish survivors left near death."},
	{"new-horizons:doom", "Reduce one enemy stack's damage, retaliation damage, Initiative, movement and Morale."},
	{"new-horizons:shieldOfChaos", "Protect one stack against physical and magical damage while greatly reducing its Morale and Luck."},
	{"new-horizons:handOfFate", "Damage one enemy stack, then strike another random surviving stack, friend or foe, based on the first target's actual health lost."},
	{"new-horizons:holyWrath", "Deal Holy damage to one enemy stack, with extra damage against Undead and Inferno creatures."},
	{"new-horizons:holyArmor", "Reduce magical damage taken by one friendly stack; non-damaging spells can still affect it."},
	{"new-horizons:heavenlyGale", "Protect your army against physical ranged projectiles, including siege shots; melee attacks and spells remain effective."},
	{"new-horizons:divineRetribution", "Protect one friendly stack by punishing creatures that damage it with Holy damage at round end."},
	{"new-horizons:entangle", "Root one enemy ground stack, preventing movement while allowing attacks and retaliation; displacement breaks the roots."},
	{"new-horizons:summonTrolls", "Summon temporary Trolls on an empty hex; they retain their natural regeneration and disappear after combat."},
	{"new-horizons:elementalConvergence", "Summon temporary Elementals determined by the battlefield terrain on an empty hex."},
	{"new-horizons:naturesWrath", "Chain through nearby stacks, damaging enemies and healing surviving allies; each jump weakens the effect."},
	{"new-horizons:pandemonium", "Damage every battlefield stack, including allies, according to how many distinct debuffs it has."},
	{"new-horizons:realityWarp", "Exchange transferable temporary magical buffs and debuffs between one friendly and one enemy stack, preserving their strength and remaining duration."},
	{"new-horizons:polymorph", "Temporarily transform one enemy stack into a random same-tier creature, preserving its current total health."},
	{"new-horizons:blink", "Teleport one friendly or enemy stack to a random nearby legal position without granting another activation."},
	{"new-horizons:confusion", "Make one enemy stack randomly attack an enemy, Defend, or wander during its next activation; it will not attack its allies."},
	{"new-horizons:puppetMaster", "Control one enemy stack for one activation, then grant it temporary immunity to further mental control."},
	{"new-horizons:hydrasVitality", "Temporarily increase one friendly living stack's maximum health and regenerate surviving creatures each activation, without restoring casualties."},
	{"new-horizons:verdantPrison", "Surround one enemy stack with temporary Dendroid Guards on available adjacent hexes."},
	{"new-horizons:vengefulVines", "Damage enemies across three connected hexes and reduce their movement Speed for two rounds; allies are unharmed."},
	{"new-horizons:theChosen", "Empower one friendly living stack with extra attacks, unlimited retaliations, Flying and Jousting; only one stack per army can be Chosen."},
	{"new-horizons:purify", "Remove selected temporary magical debuffs from friendly stacks in an area; this spell does not heal."},
	{"new-horizons:sanctuary", "Protect one friendly stack from deliberate attacks and hostile single-target spells until it moves or acts offensively; area effects still apply."},
	{"new-horizons:guardianSpirit", "Give one friendly stack temporary protective health that absorbs physical creature damage; spells bypass it."},
	{"new-horizons:lifeDrain", "Damage one enemy stack and heal surviving creatures in one friendly stack according to the damage actually dealt."},
	{"new-horizons:stormOfDaggers", "Divide magical damage among one to five selected enemy stacks; selecting more targets increases the total damage pool."},
	{"new-horizons:phantomArmy", "Temporarily copy one friendly stack; the phantom deals normal damage, resists physical damage and is vulnerable to magic."},
	{"new-horizons:regeneration", "Restore part of surviving creatures' recent damage at their next activation for three rounds; fallen creatures stay dead."},
	{"new-horizons:massRegeneration", "Give all eligible friendly living stacks Regeneration for three rounds, healing surviving creatures without restoring casualties."},
	{"core:cure", "Heal one friendly stack and remove physical afflictions; fallen creatures stay dead."},
	{"core:curse", "Make one enemy stack always roll its minimum creature damage."},
	{"new-horizons:massCurse", "Make all eligible enemy stacks always roll their minimum creature damage."},
	{"new-horizons:massSorrow", "Temporarily reduce the Morale of all eligible enemy stacks."},
	{"core:dispel", "Remove all temporary magical buffs and debuffs from one friendly or enemy stack."},
	{"new-horizons:disintegrate", "Damage one enemy stack and destroy the remains of creatures killed, preventing their restoration."},
	{"core:earthquake", "Damage fortifications during a siege, or damage grounded creatures and create movement-hindering fractured ground in field combat."},
	{"core:fireWall", "Create a wall of fire that burns creatures crossing or standing in it, including grounded flyers."},
	{"core:fireball", "Burn every creature in a small area, including allies."},
	{"core:forgetfulness", "Restrict one enemy stack to moving, basic melee attacks, Wait and Defend; its ordinary retaliation remains available."},
	{"core:frostRing", "Damage creatures in a ring around the selected hex, including allies; the center is unharmed."},
	{"core:iceBolt", "Deal cold damage to one enemy stack without changing its Speed or Initiative."},
	{"core:implosion", "Damage one enemy stack based on its current health, then pull nearby creatures one hex closer."},
	{"core:inferno", "Burn creatures in an area, including allies."},
	{"core:landMine", "Place concealed explosive mines that damage enemy creatures stepping on them."},
	{"core:lightningBolt", "Deal lightning damage to one enemy stack."},
	{"core:magicArrow", "Deal magical damage to one enemy stack; Overcharge lets you spend more Mana for a stronger hit."},
	{"core:meteorShower", "Damage creatures in an area, including allies, and damage nearby fortifications."},
	{"core:misfortune", "Prevent one enemy stack's positive Luck from triggering and reduce the chance of its favorable random abilities."},
	{"new-horizons:poison", "Poison one living enemy stack with increasing physical damage over its next three activations; Cure removes it, Dispel does not."},
	{"core:quicksand", "Place concealed patches that stop walking creatures when entered; flying creatures ignore them."},
	{"new-horizons:transfigureMatter", "Turn one ordinary physical obstacle into temporary Diamond Golems."},
	{"core:resurrection", "Heal one friendly stack and permanently restore fallen creatures, up to its battle-start capacity."},
	{"core:slow", "Temporarily reduce one enemy stack's Speed and Initiative."},
	{"new-horizons:massSlow", "Temporarily reduce every eligible enemy stack's Speed and Initiative with a weaker version of Slow."},
	{"core:sorrow", "Temporarily reduce one enemy stack's Morale."},
	{"core:teleport", "Move one friendly stack to a selected legal hex, ignoring intervening barriers without granting another activation."},
	{"new-horizons:spellLock", "Seal one stack against magic; remove allied debuffs or enemy buffs, and pause remaining magical effects' timers."},
	{"new-horizons:timeStop", "Freeze an area until your next Hero Action; affected creatures cannot act or receive damage, healing or new effects, and their timers pause."},
	{"core:summonBoat", "Bring an available boat to an adjacent legal water tile; uses your one Adventure Spell for the day."},
	{"core:waterWalk", "Walk across water at increased movement cost until day end; finish on land and cast no other Adventure Spell that day."},
	{"core:townPortal", "Teleport to your nearest controlled town and end today's movement; uses your one Adventure Spell for the day."},
	{"core:fly", "Fly across terrain at increased movement cost until day end; protected barriers remain impassable, and you must land legally."},
	{"core:dimensionDoor", "Teleport a short distance to a visible legal tile and end today's movement; protected barriers remain impassable."}
};
}

std::string spellSummaryForHero(const CGHeroInstance * hero, const spells::Spell * spell, int schoolLevel)
{
	if(!spell)
		return {};
	if(hero && rulesActive(hero->getMagicRules())
		&& (spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId())
			|| isAdventureSpell(hero->getMagicRules(), spell->getId())))
	{
		for(const auto & [identity, summary] : spellSummaries)
			if(identity == spell->getJsonKey())
				return std::string(summary);
	}
	return spellDescriptionForHero(hero, spell, schoolLevel);
}
}
