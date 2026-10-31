/*
 * NewHorizonsAdditionalSpecialties.cpp, part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsGenieSage.h"
#include "NewHorizonsAdditionalSpecialties.h"
#include "NewHorizonsOrdinaryCreatureSpecialties.h"
#include "NewHorizonsForestAmbush.h"
#include "NewHorizonsAerialSupremacy.h"
#include "NewHorizonsDreadCavalry.h"
#include "NewHorizonsPetrifyingBlood.h"
#include "NewHorizonsLivingInferno.h"
#include "NewHorizonsParalyzingGaze.h"
#include "NewHorizonsSerpentsEmbrace.h"
#include "NewHorizonsManaLeeches.h"
#include "NewHorizonsHeroRules.h"
#include "NewHorizonsLearningSpecialties.h"
#include "NewHorizonsRuleValidation.h"
#include "CHero.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../spells/NewHorizonsMagic.h"
#include <limits>
#include "../../bonuses/Bonus.h"
#include "../../bonuses/Limiters.h"
#include "../../bonuses/Propagators.h"
#include "../../bonuses/Updaters.h"
#include "../../CCreatureHandler.h"
#include "../../battle/BattleAttackInfo.h"
#include "../../battle/CBattleInfoCallback.h"
#include "../../battle/NewHorizonsCombatSkills.h"
#include "../../battle/Unit.h"
namespace newHorizonsHeroes
{
bool usesConvertedCreatureSpecialty(const CGHeroInstance * hero)
{
	return hero && (creatureLineSpecialtyRules(hero->getPrimaryGrowthRules()).has_value()
		|| replacesGenericCreatureSpecialty(hero));
}

bool replacesGenericCreatureSpecialty(const CGHeroInstance * hero)
{
	return hasOrdinaryCreatureSpecialty(hero) || hasSageOfTheAges(hero) || hasWheelOfFortune(hero)
		|| hasManaLeeches(hero) || hasSerpentsEmbrace(hero)
		|| hasParalyzingGaze(hero) || hasForestAmbush(hero) || hasPetrifyingBlood(hero) || hasLivingInferno(hero) || hasAerialSupremacy(hero) || hasDreadCavalry(hero);
}

void validateAdditionalSpecialtiesSerialization(const JsonNode & development, bool supported)
{
	if(!development.isStruct() || !development.Struct().contains("additionalSpecialties"))
		return;
	if(!supported)
		throw std::runtime_error("Additional individual specialties require the new save format");
	const auto & table = development["additionalSpecialties"];
	detail::fields(table, {"version", "fieldPhysician", "manaFurnace", "graveArchivist", "spiritTeacher", "manaLeeches", "serpentsEmbrace", "paralyzingGaze", "forestAmbush", "aerialSupremacy", "ordinaryCreatureSpecialties"});
	validateOrdinaryCreatureSpecialtiesSerialization(development, true);
	detail::require(detail::integer(table["version"], 1, 1), "additional specialty version");
	validateLearningSpecialtyRows(table);
	if(table.Struct().contains("aerialSupremacy"))
	{
		const auto & row = table["aerialSupremacy"];
		detail::fields(row, {"enabled"});
		detail::require(row["enabled"].isBool(), "Aerial Supremacy enable policy");
	}
	if(table.Struct().contains("paralyzingGaze"))
	{
		const auto & row = table["paralyzingGaze"];
		detail::fields(row, {"enabled"});
		detail::require(row["enabled"].isBool(), "Paralyzing Gaze enable policy");
	}
	if(table.Struct().contains("forestAmbush"))
	{
		const auto & row = table["forestAmbush"];
		detail::fields(row, {"enabled"});
		detail::require(row["enabled"].isBool(), "Forest Ambush enable policy");
	}
	if(table.Struct().contains("serpentsEmbrace"))
	{
		const auto & row = table["serpentsEmbrace"];
		detail::fields(row, {"enabled"});
		detail::require(row["enabled"].isBool(), "Serpent’s Embrace enable policy");
	}
	if(table.Struct().contains("manaLeeches"))
	{
		const auto & row = table["manaLeeches"];
		detail::fields(row, {"manaPerDamagingAttackSequence"});
		detail::require(detail::integer(row["manaPerDamagingAttackSequence"], 0, 1000), "Mana Leeches theft");
	}
	if(table.Struct().contains("manaFurnace"))
	{
		const auto & row = table["manaFurnace"];
		detail::fields(row, {"restorePointsPerLevel", "maximumRestorePoints"});
		detail::require(detail::integer(row["restorePointsPerLevel"], 0, 1000), "furnace points per level");
		detail::require(detail::integer(row["maximumRestorePoints"], 0, 1000000), "furnace maximum restoration");
	}
	if(!table.Struct().contains("fieldPhysician"))
		return;
	const auto & row = table["fieldPhysician"];
	detail::fields(row, {"healingPercentPerLevel", "healingMaximumPercent"});
	detail::require(detail::integer(row["healingPercentPerLevel"], 0, 1000), "physician percentage per level");
	detail::require(detail::integer(row["healingMaximumPercent"], 0, 200), "physician maximum percentage");
}

std::optional<FieldPhysicianRules> fieldPhysicianRules(const JsonNode & development)
{
	if(!usesRules(development) || !development.isStruct()
		|| !development.Struct().contains("additionalSpecialties"))
		return std::nullopt;
	validateAdditionalSpecialtiesSerialization(development, true);
	const auto & table = development["additionalSpecialties"];
	if(!table.Struct().contains("fieldPhysician"))
		return std::nullopt;
	const auto & row = table["fieldPhysician"];
	return FieldPhysicianRules{static_cast<int>(row["healingPercentPerLevel"].Integer()),
		static_cast<int>(row["healingMaximumPercent"].Integer())};
}

int fieldPhysicianHealingPercent(const CGHeroInstance * hero)
{
	if(!hero || !hero->getHeroTypeID().hasValue()
		|| hero->getHeroTypeID() != HeroTypeID(HeroTypeID::decode("core:rion")))
		return 0;
	const auto rule = fieldPhysicianRules(hero->getPrimaryGrowthRules());
	return rule ? cappedSpecialtyContribution(hero->level, rule->healingPercentPerLevel, rule->healingMaximumPercent) : 0;
}

int64_t fieldPhysicianHealing(int64_t ordinary, const CGHeroInstance * hero)
{
	const auto extra = druidSpecialtyExtra(ordinary, fieldPhysicianHealingPercent(hero));
	const auto maximum = std::numeric_limits<int64_t>::max();
	return ordinary > maximum - extra ? maximum : ordinary + extra;
}

bool resolveAdditionalSpecialtyPresentation(const CHero & hero, const JsonNode & development,
	const JsonNode & magic, const CGHeroInstance * capturedHero, SpecialtyPresentation & result)
{
	if(resolveOrdinaryCreatureSpecialtyPresentation(hero, development, result)
		|| resolveLearningSpecialtyPresentation(hero, development, magic, capturedHero, result))
		return true;
	if(hero.getJsonKey() == "core:ayden")
		return resolveManaFurnacePresentation(hero, development, capturedHero, result);
	if(hero.getJsonKey() != "core:rion")
		return false;
	const auto rule = fieldPhysicianRules(development);
	if(!rule)
		return false;
	result.name = "Field Physician";
	result.artwork = "newHorizons/heroSpecialties/RionFieldPhysician";
	const bool cureActive = newHorizonsMagic::cureEnabled(magic, SpellID::CURE);
	result.description = std::string(cureActive ? "Cure and First Aid Tent creature healing increase by "
		: "First Aid Tent creature healing increases by ")
		+ std::to_string(rule->healingPercentPerLevel) + "% per hero level, up to +"
		+ std::to_string(rule->healingMaximumPercent) + "%. ";
	result.description += cureActive ? "This includes Cure's fixed healing. Cure must be learned normally. "
		: "Cure retains its ordinary healing under this game's legacy magic rules. ";
	result.description += "This does not improve repair, remove extra afflictions or grant resurrection. "
		"Battlefield Medic uses the enhanced Tent healing budget under its ordinary rules.";
	if(capturedHero)
		result.description += std::string(cureActive ? " Current healing increase: +" : " Current Tent healing increase: +")
			+ std::to_string(fieldPhysicianHealingPercent(capturedHero)) + "%.";
	return true;
}

std::vector<std::shared_ptr<Bonus>> creatureLineSpecialtyBonuses(const CGHeroInstance & hero,
	const CreatureLineSpecialtyRules & rules, CreatureID creature, const std::array<std::string, 4> & markers)
{
	std::vector<std::shared_ptr<Bonus>> bonuses;
	const auto * heroType = hero.getHeroType();
	const TLimiterPtr lineLimiter = std::make_shared<CCreatureTypeLimiter>(*creature.toCreature(), true);
	auto makeBonus = [heroType, &markers](BonusType type, size_t marker)
	{
		auto bonus = std::make_shared<Bonus>();
		bonus->duration = BonusDuration::PERMANENT;
		bonus->source = BonusSource::HERO_SPECIAL;
		bonus->sid = BonusSourceID(heroType->getId());
		bonus->stacking = markers[marker];
		bonus->type = type;
		bonus->valType = BonusValueType::BASE_NUMBER;
		return bonus;
	};

	auto speed = makeBonus(BonusType::STACKS_SPEED, 0);
	speed->val = rules.speed;
	speed->limiter = lineLimiter;
	bonuses.push_back(speed);

	auto initiative = makeBonus(BonusType::STACKS_INITIATIVE_BASE, 1);
	initiative->val = rules.initiative;
	auto initiativeLimiter = std::make_shared<AllOfLimiter>();
	initiativeLimiter->add(lineLimiter);
	initiativeLimiter->add(std::make_shared<HasAnotherBonusLimiter>(
		BonusType::STACKS_INITIATIVE_BASE, BonusSource::CREATURE_ABILITY));
	initiative->limiter = initiativeLimiter;
	bonuses.push_back(initiative);

	const int attributeBonus = static_cast<int>(std::min<int64_t>(rules.attributeMaximum,
		static_cast<int64_t>(hero.level) / rules.levelStep * rules.attributePerStep));
	for(const auto skill : {PrimarySkill::ATTACK, PrimarySkill::DEFENSE})
	{
		auto attribute = makeBonus(BonusType::PRIMARY_SKILL, skill == PrimarySkill::ATTACK ? 2 : 3);
		attribute->subtype = BonusSubtypeID(skill);
		attribute->val = attributeBonus;
		attribute->targetSourceType = BonusSource::CREATURE_ABILITY;
		attribute->limiter = lineLimiter;
		bonuses.push_back(attribute);
	}
	return bonuses;
}

}
