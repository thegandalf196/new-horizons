/*
 * NewHorizonsSpecialtyPresentation.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsSpecialtyArtwork.h"
#include "HeroStartingProjection.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../CSkillHandler.h"
#include "../../texts/CGeneralTextHandler.h"
#include "../../texts/MetaString.h"
#include "../../spells/CSpell.h"
#include "../../spells/NewHorizonsMagic.h"
#include "../../GameLibrary.h"

namespace newHorizonsHeroes
{
namespace
{
bool resolveIndividualPresentation(const CHero & hero,
	const JsonNode & development, const JsonNode & magic, const CGHeroInstance * capturedHero,
	SpecialtyPresentation & result)
{
	if(resolveAdditionalSpecialtyPresentation(hero, development, magic, capturedHero, result)
		|| resolveIndividualRiderPresentation(hero, development, magic, capturedHero, result)
		|| resolveSchoolSpellcraftPresentation(hero, development, magic, capturedHero, result))
		return true;
	const CHero * heroType = &hero;
	if(const auto ward = masterSpellwardSpecialtyRules(development);
		ward && heroType->getJsonKey() == "core:thorgrim"
		&& newHorizonsMagic::rulesActive(magic) && magic["spells"].isStruct()
		&& magic["spells"].Struct().contains("new-horizons:holyArmor"))
	{
		result.name = "Master Spellward";
		result.artwork = "newHorizons/heroSpecialties/ThorgrimMasterSpellward";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:warcasting"));
		result.description = "While Spellward is active, its magical-damage reduction contribution increases by "
			+ std::to_string(ward->percentPerLevel) + "% per hero level, up to +"
			+ std::to_string(ward->maximumExtraPercent)
			+ "% of its ordinary value. This strengthens one Spellward source, not spell resistance."
			+ " Other magical defenses and the ordinary total reduction cap still apply.";
		return true;
	}
	if(const auto bottomless = bottomlessMindSpecialtyRules(development);
		bottomless && heroType->getJsonKey() == "core:andra"
		&& newHorizonsMagic::spellPointRulesActive(magic))
	{
		result.name = "Bottomless Mind";
		result.artwork = "newHorizons/heroSpecialties/AndraBottomlessMind";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
		result.description = "While Intelligence is active, Andra gains +"
			+ std::to_string(bottomless->pointsPerLevel)
			+ " maximum Normal Spell Points per hero level, added after other capacity bonuses."
			+ " This does not refill Spell Points or grant Buffer.";
		if(capturedHero)
			result.description += " Current specialty bonus: +"
				+ std::to_string(capturedHero->getBottomlessMindManaBonus()) + " maximum Normal Spell Points.";
		return true;
	}

	const std::string fortuneMarker = "new-horizons:fortune-child:" + std::to_string(heroType->getId().getNum());
	if(const auto preparation = arcanePreparationSpecialtyRules(development);
		preparation && heroType->getJsonKey() == "core:serena"
		&& newHorizonsMagic::spellPointRulesActive(magic))
	{
		result.name = "Arcane Preparation";
		result.artwork = "newHorizons/heroSpecialties/SerenaArcanePreparation";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
		result.description = "At combat start, Intelligence grants temporary Buffer equal to "
			+ std::to_string(preparation->initialPercent) + "% + "
			+ std::to_string(preparation->percentPerLevel) + "% per hero level of Intelligence's extra maximum Spell Points"
			+ " (up to " + std::to_string(preparation->maximumPercent) + "%, rounded down; minimum 1 when positive)."
			+ " Unused temporary Buffer expires after combat. Normal capacity and Spell Points are unchanged.";
		if(capturedHero)
			result.description += " Current: " + std::to_string(capturedHero->getArcanePreparationPercent())
				+ "%, +" + std::to_string(capturedHero->getArcanePreparationBufferBonus()) + " temporary Buffer.";
		return true;
	}
	if(const auto fortune = fortuneChildSpecialtyRules(development);
		fortune && heroType->getJsonKey() == "core:daremyth"
		&& std::ranges::any_of(heroType->spellSpecialtySuccessorProducers, [](const auto & producer)
			{ return producer.source == SpellID::FORTUNE; })
		&& (!capturedHero || std::ranges::any_of(capturedHero->getExportedBonusList(), [&fortuneMarker](const auto & bonus)
			{ return bonus && bonus->type == BonusType::LUCK && bonus->stacking == fortuneMarker; })))
	{
		result.name = "Fortune's Child";
		result.artwork = "newHorizons/heroSpecialties/DaremythFortunesChild";
		result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:luck"));
		result.description = "Daremyth grants +" + std::to_string(fortune->luckPerStep)
			+ " Luck for every " + std::to_string(fortune->levelStep)
			+ " hero levels. This adds to ordinary Luck and follows its existing limits.";
		if(capturedHero)
			result.description += " Current specialty bonus: +"
				+ std::to_string(capturedHero->getFortuneChildLuckBonus()) + " Luck.";
		return true;
	}

	if(const auto druids = druidSpecialtyRules(development))
	{
		const auto identity = heroType->getJsonKey();
		const LevelScaledSpecialty * scaling = nullptr;
		MetaString description;
		if(identity == "core:elleshar")
		{
			result.name = "High Lore";
			result.artwork = "newHorizons/heroSpecialties/EllesharHighLore";
			result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:wisdom"));
			scaling = &druids->highLore;
			description.appendRawString("The extra maximum Spell Points granted by Intelligence increase by ");
		}
		else if(identity == "core:gem")
		{
			result.name = "Restorative Sorceress";
			result.artwork = "newHorizons/heroSpecialties/GemRestorativeSorceress";
			result.spell = SpellID::CURE;
			scaling = &druids->restorativeSorceress;
			description.appendRawString("The Spell Power healing component of Cure and creature healing by the First Aid Tent increase by ");
		}
		else if(identity == "core:malcom")
		{
			result.name = "Natural Philosopher";
			result.artwork = "newHorizons/heroSpecialties/MalcomNaturalPhilosopher";
			result.skill = SecondarySkill(SecondarySkill::decode("new-horizons:learning"));
			scaling = &druids->naturalPhilosopher;
			description.appendRawString("The core experience bonus from Learning increases by ");
		}
		if(scaling)
		{
			description.appendNumber(scaling->percentPerLevel);
			description.appendRawString("% per hero level, up to ");
			description.appendNumber(scaling->maximumPercent);
			description.appendRawString("%. Fractions round down.");
			if(identity == "core:malcom")
			{
				description.appendRawString(" Learning Stones and Trees of Knowledge grant ");
				description.appendNumber(druids->knowledgeObjectExperiencePercent);
				description.appendRawString("% additional raw experience before ordinary experience modifiers.");
			}
			result.description = description.toString(LIBRARY->generaltexth.get());
			return true;
		}
	}

	return false;
}

bool resolveCreatureLinePresentation(const CHero & hero, const JsonNode & development,
	std::optional<CreatureID> target, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	const CHero * heroType = &hero;
	const auto rules = newHorizonsHeroes::creatureLineSpecialtyRules(development);
	const BonusList empty;
	const auto & localBonuses = capturedHero ? capturedHero->getExportedBonusList() : empty;
	if(rules && heroType->creatureLineSpecialtyAlias)
	{
		const CreatureID creature = target.value_or(heroType->creatureLineSpecialtyAlias->creature);
		const std::array<std::string, 4> markers = {
			"new-horizons:creature-line-specialty:" + std::to_string(heroType->getId().getNum()) + ":"
				+ std::to_string(creature.getNum()) + ":" + "speed",
			"new-horizons:creature-line-specialty:" + std::to_string(heroType->getId().getNum()) + ":"
				+ std::to_string(creature.getNum()) + ":" + "initiative",
			"new-horizons:creature-line-specialty:" + std::to_string(heroType->getId().getNum()) + ":"
				+ std::to_string(creature.getNum()) + ":" + "attack",
			"new-horizons:creature-line-specialty:" + std::to_string(heroType->getId().getNum()) + ":"
				+ std::to_string(creature.getNum()) + ":" + "defense"
		};
		const bool converted = !capturedHero || std::ranges::any_of(localBonuses, [&markers](const auto & bonus)
		{
			return bonus && std::ranges::find(markers, bonus->stacking) != markers.end();
		});
		if(converted)
		{
			MetaString description;
			description.appendNamePlural(creature);
			description.appendRawString(" and their upgrades gain +");
			description.appendNumber(rules->speed);
			description.appendRawString(" Speed and +");
			description.appendNumber(rules->initiative);
			description.appendRawString(" Initiative. They also gain +");
			description.appendNumber(rules->attributePerStep);
			description.appendRawString(" Creature Attack and +");
			description.appendNumber(rules->attributePerStep);
			description.appendRawString(" Creature Defense per ");
			description.appendNumber(rules->levelStep);
			description.appendRawString(" hero levels, up to +");
			description.appendNumber(rules->attributeMaximum);
			description.appendRawString(" at level ");
			description.appendNumber(rules->levelStep * rules->attributeMaximum);
			description.appendRawString(".");
			result.creature = creature;
			result.artwork = detail::resolvedSpecialtyArtwork(heroType->getJsonKey(),
				detail::SpecialtyArtworkFamily::Creature, creature.toCreature()->getJsonKey());
			if(creature != heroType->creatureLineSpecialtyAlias->creature)
				result.name = creature.toCreature()->getNamePluralTranslated();
			result.description = description.toString(LIBRARY->generaltexth.get());
			return true;
		}
	}

	return false;
}
}

SpecialtyPresentation resolveSpecialtyPresentation(const CHero & hero,
	const JsonNode & development, const JsonNode & magic, std::optional<CreatureID> target,
	const CGHeroInstance * capturedHero)
{
	const CHero * heroType = &hero;
	SpecialtyPresentation result{hero.getSpecialtyNameTranslated(), hero.getSpecialtyDescriptionTranslated(),
		std::nullopt, std::nullopt, std::nullopt, std::nullopt};
	if(capturedHero && (capturedHero->getHeroType() != heroType
		|| capturedHero->getPrimaryGrowthRules() != development || capturedHero->getMagicRules() != magic))
		throw std::invalid_argument("Specialty presentation requires the matching captured context");

	if(resolveIndividualPresentation(hero, development, magic, capturedHero, result))
		return result;
	if(resolveCreatureLinePresentation(hero, development, target, capturedHero, result))
		return result;

	for(const auto & producer : heroType->spellSpecialtySuccessorProducers)
		if(const auto target = authoredRemainingSpellReplacement(hero, development, magic, producer.source))
		{
			const auto damage = damageSpellSpecialtyRules(development);
			const auto nonDamage = nonDamageSpellSpecialtyRules(development);
			const int percent = capturedHero
				? (producer.damage ? capturedHero->getDamageSpellSpecialtyBonusPercent(*target)
					: capturedHero->getNonDamageSpellSpecialtyBonusPercent(*target))
				: producer.damage ? (damage ? damage->componentPercent : 0)
					: (nonDamage ? nonDamage->componentPercent : 0);
			if(percent > 0)
			{
				MetaString description;
				description.appendName(*target);
				description.appendRawString(" gains +");
				description.appendNumber(percent);
				description.appendRawString(producer.damage
					? "% to its initial Spell Power-derived damage component. Fixed terms and later triggers are unchanged."
					: "% to its Spell Power-derived duration or Integrity component. Fixed terms and caps are unchanged.");
				result.spell = *target;
				result.artwork = detail::resolvedSpecialtyArtwork(heroType->getJsonKey(),
					detail::SpecialtyArtworkFamily::Spell, target->toSpell()->getJsonKey());
				result.name = target->toSpell()->getNameTranslated();
				result.description = description.toString(LIBRARY->generaltexth.get());
				return result;
			}
		}

	if(const auto damageRules = newHorizonsHeroes::damageSpellSpecialtyRules(development))
		for(const auto & producer : heroType->damageSpellSpecialtyProducers)
			if(const auto target = authoredDamageSpellReplacement(hero, development, magic, producer.spell).value_or(producer.spell);
				(capturedHero ? capturedHero->getDamageSpellSpecialtyBonusPercent(target) > 0
					: producer.supported && (producer.spell != SpellID::SLAYER || target != producer.spell)))
			{
				MetaString description;
				description.appendName(target);
				description.appendRawString(" gains +");
				description.appendNumber(damageRules->componentPercent);
				description.appendRawString("% to its Spell Power-derived damage component.");
				result.spell = target;
				result.artwork = detail::resolvedSpecialtyArtwork(heroType->getJsonKey(),
					detail::SpecialtyArtworkFamily::Spell, target.toSpell()->getJsonKey());
				result.name = target.toSpell()->getNameTranslated();
				result.description = description.toString(LIBRARY->generaltexth.get());
				return result;
			}

	if(const auto rules = newHorizonsHeroes::nonDamageSpellSpecialtyRules(development))
		for(const auto & producer : heroType->nonDamageSpellSpecialtyProducers)
		{
			const auto target = authoredNonDamageSpellReplacement(hero, development, magic, producer.spell).value_or(producer.spell);
			if(capturedHero ? capturedHero->getNonDamageSpellSpecialtyBonusPercent(target) > 0
				: producer.supported && std::ranges::find(rules->spells, target) != rules->spells.end())
			{
				MetaString description;
				description.appendName(target);
				description.appendRawString(" gains +");
				description.appendNumber(rules->componentPercent);
				if(producer.spell == SpellID::BLESS)
					description.appendRawString("% to its Spell Power-derived duration component before its normal duration cap.");
				else if(producer.spell == SpellID::HASTE)
					description.appendRawString("% to its Spell Power-derived duration component. Its Speed bonus is unchanged.");
				else if(target.toSpell()->getJsonKey() == newHorizonsMagic::SHADOW_FRAILTY_SPELL)
					description.appendRawString("% to its Spell Power-derived Creature Defense loss component. Its fixed term and caps are unchanged.");
				else if(target.toSpell()->getJsonKey() == "new-horizons:hydrasVitality")
					description.appendRawString("% to its Spell Power-derived maximum-HP increase. Its fixed term, cap and regeneration rate are unchanged.");
				else if(target.toSpell()->getJsonKey() == "new-horizons:guardianSpirit")
					description.appendRawString("% to its Spell Power-derived protective HP component. Its fixed term and duration are unchanged.");
				else if(target.toSpell()->getJsonKey() == "new-horizons:crusade")
					description.appendRawString("% to its Spell Power-derived Attack, Defense, Initiative and Magical Damage Reduction components. Its fixed terms, caps and duration are unchanged.");
				else
					description.appendRawString("% to its Spell Power-derived healing component.");
				result.spell = target;
				result.artwork = detail::resolvedSpecialtyArtwork(heroType->getJsonKey(),
					detail::SpecialtyArtworkFamily::Spell, target.toSpell()->getJsonKey());
				result.name = target.toSpell()->getNameTranslated();
				result.description = description.toString(LIBRARY->generaltexth.get());
				return result;
			}
		}

	if(const auto rules = newHorizonsHeroes::skillSpecialtyRules(development))
		if(heroType->navigationSpecialtyProducer
			&& usesNavigationStartReplacement(development)
			&& (capturedHero ? capturedHero->getSkillSpecialtyCoreBonusPercent(SecondarySkill(SecondarySkill::LOGISTICS)) > 0
				: std::ranges::find(rules->skills, SecondarySkill(SecondarySkill::LOGISTICS)) != rules->skills.end()))
		{
			result.skill = SecondarySkill(SecondarySkill::LOGISTICS);
			result.artwork = detail::resolvedSpecialtyArtwork(heroType->getJsonKey(),
				detail::SpecialtyArtworkFamily::Skill, result.skill->toEntity(LIBRARY)->getJsonKey());
			result.name = result.skill->toEntity(LIBRARY)->getNameTranslated();
			result.description = "Logistics's core Movement effect is increased by +" + std::to_string(rules->coreBonusPercent)
				+ "%. Navigation's sea bonus and embark cost, and other perks, are unchanged.";
			return result;
		}

	if(const auto rules = newHorizonsHeroes::skillSpecialtyRules(development))
		if(heroType->secondarySkillSpecialtyAlias
			&& (capturedHero ? capturedHero->getSkillSpecialtyCoreBonusPercent(heroType->secondarySkillSpecialtyAlias->skill) > 0
				: std::ranges::find(rules->skills, heroType->secondarySkillSpecialtyAlias->skill) != rules->skills.end()))
		{
			MetaString description;
			description.appendTextID(heroType->secondarySkillSpecialtyAlias->skill.toEntity(LIBRARY)->getNameTextID());
			description.appendRawString(" core numerical effects are increased by +");
			description.appendNumber(rules->coreBonusPercent);
			description.appendRawString("%. Skill perks are not increased.");
			result.skill = heroType->secondarySkillSpecialtyAlias->skill;
			result.artwork = detail::resolvedSpecialtyArtwork(heroType->getJsonKey(),
				detail::SpecialtyArtworkFamily::Skill, result.skill->toEntity(LIBRARY)->getJsonKey());
			result.name = result.skill->toEntity(LIBRARY)->getNameTranslated();
			result.description = description.toString(LIBRARY->generaltexth.get());
			return result;
		}

	return result;
}

}
