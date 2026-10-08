/*
 * CSpell.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "CSpell.h"

#include "Problem.h"
#include "SpellSchoolHandler.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsSorcery.h"
#include "MagicalDamageReduction.h"

#include "../CBonusTypeHandler.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusSelector.h"
#include "../GameLibrary.h"
#include "../json/JsonBonus.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../texts/CGeneralTextHandler.h"

#include <vcmi/spells/Caster.h>

#include <limits>

static constexpr std::array LEVEL_NAMES = {"none", "basic", "advanced", "expert"};

///CSpell
CSpell::CSpell():
	id(SpellID::NONE),
	level(0),
	power(0),
	combat(false),
	creatureAbility(false),
	castOnSelf(false),
	castOnlyOnSelf(false),
	castWithoutSkip(false),
	defaultProbability(0),
	rising(false),
	damage(false),
	offensive(false),
	special(true),
	nonMagical(false),
	targetType(spells::AimType::NOTHING)
{
	levels.resize(GameConstants::SPELL_SCHOOL_LEVELS);
}

//must be instantiated in .cpp file for access to complete types of all member fields
CSpell::~CSpell() = default;

bool CSpell::adventureCast(SpellCastEnvironment * env, const AdventureSpellCastParameters & parameters) const
{
	assert(env);

	if(!adventureMechanics)
	{
		env->complain("Invalid adventure spell cast attempt!");
		return false;
	}
	return adventureMechanics->adventureCast(env, parameters);
}

const CSpell::LevelInfo & CSpell::getLevelInfo(const int32_t schoolLevel) const
{
	if(schoolLevel < 0 || schoolLevel >= GameConstants::SPELL_SCHOOL_LEVELS)
	{
		logGlobal->error("CSpell::getLevelInfo: invalid school mastery level %d", schoolLevel);
		return levels.at(MasteryLevel::EXPERT);
	}

	return levels.at(schoolLevel);
}

int64_t CSpell::calculateDamage(const spells::Caster * caster) const
{
	//check if spell really does damage - if not, return 0
	if(!isDamage())
		return 0;
	const int effectLevel = caster->getEffectLevel(this);
	const int effectPower = caster->getEffectPower(this);
	const int divisor = caster->getEffectPowerDivisor(this);
	auto rawDamage = calculateRawEffectValue(effectLevel, effectPower, 1, divisor);
	const auto * hero = caster->getHeroCaster();
	if(hero && newHorizonsMagic::rulesActive(hero->getMagicRules())
		&& hero->getMagicRules()["rulesetVersion"].Integer() >= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
	{
		const int spellPowerCoefficientBasisPoints = newHorizonsMagic::spellPowerCoefficientBasisPoints(
			hero->getMagicRules(), hero, id);
		const int damagePerkBonusPercent = newHorizonsMagic::spellPowerDamagePerkBonusPercent(
			hero->getMagicRules(), hero, this);
		const int coefficientBasisPoints = spellPowerCoefficientBasisPoints
			* (100 + damagePerkBonusPercent) / 100;
		const int damageSpecialtyPercent = hero->getDamageSpellSpecialtyBonusPercent(id);
		const int empowerBonusPercent = newHorizonsMagic::empowerSpellBonusPercent(
			hero->getMagicRules(), hero, id);
		if(const auto formula = newHorizonsMagic::spellDirectDamage(hero->getMagicRules(), getJsonKey()))
			rawDamage = formula->evaluateBasisPoints(effectPower, divisor, coefficientBasisPoints,
				empowerBonusPercent, damageSpecialtyPercent);
		else if(coefficientBasisPoints != 10000 || empowerBonusPercent > 0 || damageSpecialtyPercent > 0)
		{
			const int64_t powerNumerator = static_cast<int64_t>(getBasePower()) * effectPower;
			rawDamage = getLevelPower(effectLevel)
				+ spells::scaleSpellPowerComponentWithCoefficientBasisPoints(
					powerNumerator, divisor, coefficientBasisPoints, 0, empowerBonusPercent,
					damageSpecialtyPercent);
		}
	}

	return applyElementalDamageBonus(caster, caster->getSpellBonus(this, rawDamage, nullptr));
}

int64_t CSpell::applyElementalDamageBonus(const spells::Caster * caster, const int64_t damage) const
{
	if(!caster || !isMagical() || !isDamage() || damageElement == SpellDamageElement::NONE || damage <= 0)
		return damage;

	const int32_t bonusPercent = caster->getElementalSpellDamageBonus(damageElement);
	if(bonusPercent == 0)
		return damage;

	const int64_t multiplier = std::max<int64_t>(0, 100 + static_cast<int64_t>(bonusPercent));
	if(multiplier == 0)
		return 0;

	const int64_t wholeDamage = damage / 100;
	const int64_t fractionalDamage = damage % 100 * multiplier / 100;
	const int64_t maximum = std::numeric_limits<int64_t>::max();
	if(wholeDamage > maximum / multiplier)
		return maximum;
	const int64_t scaledWholeDamage = wholeDamage * multiplier;
	if(scaledWholeDamage > maximum - fractionalDamage)
		return maximum;
	return scaledWholeDamage + fractionalDamage;
}

int64_t CSpell::applyIncomingElementalDamageBonus(const battle::Unit * affectedCreature, const int64_t damage) const
{
	if(!affectedCreature || !isMagical() || !isDamage()
		|| damageElement == SpellDamageElement::NONE || damage <= 0)
		return damage;

	const auto bonuses = affectedCreature->getBonusesOfType(BonusType::ELEMENTAL_SPELL_DAMAGE_RECEIVED,
		BonusSubtypeID(BonusCustomSubtype(static_cast<int32_t>(damageElement))));
	if(bonuses->empty())
		return damage;

	// This modifier is an additive percentage-point total, applied once after
	// the ordinary target-side magical defenses. Saturation keeps malformed or
	// unusually large bonus sets from overflowing the accumulator.
	int64_t totalPercent = 0;
	constexpr int64_t maximum = std::numeric_limits<int64_t>::max();
	constexpr int64_t minimum = std::numeric_limits<int64_t>::min();
	for(const auto & bonus : *bonuses)
	{
		const int64_t value = bonus->val;
		if(value > 0 && totalPercent > maximum - value)
			totalPercent = maximum;
		else if(value < 0 && totalPercent < minimum - value)
			totalPercent = minimum;
		else
			totalPercent += value;
	}

	if(totalPercent <= -100)
		return 0;
	const int64_t multiplier = totalPercent > maximum - 100 ? maximum : totalPercent + 100;
	const int64_t wholeDamage = damage / 100;
	const int64_t fractionalDamage = damage % 100;
	const int64_t wholeMultiplier = multiplier / 100;
	const int64_t fractionalMultiplier = multiplier % 100;
	const int64_t scaledFraction = fractionalDamage * wholeMultiplier
		+ fractionalDamage * fractionalMultiplier / 100;
	if(wholeDamage > maximum / multiplier)
		return maximum;
	const int64_t scaledWholeDamage = wholeDamage * multiplier;
	if(scaledWholeDamage > maximum - scaledFraction)
		return maximum;
	return scaledWholeDamage + scaledFraction;
}

bool CSpell::hasSchool(SpellSchool which) const
{
	return schools.count(which);
}

bool CSpell::canBeCast(const CBattleInfoCallback * cb, spells::Mode mode, const spells::Caster * caster,
	bool metamagicGrand) const
{
	//if caller do not interested in description just discard it and do not pollute even debug log
	spells::detail::ProblemImpl problem;
	return canBeCast(problem, cb, mode, caster, metamagicGrand);
}

bool CSpell::canBeCast(spells::Problem & problem, const CBattleInfoCallback * cb, spells::Mode mode,
	const spells::Caster * caster, bool metamagicGrand) const
{
spells::BattleCast event(cb, caster, mode, this);
	if(mode == spells::Mode::HERO)
	{
		const auto side = cb->playerToSide(caster->getCasterOwner());
		event.setMetamagicFollowup(cb->battleCanUseMetamagicFollowup(side));
		event.setMetamagicGrand(metamagicGrand);
	}
	auto mechanics = battleMechanics(&event);
	if(mechanics->canBeCast(problem))
		return true;

	// Availability answers whether the spell has at least one legal mode. The
	// Selective Dispel perk can make a cast legal even when ordinary smart
	// targeting finds no valid stack (for example, only an enemy buff exists).
	const auto * hero = mode == spells::Mode::HERO ? caster->getHeroCaster() : nullptr;
	if(id == SpellID::SLOW && hero
		&& cb->getBattle() && !newHorizonsMagic::hasDistinctMassSlow(cb->getBattle()->getMagicRules())
		&& hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.temporalField"))
	{
		spells::BattleCast massEvent(cb, caster, mode, this);
		if(mode == spells::Mode::HERO)
		{
			massEvent.setMetamagicFollowup(cb->battleCanUseMetamagicFollowup(cb->playerToSide(caster->getCasterOwner())));
			massEvent.setMetamagicGrand(metamagicGrand);
		}
		massEvent.setMassSlow(true);
		spells::detail::ProblemImpl massProblem;
		if(battleMechanics(&massEvent)->canBeCast(massProblem))
			return true;
	}
	if(id != SpellID::DISPEL || !hero
		|| !hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.selectiveDispel"))
		return false;

	spells::BattleCast selectiveEvent(cb, caster, mode, this);
	if(mode == spells::Mode::HERO)
	{
		selectiveEvent.setMetamagicFollowup(cb->battleCanUseMetamagicFollowup(cb->playerToSide(caster->getCasterOwner())));
		selectiveEvent.setMetamagicGrand(metamagicGrand);
	}
	selectiveEvent.setSelectiveDispel(true);
	spells::detail::ProblemImpl selectiveProblem;
	return battleMechanics(&selectiveEvent)->canBeCast(selectiveProblem);
}

spells::AimType CSpell::getTargetType() const
{
	return targetType;
}

void CSpell::forEachSchool(const std::function<void(const SpellSchool &, bool &)>& cb) const
{
	bool stop = false;
	for(auto schoolID : LIBRARY->spellSchoolHandler->getAllObjects())
	{
		if(schools.count(schoolID))
		{
			cb(schoolID, stop);
			if(stop)
				break;
		}
	}
}

SpellID CSpell::getId() const
{
	return id;
}

std::string CSpell::getNameTextID() const
{
	TextIdentifier textId("spell", modScope, identifier, "name");
	return textId.get();
}

std::string CSpell::getNameTranslated() const
{
	return LIBRARY->generaltexth->translate(getNameTextID());
}

std::string CSpell::getDescriptionTextID(int32_t schoolLevel) const
{
	TextIdentifier textID("spell", modScope, identifier, "description", LEVEL_NAMES[schoolLevel]);
	return textID.get();
}

std::string CSpell::getDescriptionTranslated(int32_t schoolLevel) const
{
	return LIBRARY->generaltexth->translate(getDescriptionTextID(schoolLevel));
}

std::string CSpell::getAdventureEffectTextID(const std::string & effectType, const std::string & field) const
{
	TextIdentifier textID("spell", modScope, identifier, "adventureEffect", effectType, field);
	return textID.get();
}

std::string CSpell::getJsonKey() const
{
	return modScope + ':' + identifier;
}

std::string CSpell::getModScope() const
{
	return modScope;
}

int32_t CSpell::getIndex() const
{
	return id.toEnum();
}

int32_t CSpell::getIconIndex() const
{
	return getIndex();
}

int32_t CSpell::getLevel() const
{
	return level;
}

bool CSpell::isCombat() const
{
	return combat;
}

bool CSpell::isAdventure() const
{
	return !combat;
}

bool CSpell::isCreatureAbility() const
{
	return creatureAbility;
}

bool CSpell::isMagical() const
{
	return !nonMagical;
}

bool CSpell::isPositive() const
{
	return positive;
}

bool CSpell::isNegative() const
{
	return negative;
}

bool CSpell::isNeutral() const
{
	return !positive && !negative;
}

bool CSpell::isPersistent() const
{
	return persistent;
}

bool CSpell::isDamage() const
{
	return damage;
}

bool CSpell::isOffensive() const
{
	return offensive;
}

bool CSpell::isSpecial() const
{
	return special;
}

bool CSpell::isCommonHeroSpell() const
{
	return !isSpecial() && !isCreatureAbility();
}


bool CSpell::hasEffects() const
{
	return !levels[0].effects.Struct().empty() || !levels[0].cumulativeEffects.Struct().empty();
}

bool CSpell::hasBattleEffects() const
{
	return levels[0].battleEffects.getType() == JsonNode::JsonType::DATA_STRUCT && !levels[0].battleEffects.Struct().empty();
}

bool CSpell::canCastOnSelf() const
{
	return castOnSelf;
}

bool CSpell::canCastOnlyOnSelf() const
{
	return castOnlyOnSelf;
}

bool CSpell::canCastWithoutSkip() const
{
	return castWithoutSkip;
}

const ImagePath & CSpell::getIconImmune() const
{
	return iconImmune;
}

const std::string & CSpell::getIconBook() const
{
	return iconBook;
}

const std::string & CSpell::getIconEffect() const
{
	return iconEffect;
}

const std::string & CSpell::getIconScenarioBonus() const
{
	return iconScenarioBonus;
}

const std::string & CSpell::getIconScroll() const
{
	return iconScroll;
}

const AudioPath & CSpell::getCastSound() const
{
	return castSound;
}

int32_t CSpell::getCost(const int32_t skillLevel) const
{
	return getLevelInfo(skillLevel).cost;
}

int32_t CSpell::getBasePower() const
{
	return power;
}

int32_t CSpell::getLevelPower(const int32_t skillLevel) const
{
	return getLevelInfo(skillLevel).power;
}

si32 CSpell::getProbability(const FactionID & factionId) const
{
	if(!vstd::contains(probabilities, factionId))
	{
		return defaultProbability;
	}
	return probabilities.at(factionId);
}

void CSpell::getEffects(std::vector<Bonus> & lst, const int schoolLevel, const bool cumulative, const si32 duration, std::optional<si32 *> maxDuration) const
{
	if(schoolLevel < 0 || schoolLevel >= GameConstants::SPELL_SCHOOL_LEVELS)
	{
		logGlobal->error("invalid school level %d", schoolLevel);
		return;
	}

	const auto & levelObject = levels.at(schoolLevel);

	const auto & effectsJson = cumulative ? levelObject.cumulativeEffects : levelObject.effects;

	if(effectsJson.Struct().empty())
	{
		logGlobal->error("This spell (%s) has no effects for level %d", getNameTranslated(), schoolLevel);
		return;
	}

	for(const auto & [name, bonusNode] : effectsJson.Struct())
	{
		auto b = JsonUtils::parseBonus(bonusNode);
		if(!b)
			continue;
		const bool usePowerAsValue = bonusNode["val"].isNull();
		if(usePowerAsValue)
			b->val = levelObject.power;
		b->sid = BonusSourceID(id);
		b->source = BonusSource::SPELL_EFFECT;

		Bonus nb(*b);
		if(nb.turnsRemain == 0)
			nb.turnsRemain = duration;
		if(maxDuration)
			vstd::amax(*(maxDuration.value()), nb.turnsRemain);

		lst.push_back(nb);
	}
}

std::vector<int> CSpell::magicalDamageReductionSourcesBasisPoints(const battle::Unit * affectedCreature,
	int magicalDamageReductionBasisPoints, int perkMagicalDamageReductionBasisPoints,
	bool useFractionalMagicalDamageReduction) const
{
	std::vector<int> sources;
	if(!affectedCreature || !isMagical())
		return sources;
	const auto * bearer = affectedCreature->getBonusBearer();
	// Preserve the existing multi-school rule: the first matching school
	// supplies school-specific sources; each bonus remains independent.
	forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		const BonusSubtypeID subtype(school);
		const auto reductions = bearer->getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION, subtype);
		const auto fractional = bearer->getBonusesOfType(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, subtype);
		if(!reductions->empty() || (useFractionalMagicalDamageReduction && !fractional->empty()))
		{
			for(const auto & bonus : *reductions)
			{
				const int reduction = std::clamp(bonus->val, 0, 100);
				if(reduction > 0)
					sources.push_back(reduction * 100);
			}
			if(useFractionalMagicalDamageReduction)
			{
				for(const auto & bonus : *fractional)
				{
					const int reduction = std::clamp(bonus->val, 0, 10000);
					if(reduction > 0)
						sources.push_back(reduction);
				}
			}
			stop = true;
		}
	});
	const auto anySchool = bearer->getBonuses(
		Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(SpellSchool::ANY)),
		"type_SPELL_DAMAGE_REDUCTION_s_ANY");
	for(const auto & bonus : *anySchool)
	{
		const int reduction = std::clamp(bonus->val, 0, 100);
		if(reduction > 0)
			sources.push_back(reduction * 100);
	}
	if(useFractionalMagicalDamageReduction)
	{
		const auto anySchoolFractional = bearer->getBonuses(
			Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION_BASIS_POINTS, BonusSubtypeID(SpellSchool::ANY)),
			"type_SPELL_DAMAGE_REDUCTION_BASIS_POINTS_s_ANY");
		for(const auto & bonus : *anySchoolFractional)
		{
			const int reduction = std::clamp(bonus->val, 0, 10000);
			if(reduction > 0)
				sources.push_back(reduction);
		}
	}
	if(magicalDamageReductionBasisPoints > 0)
		sources.push_back(std::clamp(magicalDamageReductionBasisPoints, 0, 10000));
	if(perkMagicalDamageReductionBasisPoints > 0)
		sources.push_back(std::clamp(perkMagicalDamageReductionBasisPoints, 0, 10000));
	return sources;
}

bool CSpell::hasApplicableMagicalDamageReduction(const battle::Unit * affectedCreature,
	int magicalDamageReductionBasisPoints, int perkMagicalDamageReductionBasisPoints,
	bool useFractionalMagicalDamageReduction) const
{
	return !magicalDamageReductionSourcesBasisPoints(affectedCreature, magicalDamageReductionBasisPoints,
		perkMagicalDamageReductionBasisPoints, useFractionalMagicalDamageReduction).empty();
}

int64_t CSpell::adjustRawDamage(const spells::Caster * caster, const battle::Unit * affectedCreature, int64_t rawDamage,
	int ignoreSpellDamageReductionPercent, int magicalDamageReductionBasisPoints,
	int finalDamageMultiplierPercent, bool useIndependentMagicalDamageReduction,
	bool useFractionalMagicalDamageReduction, bool applyCasterBonuses,
	int perkMagicalDamageReductionBasisPoints,
	const std::vector<int> & independentPenetrationsPercent) const
{
	auto ret = rawDamage;
	ignoreSpellDamageReductionPercent = std::clamp(ignoreSpellDamageReductionPercent, 0, 100);
	//affected creature-specific part
	if(nullptr != affectedCreature)
	{
		const auto * bearer = affectedCreature->getBonusBearer();
		if(useIndependentMagicalDamageReduction && isMagical())
		{
			const auto reductionSourcesBasisPoints = magicalDamageReductionSourcesBasisPoints(affectedCreature,
				magicalDamageReductionBasisPoints, perkMagicalDamageReductionBasisPoints,
				useFractionalMagicalDamageReduction);

			auto penetrations = independentPenetrationsPercent;
			if(ignoreSpellDamageReductionPercent > 0)
				penetrations.push_back(ignoreSpellDamageReductionPercent);
			ret = spells::calculateMagicalDamageReductionBasisPoints(
				ret, reductionSourcesBasisPoints, penetrations).damageWithPenetration;
		}
		else
		{
			// Legacy school-specific reduction semantics: when a spell has multiple
			// schools, only the first matching school's aggregate is applied.
			forEachSchool([&](const SpellSchool & cnf, bool & stop)
			{
				if(bearer->hasBonusOfType(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(cnf)))
				{
					const int reduction = bearer->valOfBonuses(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(cnf));
					const int effectiveReduction = reduction * (100 - ignoreSpellDamageReductionPercent) / 100;
					ret *= 100 - effectiveReduction;
					ret /= 100;
					stop = true; //only bonus from one school is used
				}
			});

			CSelector selector = Selector::typeSubtype(BonusType::SPELL_DAMAGE_REDUCTION, BonusSubtypeID(SpellSchool::ANY));
			auto cachingStr = "type_SPELL_DAMAGE_REDUCTION_s_ANY";

			//general spell dmg reduction, works only on magical effects
			if(bearer->hasBonus(selector, cachingStr) && isMagical())
			{
				const int reduction = bearer->valOfBonuses(selector, cachingStr);
				const int effectiveReduction = reduction * (100 - ignoreSpellDamageReductionPercent) / 100;
				ret *= 100 - effectiveReduction;
				ret /= 100;
			}

			// Hold the Line's saved Iron Discipline value is an independent magical
			// reduction. Basis points preserve the exact half of an odd physical value.
			if(isMagical() && magicalDamageReductionBasisPoints > 0)
			{
				const int boundedReductionBasisPoints = std::clamp(magicalDamageReductionBasisPoints, 0, 10000);
				const int effectiveReductionBasisPoints = boundedReductionBasisPoints
					* (100 - ignoreSpellDamageReductionPercent) / 100;
				const int remainingDamageBasisPoints = 10000
					- std::clamp(effectiveReductionBasisPoints, 0, 10000);
				ret = ret / 10000 * remainingDamageBasisPoints
					+ ret % 10000 * remainingDamageBasisPoints / 10000;
			}
		}

		//dmg increasing
		if(bearer->hasBonusOfType(BonusType::MORE_DAMAGE_FROM_SPELL, BonusSubtypeID(id)))
		{
			ret *= 100 + bearer->valOfBonuses(BonusType::MORE_DAMAGE_FROM_SPELL, BonusSubtypeID(id));
			ret /= 100;
		}

		//invincible
		if(affectedCreature->isInvincible())
			ret = 0;
	}
	if(applyCasterBonuses)
		ret = caster->getSpellBonus(this, ret, affectedCreature);
	// Some magical damage sources (for example Fire Shield reflection) are
	// represented by positive timed spells and do not carry the DAMAGE flag.
	if(affectedCreature != nullptr && isMagical() && affectedCreature->getPhantomIntegrity() > 0)
	{
		ret = ret * newHorizonsSorcery::phantomArmyDamageTakenPercent(true) / 100;
	}
	// Apply spell-specific final damage bonuses before the per-creature cap so
	// that capped targets cannot take more than their configured maximum.
	if(finalDamageMultiplierPercent != 100)
	{
		const int multiplierPercent = std::max(0, finalDamageMultiplierPercent);
		ret = ret / 100 * multiplierPercent + ret % 100 * multiplierPercent / 100;
	}
	if(applyCasterBonuses)
		ret = applyElementalDamageBonus(caster, ret);
	ret = applyIncomingElementalDamageBonus(affectedCreature, ret);

	//cap damage received per single creature (e.g. HotA war machines), same rule as melee/ranged damage
	if(affectedCreature != nullptr)
	{
		int capPercentage = affectedCreature->valOfBonuses(BonusType::DAMAGE_RECEIVED_CAP);
		if(capPercentage > 0)
			ret = std::min<int64_t>(ret, affectedCreature->getMaxHealth() * capPercentage / 100);
	}

	return ret;
}

int64_t CSpell::calculateRawEffectValue(int32_t effectLevel, int32_t basePowerMultiplier, int32_t levelPowerMultiplier, int32_t powerDivisor) const
{
	if(powerDivisor <= 0)
		throw std::runtime_error("Spell power divisor must be positive");
	return static_cast<int64_t>(basePowerMultiplier) * getBasePower() / powerDivisor
		+ static_cast<int64_t>(levelPowerMultiplier) * getLevelPower(effectLevel);
}

void CSpell::setIsOffensive(const bool val)
{
	offensive = val;

	if(val)
	{
		positive = false;
		negative = true;
		damage = true;
	}
}

void CSpell::setIsRising(const bool val)
{
	rising = val;

	if(val)
	{
		positive = true;
		negative = false;
	}
}

JsonNode CSpell::convertTargetCondition(const BTVector & immunity, const BTVector & absImmunity, const BTVector & limit, const BTVector & absLimit) const
{
	static const std::string CONDITION_NORMAL = "normal";
	static const std::string CONDITION_ABSOLUTE = "absolute";

	JsonNode res;

	auto convertVector = [&](const std::string & targetName, const BTVector & source, const std::string & value)
	{
		for(auto bonusType : source)
		{
			std::string bonusName = LIBRARY->bth->bonusToString(bonusType);
			res[targetName][bonusName].String() = value;
		}
	};

	auto convertSection = [&](const std::string & targetName, const BTVector & normal, const BTVector & absolute)
	{
		convertVector(targetName, normal, CONDITION_NORMAL);
		convertVector(targetName, absolute, CONDITION_ABSOLUTE);
	};

	convertSection("allOf", limit, absLimit);
	convertSection("noneOf", immunity, absImmunity);

	return res;
}

void CSpell::setupMechanics()
{
	mechanics = spells::ISpellMechanicsFactory::get(this);
	adventureMechanics = IAdventureSpellMechanics::createMechanics(this);
}

const IAdventureSpellMechanics & CSpell::getAdventureMechanics() const
{
	return *adventureMechanics;
}

std::unique_ptr<spells::Mechanics> CSpell::battleMechanics(const spells::IBattleCast * event) const
{
	return mechanics->create(event);
}

void CSpell::registerIcons(const IconRegistar & cb) const
{
	cb(getIndex(), 0, "SPELLS", iconBook);
	cb(getIndex()+1, 0, "SPELLINT", iconEffect);
	cb(getIndex(), 0, "SPELLBON", iconScenarioBonus);
	cb(getIndex(), 0, "SPELLSCR", iconScroll);
}

///CSpell::AnimationInfo
AnimationPath CSpell::AnimationInfo::selectProjectile(const double angle) const
{
	AnimationPath res;
	double maximum = 0.0;

	for(const auto & info : projectile)
	{
		if(info.minimumAngle < angle && info.minimumAngle >= maximum)
		{
			maximum = info.minimumAngle;
			res = info.resourceName;
		}
	}
	return res;
}

///CSpell::TargetInfo
CSpell::TargetInfo::TargetInfo(const CSpell * spell, const int level, spells::Mode mode)
	: type(spell->getTargetType()),
	smart(false),
	massive(false),
	clearAffected(false)
{
	const auto & levelInfo = spell->getLevelInfo(level);

	smart = levelInfo.smartTarget;
	massive = levelInfo.range.empty();
	clearAffected = levelInfo.clearAffected;
}
