/*
 * NewHorizonsMagicReadouts.cpp, part of VCMI engine
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
#include "NewHorizonsBlessDuration.h"
#include "NewHorizonsCureHealing.h"
#include "NewHorizonsTheChosen.h"
#include "../entities/hero/NewHorizonsChosenSpecialties.h"
#include "../entities/hero/CHero.h"
#include "../entities/hero/NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsSorceryRevision.h"
#include "CSpell.h"
#include "ISpellMechanics.h"
#include "../bonuses/BonusEnum.h"
#include "../mapObjects/CGHeroInstance.h"

#include <array>

namespace newHorizonsMagic::presentation
{
using detail::registeredSpellcraftRank;
using detail::NATURE_MIRE_SHAPER;

namespace
{
void describeResurrection(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && resurrectionRestorationEnabled(hero->getMagicRules(), spell->getId()))
	{
		const auto & rules = hero->getMagicRules();
		result = "Heal one friendly stack and permanently restore its fallen creatures, up to its battle-start capacity. "
			"The wounded surviving creature is healed first. Summoned, cloned, and Phantom stacks are ineligible.\n\n"
			"Base restoration pool: " + std::to_string(RESURRECTION_BASE_POOL_HP)
			+ " + " + std::to_string(RESURRECTION_SPELL_POWER_HP_PER_POINT)
			+ " HP per Spell Power. School and Spellcraft strengthen only the Spell Power part ("
			+ percentFromBasisPoints(spellPowerCoefficientBasisPoints(rules, hero, spell->getId()))
			+ " of normal strength). Empower and battle-only Warcasting may further strengthen that part; "
			"other spell bonuses may modify the resulting pool.";
		if(hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.miracleWorker"))
			result += " Miracle Worker adds 25% to the HP available for restoring casualties, rounded down after reserving the healing for the wounded survivor. Wound healing is unchanged.";
	}
}

void describeQuicksand(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getId() == SpellID(SpellID::QUICKSAND)
		&& rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		const auto & rules = hero->getMagicRules();
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const auto patchCount = quicksandPatchCount(rules, hero, spell->getId(),
			std::max<int32_t>(0, hero->getEffectPower(spell)),
			std::max<int32_t>(1, hero->getEffectPowerDivisor(spell)), 0, empowerBonusPercent);
		if(patchCount)
		{
			const int32_t powerStep = detail::quicksandSpellPowerStep(rules,
				std::max<int32_t>(1, hero->getEffectPowerDivisor(spell)));
			result = "Places " + std::to_string(*patchCount)
				+ " concealed Quicksand patches on legal empty ground hexes. Walking creatures stop when entering a patch; flying creatures ignore it.\n\nBase count: min(5, "
				+ "2 + floor(displayed Spell Power / " + std::to_string(powerStep)
				+ ")). Nature rank, Spellcraft, and Empower strengthen only the Spell Power part. "
				+ "The displayed count includes those bonuses, but battle-only Warcasting may add patches.";
			if(hero->hasActivePerk(std::string(NATURE_MAGIC_SKILL), std::string(NATURE_MIRE_SHAPER)))
				result += " Mire Shaper adds one patch after the ordinary five-patch cap, allowing up to six patches.";
		}
	}
}

void describeTimeStop(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && rulesActive(hero->getMagicRules())
		&& spell->getJsonKey() == newHorizonsSorcery::TIME_STOP_SPELL)
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const int32_t spellPower = std::max<int32_t>(0, hero->getEffectPower(spell));
		const bool chronomancer = hero->hasActivePerk(
			newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::CHRONOMANCER_PERK);
		const int radiusCap = newHorizonsSorcery::TIME_STOP_BASE_MAX_RADIUS
			+ (chronomancer ? newHorizonsSorcery::TIME_STOP_CHRONOMANCER_RADIUS_BONUS : 0);
		const int radius = newHorizonsSorcery::timeStopRadius(spellPower,
			coefficientBasisPoints, 0, empowerBonusPercent, chronomancer);
		const bool schoolRankRules = rules["rulesetVersion"].Integer()
			>= SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;

		result = "Stop time in the selected area until the beginning of your next Hero Action. Creatures in stasis cannot move, "
			"act, retaliate, take damage, receive healing, teleport, or receive new effects. Their existing effect timers "
			"are paused, and they still block their occupied hexes.";
		if(schoolRankRules)
		{
			result += "\n\nRadius before battle-only Warcasting (hexes): " + std::to_string(radius) + " at Spell Power "
				+ std::to_string(spellPower) + ". Sorcery and Spellcraft strengthen only the Spell Power part ("
				+ percentFromBasisPoints(coefficientBasisPoints) + " of normal strength).";
		}
		else
		{
			result += "\n\nRadius before battle-only Warcasting (hexes): " + std::to_string(radius) + " at Spell Power "
				+ std::to_string(spellPower) + ". School rank does not increase this radius.";
		}
		result += " Maximum radius: " + std::to_string(radiusCap)
			+ " hexes. Chronomancer raises the maximum to 3 without adding a free radius step. "
			"Battle-only Warcasting may increase the radius through Spell Power. It does not extend the duration "
			"or change which targets can be affected.";
	}
}

void describeCure(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && cureEnabled(hero->getMagicRules(), spell->getId()))
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		int64_t coefficientTenThousandths = 15'000LL * coefficientBasisPoints
			* (100 + empowerBonusPercent) / (SPELL_POWER_COEFFICIENT_BASIS_POINTS * 100LL);
		if(hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"))
			coefficientTenThousandths = coefficientTenThousandths * 120 / 100;
		const std::string spellPowerCoefficient = fixedPointFromScaledValue(coefficientTenThousandths, 4);
		result = "Heal one friendly living stack without restoring dead creatures. If the target has Poison or Disease, choose one physical "
			"affliction to remove. Magical effects remain.\n\nBase healing: 25 + "
			+ spellPowerCoefficient + " \u00d7 Spell Power HP.";
		if(newHorizonsHeroes::fieldPhysicianHealingPercent(hero) > 0)
		{
			const auto healing = hero->getSpellBonus(spell, cureHealingBudget(hero, std::max<int32_t>(0, hero->getEffectPower(spell)),
				coefficientBasisPoints, 0, empowerBonusPercent), nullptr);
			result = "Heal one friendly living stack without restoring dead creatures. If the target has Poison or Disease, choose one physical "
				"affliction to remove. Magical effects remain.\n\nOrdinary healing budget: " + std::to_string(healing)
				+ " HP before battle-only and target-specific bonuses. Field Physician is included in both the fixed healing "
				"and the Spell Power part. Healer, when present, strengthens only the Spell Power part.";
		}
	}
}

void describeBlessDuration(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spellVariantBase(hero->getMagicRules(), spell->getId()) == SpellID::BLESS)
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const int64_t power = std::max<int32_t>(0, hero->getEffectPower(spell));
		const int specialtyPercent = hero->getNonDamageSpellSpecialtyBonusPercent(SpellID(SpellID::BLESS));
		const int ordinaryDuration = blessOrdinaryDuration(power, coefficientBasisPoints, 0,
			empowerBonusPercent, specialtyPercent);
		const int64_t spellDurationModifier = static_cast<int64_t>(hero->valOfBonuses(
			BonusType::SPELL_DURATION, BonusSubtypeID()))
			+ hero->valOfBonuses(BonusType::SPELL_DURATION, BonusSubtypeID(SpellID(SpellID::BLESS)))
			+ spellDurationBonus(hero, SpellID::BLESS);
		const bool benediction = hasBenedictionPerk(hero);
		const int totalDuration = blessCasterDuration(*hero, ordinaryDuration, false);
		result += "\n\nCurrent ordinary duration: " + std::to_string(ordinaryDuration)
			+ (ordinaryDuration == 1 ? " round" : " rounds") + " (combined Spell Power coefficient: "
			+ percentFromBasisPoints(coefficientBasisPoints) + "). Current total before battle-only adjustments: "
			+ std::to_string(totalDuration) + (totalDuration == 1 ? " round." : " rounds.");
		if(spellDurationModifier != 0)
			result += " SPELL_DURATION modifier: " + std::to_string(spellDurationModifier) + ".";
		if(benediction)
			result += " Benediction adds 1 round.";
	}
}

void describeChainRetention(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && rulesActive(hero->getMagicRules())
		&& spell->getJsonKey() == "new-horizons:masterChainLightning")
	{
		// Keep the translated base description complete for hero-independent help
		// surfaces, then add the live value only where a hero context exists.
		result += "\n\nCurrent retention: "
			+ std::to_string(masterChainLightningRetentionPercent(hero->level)) + "%.";
	}
}

void describeSpellLock(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() >= SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& spell->getJsonKey() == newHorizonsSorcery::SPELL_LOCK_SPELL)
	{
		const auto & rules = hero->getMagicRules();
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(rules, hero, spell->getId());
		const int empowerBonusPercent = empowerSpellBonusPercent(rules, hero, spell->getId());
		const bool spellbinder = hero->hasActivePerk(
			newHorizonsSorcery::SORCERY_MAGIC_SKILL, newHorizonsSorcery::SPELLBINDER_PERK);
		const int duration = newHorizonsSorcery::spellLockDurationBasisPoints(
			hero->getEffectPower(spell), spellbinder, coefficientBasisPoints, 0, empowerBonusPercent);
		result += "\n\nCurrent combined Spell Power coefficient: "
			+ percentFromBasisPoints(coefficientBasisPoints)
			+ (empowerBonusPercent > 0 ? " with Empower Spell +25%; ordinary duration at current Spell Power: "
				: "; ordinary duration at current Spell Power: ") + std::to_string(duration)
			+ (duration == 1 ? " round." : " rounds.");
	}
}

void describeSpellcraft(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() == SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& hero->getMagicRules().Struct().contains("spellcraftEfficiencyPercent")
		&& registeredSpellcraftRank(hero) > MasteryLevel::NONE
		&& spell->getJsonKey() != newHorizonsSorcery::FOCUS_MAGIC_SPELL
		&& spell->isCommonHeroSpell() && spellAllowedBySavedRoster(hero->getMagicRules(), spell->getId())
		&& !isAdventureSpell(hero->getMagicRules(), spell->getId())
		&& !spellSchoolSkills(hero->getMagicRules(), spell->getId()).empty())
	{
		const int efficiencyPercent = spellcraftEfficiencyPercent(hero->getMagicRules(), registeredSpellcraftRank(hero));
		result += "\n\nSpellcraft: " + std::to_string(efficiencyPercent)
			+ "% of normal strength from Spell Power, applied after School rank.";
		const int ordinary = spellPowerCoefficientBasisPoints(hero->getMagicRules(), hero, spell->getId());
		const int direct = directDamageSpellPowerCoefficientBasisPoints(hero->getMagicRules(), hero, spell->getId());
		if(direct != ordinary)
			result += " Your specialty changes damage from Spell Power to "
				+ percentFromBasisPoints(direct)
				+ " of normal strength after School and Spellcraft. Other effects keep their usual strength.";
	}
}

void appendCapturedReadouts(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell,
	const JsonNode & savedMagicRules, int coefficientBasisPoints, bool hasSavedSchoolRankCoefficient,
	bool phantomCoefficient)
{
	const auto rawPower = std::max(0, hero->getEffectPower(spell));
	if(phantomCoefficient)
		result += "\nCurrent ordinary Integrity per 10,000 source current HP: "
			+ std::to_string(newHorizonsSorcery::phantomArmyIntegrityWithModifiers(10000, rawPower,
				hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist"),
				coefficientBasisPoints, 0, empowerSpellBonusPercent(savedMagicRules, hero, spell->getId()),
				hero->getNonDamageSpellSpecialtyBonusPercent(spell->getId())))
			+ " (before battle-only Warcasting; real HP floors only after the capped fraction and Illusionist).";
	if(spell->getJsonKey() == "new-horizons:hexOfPain")
		result += "\nCurrent ordinary captured flat damage: "
			+ std::to_string(hexOfPainFlatDamage(rawPower, damageSpellPowerCoefficientBasisPoints(savedMagicRules, hero, spell->getId()), 0,
				empowerSpellBonusPercent(savedMagicRules, hero, spell->getId()),
				hero->getDamageSpellSpecialtyBonusPercent(spell->getId()),
				hasSavedSchoolRankCoefficient && hero->hasActivePerk("new-horizons:shadowMagic", "new-horizons:shadowMagic.painweaver")))
			+ " (before battle-only Warcasting). Each trigger adds the unchanged 10% damage share.";
	if(spell->getJsonKey() == "new-horizons:plague")
		result += "\nCurrent captured raw tick damage: "
			+ std::to_string(plagueTickDamage(rawPower, damageSpellPowerCoefficientBasisPoints(savedMagicRules, hero, spell->getId()),
				hero->getDamageSpellSpecialtyBonusPercent(spell->getId())))
			+ ". Later ticks and child infections reuse this snapshot.";
	if(phantomCoefficient && hero->getNonDamageSpellSpecialtyBonusPercent(spell->getId()) > 0)
		result += "\nAstral specialty: +20% to the Spell Power-derived Integrity fraction before the 40% base cap; "
			"the fixed 20%, Illusionist multiplier and duration are unchanged.";
	if((spell->getJsonKey() == "new-horizons:hexOfPain" || spell->getJsonKey() == "new-horizons:plague")
		&& hero->getDamageSpellSpecialtyBonusPercent(spell->getId()) > 0)
		result += "\nSpecialty: +15% to the initial Spell Power-derived flat damage only; "
			"the fixed term and subsequent damage-share/spread snapshot are unchanged.";
}

void appendFocusReadout(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell,
	bool hasSavedSchoolRankCoefficient, int coefficientBasisPoints)
{
	const int64_t spellPower = std::max<int32_t>(0, hero->getEffectPower(spell));
	const auto profile = newHorizonsSorceryRevision::focusRules(hero->getMagicRules()).value_or(
		newHorizonsSorceryRevision::FocusRules{newHorizonsSorcery::ARCANE_BREACH_BASE_BASIS_POINTS,
			newHorizonsSorcery::ARCANE_BREACH_POWER_BASIS_POINTS, newHorizonsSorcery::ARCANE_BREACH_CAP_BASIS_POINTS});
	const int specialty = hasSavedSchoolRankCoefficient
		? hero->getNonDamageSpellSpecialtyBonusPercent(spell->getId()) : 0;
	const int penetrationBasisPoints = newHorizonsSorceryRevision::focusMarkBasisPoints(
		profile, static_cast<int32_t>(spellPower), coefficientBasisPoints, 0,
		empowerSpellBonusPercent(hero->getMagicRules(), hero, spell->getId()), specialty);
	result += "\n\nYour hero: each mark lets friendly ranged attacks ignore "
		+ percentFromBasisPoints(penetrationBasisPoints) + " Creature Defense; 3 such marks: "
		+ percentFromBasisPoints(3 * penetrationBasisPoints) + ". Battle bonuses may increase this.";
}

void appendPowerCoefficientReadout(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(!hero || !rulesActive(hero->getMagicRules()))
		return;
	const auto & savedMagicRules = hero->getMagicRules();
	const bool focusMagicCoefficient = spell->getJsonKey() == newHorizonsSorcery::FOCUS_MAGIC_SPELL;
	const bool hasSavedSchoolRankCoefficient = savedMagicRules["rulesetVersion"].Integer()
		>= SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
	if((!hasSavedSchoolRankCoefficient && !focusMagicCoefficient)
		|| spellSchoolSkills(savedMagicRules, spell->getId()).empty())
		return;

	const int coefficientPercent = spellPowerCoefficientPercent(savedMagicRules, hero, spell->getId());
	const int coefficientBasisPoints = spellPowerCoefficientBasisPoints(savedMagicRules, hero, spell->getId());
	const bool cureCoefficient = cureEnabled(hero->getMagicRules(), spell->getId());
	const bool transfigureCoefficient = spell->getJsonKey() == "new-horizons:transfigureMatter";
	const bool phantomCoefficient = spell->getJsonKey() == "new-horizons:phantomArmy";
	appendCapturedReadouts(result, hero, spell, savedMagicRules, coefficientBasisPoints,
		hasSavedSchoolRankCoefficient, phantomCoefficient);
	const auto formula = spellDirectDamage(hero->getMagicRules(), spell->getJsonKey());
	const bool damageCoefficient = spell->isDamage()
		&& (spell->getBasePower() != 0 || (formula && formula->powerCoefficient != 0));
	if(!focusMagicCoefficient && !cureCoefficient && !transfigureCoefficient && !phantomCoefficient && !damageCoefficient)
		return;

	constexpr std::array<std::string_view, 4> rankNames{
		"No School Rank", "Basic School", "Advanced School", "Expert School"};
	if(focusMagicCoefficient)
	{
		appendFocusReadout(result, hero, spell, hasSavedSchoolRankCoefficient,
			coefficientBasisPoints);
		return;
	}

	const auto rankIndex = detail::schoolCoefficientRankIndex(coefficientPercent);
	if(!rankIndex)
		return;
	result += "\n\n" + std::string(rankNames[*rankIndex]) + ": "
		+ std::to_string(coefficientPercent)
		+ (transfigureCoefficient ? "% of normal golem HP from Spell Power."
			: phantomCoefficient ? "% of normal Phantom Integrity from Spell Power."
			: cureCoefficient ? "% of normal healing from Spell Power."
			: "% of normal damage from Spell Power.");
	return;
}
}

void applyContextualOverrides(std::string & result, const CGHeroInstance * hero, const spells::Spell * spell)
{
	if(hero && spell->getJsonKey() == newHorizonsTheChosen::SPELL_KEY)
	{
		if(const auto rules = newHorizonsTheChosen::rulesFor(hero->getMagicRules()))
		{
			const int schoolPercent = spellPowerCoefficientPercent(hero->getMagicRules(), hero, spell->getId());
			const int nominal = newHorizonsTheChosen::nominalDuration(*rules,
				hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), schoolPercent);
			const int exalted = newHorizonsTheChosen::exaltedChampionDurationBonus(*rules, hero);
			const int specialty = newHorizonsHeroes::chosenSpecialtyDurationBonus(
				hero->getPrimaryGrowthRules(), hero->getHeroType()->getJsonKey(), spell->getId());
			const int duration = nominal + exalted + specialty;
			result += "\n\nCurrent duration: " + std::to_string(duration)
				+ " rounds (capped ordinary duration " + std::to_string(nominal)
				+ "; Exalted Champion +" + std::to_string(exalted) + "; Inteus +" + std::to_string(specialty)
				+ "). Battle-only Extend Spell and Echoed Duration may add rounds after the ordinary cap. "
				"Only one stack per army; a successful recast replaces the previous recipient.";
		}
		return;
	}
	describeResurrection(result, hero, spell);
	describeQuicksand(result, hero, spell);
	describeTimeStop(result, hero, spell);
	describeCure(result, hero, spell);
	describeBlessDuration(result, hero, spell);
	describeChainRetention(result, hero, spell);
	describeSpellLock(result, hero, spell);
	describeSpellcraft(result, hero, spell);
	appendPowerCoefficientReadout(result, hero, spell);
}
}
