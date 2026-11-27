/*
 * ISpellMechanics.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#include "StdInc.h"
#include "../battle/QuickStudySpecialty.h"
#include "NewHorizonsWisdom.h"
#include "NewHorizonsSorceryRevision.h"
#include "ISpellMechanics.h"
#include "HeroSpellCastContext.h"
#include "NewHorizonsTimedUtility.h"
#include "NewHorizonsMetamagicEffects.h"
#include "NewHorizonsRestorationPools.h"
#include "NewHorizonsBlessDuration.h"
#include "NewHorizonsLightDamage.h"
#include "NewHorizonsTimedMagnitudes.h"
#include "NewHorizonsPlacementPower.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsImplosion.h"
#include "NewHorizonsCureHealing.h"
#include "NewHorizonsSorcery.h"
#include "NewHorizonsCrossSchoolFormula.h"
#include "NewHorizonsElementalTerrain.h"
#include "NewHorizonsNaturesWrath.h"
#include "NewHorizonsPandemonium.h"
#include "MagicalDamageReduction.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../battle/NewHorizonsShadowGift.h"
#include "../battle/NewHorizonsCombatSkills.h"
#include "../battle/NewHorizonsPlague.h"
#include "NewHorizonsSpellAvailability.h"

#include "BattleSpellMechanics.h"
#include "TargetCondition.h"
#include "Problem.h"
#include "CSpell.h"
#include "ObstacleCasterProxy.h"

#include "adventure/AdventureSpellMechanics.h"
#include "effects/Effects.h"
#include "NewHorizonsSpellcraft.h"

#include "../GameLibrary.h"
#include "../entities/hero/NewHorizonsHeroRules.h"
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/CGTownInstance.h"
#include "../bonuses/Bonus.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/HeroActionAllowanceState.h"
#include "../battle/MetamagicDoubleCast.h"
#include "../battle/HeroCommand.h"
#include "../battle/IBattleState.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../serializer/JsonDeserializer.h"
#include "../serializer/JsonSerializer.h"
#include "../BattleFieldHandler.h"

#include <vstd/RNG.h>

namespace spells
{

namespace
{

int64_t multiplyDivideFloor(int64_t value, uint64_t multiplier, int64_t divisor)
{
	// value is smaller than divisor. This bitwise quotient/remainder loop avoids
	// forming value * multiplier, which may overflow even when the final quotient
	// is small. The coefficient/Empower/specialty product is bounded
	// below 2^42. Specialty scaling may make the denominator as large as
	// INT32_MAX * 1e6 * 20, so keep the intermediate remainder unsigned.
	if(value < 0 || divisor <= 0 || value >= divisor
		|| static_cast<uint64_t>(divisor) > std::numeric_limits<uint64_t>::max() / 3)
		throw std::invalid_argument("Invalid quotient/remainder scaling inputs");
	const uint64_t unsignedValue = static_cast<uint64_t>(value);
	const uint64_t unsignedDivisor = static_cast<uint64_t>(divisor);
	int64_t quotient = 0;
	uint64_t remainder = 0;
	for(int bit = 63; bit >= 0; --bit)
	{
		quotient *= 2;
		remainder *= 2;
		if(multiplier & (uint64_t{1} << bit))
			remainder += unsignedValue;
		quotient += static_cast<int64_t>(remainder / unsignedDivisor);
		remainder %= unsignedDivisor;
	}
	return quotient;
}


}

int64_t scaleSpellPowerComponentWithCoefficientBasisPoints(const int64_t numerator, const int32_t divisor,
	const int32_t coefficientBasisPoints,
	const int32_t empowerSpellBonusPercent, const int32_t damageSpecialtyPercent)
{
	if(numerator < 0 || divisor <= 0 || coefficientBasisPoints < 0 || coefficientBasisPoints > 100000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000
		|| (damageSpecialtyPercent != 0 && damageSpecialtyPercent != 15 && damageSpecialtyPercent != 20))
		throw std::invalid_argument("Invalid Spell Power basis-point coefficient inputs");
	if(coefficientBasisPoints == 0 || numerator == 0)
		return 0;

	// Keep coefficient, Empower Spell, and the divisor in one
	// rational expression until the final floor. Quotient/remainder scaling
	// avoids multiplying a potentially large Spell Power numerator directly.
	int64_t denominator = static_cast<int64_t>(divisor) * 1000000;
	uint64_t multiplier = static_cast<uint64_t>(coefficientBasisPoints)
		* (100LL + empowerSpellBonusPercent);
	if(damageSpecialtyPercent == 15)
	{
		denominator *= 20;
		multiplier *= 23;
	}
	else if(damageSpecialtyPercent == 20)
	{
		denominator *= 5;
		multiplier *= 6;
	}
	const int64_t whole = numerator / denominator;
	const int64_t remainder = numerator % denominator;
	const int64_t maximum = std::numeric_limits<int64_t>::max();
	if(whole > maximum / static_cast<int64_t>(multiplier))
		throw std::overflow_error("Spell Power basis-point coefficient overflows");
	const int64_t scaledWhole = whole * static_cast<int64_t>(multiplier);
	const int64_t scaledRemainder = multiplyDivideFloor(remainder, multiplier, denominator);
	if(scaledWhole > maximum - scaledRemainder)
		throw std::overflow_error("Spell Power basis-point coefficient overflows");
	return scaledWhole + scaledRemainder;
}

int64_t Mechanics::scaleSpellPowerComponent(const int64_t numerator, const int32_t divisor) const
{
	return scaleSpellPowerComponentWithCoefficient(numerator, divisor, 100);
}

int64_t Mechanics::scaleSpellPowerComponentWithCoefficient(const int64_t numerator, const int32_t divisor,
	const int32_t coefficientPercent) const
{
	if(numerator < 0 || divisor <= 0 || coefficientPercent < 0 || coefficientPercent > 1000)
		throw std::invalid_argument("Invalid Spell Power coefficient inputs");
	return this->scaleSpellPowerComponentWithCoefficientBasisPoints(
		numerator, divisor, coefficientPercent * 100);
}

int64_t Mechanics::scaleSpellPowerComponentWithCoefficientBasisPoints(const int64_t numerator,
	const int32_t divisor, const int32_t coefficientBasisPoints) const
{
	return scaleDamageSpellPowerComponentWithCoefficientBasisPoints(numerator, divisor, coefficientBasisPoints, 0);
}

int64_t Mechanics::scaleDamageSpellPowerComponentWithCoefficientBasisPoints(const int64_t numerator,
	const int32_t divisor, const int32_t coefficientBasisPoints, const int32_t damageSpecialtyPercent) const
{
	return spells::scaleSpellPowerComponentWithCoefficientBasisPoints(numerator, divisor,
		coefficientBasisPoints, getEmpowerSpellBonusPercent(), damageSpecialtyPercent);
}

bool heroLuckDamageEligible(const Spell & spell, const JsonNode & savedMagicRules)
{
	if(spell.getId() == SpellID::DEATH_STARE)
		return false;
	const auto & key = spell.getJsonKey();
	return spell.isDamage() || newHorizonsMagic::physicalPoisonEnabled(savedMagicRules, spell.getId())
		|| newHorizonsMagic::earthquakeRulesEnabled(savedMagicRules, spell.getId())
		|| key == "new-horizons:lifeDrain" || key == "new-horizons:soulChain"
		|| key == "new-horizons:plague" || key == "new-horizons:hexOfPain"
		|| newHorizonsMagic::isLandMine(spell.getId()) || newHorizonsMagic::isFireWall(spell.getId());
}

int64_t scaleHeroLuckDamage(int64_t damage, int32_t multiplierPercent)
{
	if(multiplierPercent < 0 || multiplierPercent > 1000)
		throw std::invalid_argument("Invalid Hero Luck damage multiplier");
	if(damage <= 0)
		return damage;
	const int64_t multiplier = multiplierPercent;
	const int64_t quotient = damage / 100;
	const int64_t remainder = damage % 100 * multiplier / 100;
	if(multiplier != 0 && quotient > (std::numeric_limits<int64_t>::max() - remainder) / multiplier)
		return std::numeric_limits<int64_t>::max();
	return quotient * multiplier + remainder;
}

int64_t Mechanics::scaleHeroLuckDamage(int64_t damage) const
{
	return spells::scaleHeroLuckDamage(damage, getHeroLuckDamageMultiplierPercent());
}

int64_t Mechanics::getHexOfPainFlatDamage() const
{
	if(getSpellId().toSpell()->getJsonKey() != "new-horizons:hexOfPain")
		return 0;
	const auto * hero = getHeroCaster();
	return scaleHeroLuckDamage(newHorizonsMagic::hexOfPainFlatDamage(getEffectPower(), getDamageSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent(),
		hero ? hero->getDamageSpellSpecialtyBonusPercent(getSpellId()) : 0,
		usesNewHorizonsMagicV3() && hero
			&& hero->hasActivePerk("new-horizons:shadowMagic", "new-horizons:shadowMagic.painweaver")));
}

int32_t Mechanics::getSchoolRankPowerCoefficientPercent() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return 100;

	return newHorizonsMagic::spellPowerCoefficientPercent(
		battleState->getMagicRules(), getHeroCaster(), getSpellId());
}


int64_t Mechanics::scaleRecipientSpellPowerComponent(int64_t numerator, int64_t divisor, const battle::Unit * target) const
{
	const auto * hero = getHeroCaster();
	const int specialty = hero && usesNewHorizonsMagicV3()
		&& getSpellId().toSpell()->getJsonKey() == "new-horizons:crusade"
		? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0;
	return scaleRecipientSpellPowerComponentWithSpecialty(numerator, divisor, target, specialty);
}

int64_t Mechanics::scaleRecipientSpellPowerComponentWithSpecialty(int64_t numerator, int64_t divisor,
	const battle::Unit * target, int32_t specialtyPercent) const
{
	const int percent = getCrownAndAltarBonusPercent(target);
	if(percent == 0)
		return scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
			numerator, divisor, getSpellPowerCoefficientBasisPoints(), specialtyPercent);
	if(numerator < 0 || divisor <= 0 || numerator > std::numeric_limits<int64_t>::max() / (100 + percent)
		|| divisor > std::numeric_limits<int64_t>::max() / 100)
		throw std::runtime_error("Invalid Crown and Altar Spell Power component");
	return scaleDamageSpellPowerComponentWithCoefficientBasisPoints(numerator * (100 + percent), divisor * 100,
		getSpellPowerCoefficientBasisPoints(), specialtyPercent);
}

int32_t Mechanics::getNewHorizonsQuicksandPatchCount() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return 0;

	const auto patchCount = newHorizonsMagic::quicksandPatchCount(battleState->getMagicRules(),
		getHeroCaster(), getSpellId(), getEffectPower(), getEffectPowerDivisor(), getEmpowerSpellBonusPercent(), getCastSpellPowerComponentBonusPercent(),
		getSpellPowerCoefficientBasisPoints());
	return patchCount.value_or(0);
}

int32_t Mechanics::getNewHorizonsLandMinePatchCount() const
{
	if(!usesNewHorizonsMagic() || !newHorizonsMagic::isLandMine(getSpellId()))
		return 0;

	return newHorizonsMagic::landMineCountForHero(getEffectPower(), getSpellPowerCoefficientBasisPoints(), getHeroCaster());
}

bool Mechanics::usesNewHorizonsEarthquake() const
{
	const auto * state = battle() ? battle()->getBattle() : nullptr;
	return state && newHorizonsMagic::earthquakeRulesEnabled(state->getMagicRules(), getSpellId());
}

int32_t Mechanics::getNewHorizonsEarthquakeParameter(const std::string & name) const
{
	if(!usesNewHorizonsEarthquake())
		return 0;
	const auto & terrain = battle()->getBattle()->getMagicRules()["spells"]["core:earthquake"]["earthquake"];
	const auto found = terrain.Struct().find(name);
	return found == terrain.Struct().end() ? 0 : static_cast<int32_t>(found->second.Integer());
}

int32_t Mechanics::getNewHorizonsEarthquakeSectionCount() const
{
	if(!usesNewHorizonsEarthquake())
		return 0;
	const auto & profile = battle()->getBattle()->getMagicRules()["spells"]["core:earthquake"]["earthquake"];
	const auto ordinary = newHorizonsMagic::earthquakeSectionCount(profile, getEffectPower(),
		getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent());
	return static_cast<int32_t>(std::min<int64_t>(profile["maxSections"].Integer(),
		adjustMetamagicMagnitude(ordinary, nullptr)));
}

bool Mechanics::usesNewHorizonsHavocStructures() const
{
	const auto * state = battle() ? battle()->getBattle() : nullptr;
	return state && newHorizonsMagic::havocStructuresEnabled(state->getMagicRules(), getSpellId());
}

int32_t Mechanics::getNewHorizonsHavocStructuralDamage() const
{
	if(!usesNewHorizonsHavocStructures())
		return 0;

	const int64_t rawDamage = scaleHeroLuckDamage(std::max<int64_t>(0, getEffectValue()));
	const int percentage = newHorizonsMagic::havocFortificationDamagePercent(
		battle()->getBattle()->getMagicRules(), getSpellId());
	return newHorizonsMagic::scaleHavocStructuralDamage(rawDamage, percentage,
		newHorizonsMagic::havocStructuralPerkBonusPercent(
			battle()->getBattle()->getMagicRules(), getHeroCaster(), getSpellId()));
}

bool Mechanics::destroysNewHorizonsHavocMagicalObstacles() const
{
	const auto * state = battle() ? battle()->getBattle() : nullptr;
	return state && newHorizonsMagic::havocDestroysOrdinaryMagicalObstacles(
		state->getMagicRules(), getHeroCaster(), getSpellId());
}

int32_t Mechanics::getShadowGiftSacrificeCostBasisPoints() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState || !newHorizonsMagic::shadowGiftEnabled(battleState->getMagicRules(), getSpellId())
		|| !newHorizonsShadowGift::isValidSacrificePercent(getShadowGiftSacrificePercent()))
		return 0;

	return newHorizonsShadowGift::getSacrificeCostBasisPoints(
		getShadowGiftSacrificePercent(), newHorizonsMagic::hasDarkGiftPerk(getHeroCaster()));
}

int32_t Mechanics::getShadowGiftDamageBonusBasisPoints() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState || !newHorizonsMagic::shadowGiftEnabled(battleState->getMagicRules(), getSpellId())
		|| !newHorizonsShadowGift::isValidSacrificePercent(getShadowGiftSacrificePercent()))
		return 0;

	return newHorizonsShadowGift::getDamageBonusBasisPoints(getShadowGiftSacrificePercent(), getEffectPower(),
		getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent());
}

int32_t Mechanics::getEmpowerSpellBonusPercent() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return 0;

	return newHorizonsMagic::empowerSpellBonusPercent(
		battleState->getMagicRules(), getHeroCaster(), getSpellId(), isMassSlow() ? 3 : 1);
}

int32_t Mechanics::getEffectiveChainLength(const int32_t configuredLength) const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return configuredLength;

	return newHorizonsMagic::chainLightningTargetCount(
		battleState->getMagicRules(), getSpellId(), configuredLength);
}

int32_t Mechanics::getNewHorizonsChainLightningRetentionPercent(const int32_t targetIndex) const
{
	if(targetIndex < 0 || targetIndex >= newHorizonsMagic::CHAIN_LIGHTNING_FIXED_TARGET_COUNT_V3)
		return -1;

	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return -1;
	const auto & rules = battleState->getMagicRules();
	if(!newHorizonsMagic::rulesActive(rules)
		|| rules["rulesetVersion"].Integer() != newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
		return -1;

	return newHorizonsMagic::chainLightningRetentionPercent(
		rules, getSpellId(), targetIndex);
}

static std::shared_ptr<TargetCondition> makeCondition(const CSpell * s)
{
	auto res = std::make_shared<TargetCondition>();

	JsonDeserializer deser(nullptr, s->targetCondition);
	res->serializeJson(deser, TargetConditionItemFactory::getDefault());

	return res;
}

class CustomMechanicsFactory : public ISpellMechanicsFactory
{
public:
	RecipientConditionResult checkRecipient(const RecipientConditionContext & context, const battle::Unit * target) const override
	{
		if(context.source != spell)
			return RecipientConditionResult::INVALID_CONTEXT;
		return targetCondition->checkRecipient(context, target);
	}
	std::unique_ptr<Mechanics> create(const IBattleCast * event) const override
	{
		auto * ret = new BattleSpellMechanics(event, effects, targetCondition);
		return std::unique_ptr<Mechanics>(ret);
	}
protected:
	std::shared_ptr<effects::Effects> effects;

	CustomMechanicsFactory(const CSpell * s)
		: ISpellMechanicsFactory(s), effects(new effects::Effects)
	{
		targetCondition = makeCondition(s);
	}

	void loadEffects(const JsonNode & config, const int level)
	{
		effects->data.at(level) = effects::Effects::loadJson(config, spell->modScope, spell->identifier);
	}
private:
	std::shared_ptr<TargetCondition> targetCondition;
};

class ConfigurableMechanicsFactory : public CustomMechanicsFactory
{
public:
	ConfigurableMechanicsFactory(const CSpell * s)
		: CustomMechanicsFactory(s)
	{
		for(int level = 0; level < GameConstants::SPELL_SCHOOL_LEVELS; level++)
			loadEffects(s->getLevelInfo(level).battleEffects, level);
	}
};


//to be used for spells configured with old format
class FallbackMechanicsFactory : public CustomMechanicsFactory
{
	JsonNode usePowerAsVal(const JsonNode & effectsNode, si32 power) const
	{
		JsonNode result = effectsNode;
		for(auto & [name, bonusNode] : result.Struct())
			if(bonusNode["val"].isNull())
				bonusNode["val"].Integer() = power;
		return result;
	}

public:
	FallbackMechanicsFactory(const CSpell * s)
		: CustomMechanicsFactory(s)
	{
		for(int level = 0; level < GameConstants::SPELL_SCHOOL_LEVELS; level++)
		{
			const CSpell::LevelInfo & levelInfo = s->getLevelInfo(level);
			assert(levelInfo.battleEffects.isNull());

			if(!levelInfo.effects.Struct().empty())
			{
				JsonNode config;
				config["timed"]["type"].String() = "core:timed";
				config["timed"]["bonus"] = usePowerAsVal(levelInfo.effects, levelInfo.power);
				config.setModScope(s->modScope);
				loadEffects(config, level);
			}
			else if(!levelInfo.cumulativeEffects.Struct().empty())
			{
				JsonNode config;
				config["timed"]["type"].String() = "core:timed";
				config["timed"]["cumulative"].Bool() = true;
				config["timed"]["bonus"] = usePowerAsVal(levelInfo.cumulativeEffects, levelInfo.power);
				config.setModScope(s->modScope);
				loadEffects(config, level);
			}
		}
	}
};

BattleCast::~BattleCast() = default;

const CSpell * BattleCast::getSpell() const
{
	return spell;
}

Mode BattleCast::getMode() const
{
	return mode;
}

const Caster * BattleCast::getCaster() const
{
	return caster;
}

const CBattleInfoCallback * BattleCast::getBattle() const
{
	return cb;
}

BattleCast::OptionalValue BattleCast::getSpellLevel() const
{
	return magicSkillLevel;
}

BattleCast::OptionalValue BattleCast::getEffectPower() const
{
	return effectPower;
}

BattleCast::OptionalValue BattleCast::getEffectDuration() const
{
	return effectDuration;
}

BattleCast::OptionalValue BattleCast::getOvercharge() const
{
	return overcharge;
}

SpellID BattleCast::getCureAffliction() const
{
	return cureAffliction;
}

std::string BattleCast::getCurePhysicalAffliction() const
{
	return curePhysicalAffliction;
}

std::vector<std::pair<int32_t, SpellID>> BattleCast::getPurifyChoices() const
{
	return purifyChoices;
}

bool BattleCast::getForceNonSmartTargeting() const
{
	return forceNonSmartTargeting;
}

bool BattleCast::getSelectiveDispel() const
{
	return selectiveDispel;
}

bool BattleCast::getMassSlow() const
{
	return massSlow;
}

int32_t BattleCast::getShadowGiftSacrificePercent() const
{
	return shadowGiftSacrificePercent;
}

AutomaticHeroCastReceipt BattleCast::getAutomaticHeroCast() const
{
	return automaticHeroCast;
}

std::optional<newHorizonsHeroLuck::Receipt> BattleCast::getHeroLuck() const
{
	return heroLuck;
}

BattleCast::OptionalValue64 BattleCast::getEffectValue() const
{
	return effectValue;
}

bool BattleCast::isForceMassive() const
{
	return forceMassive;
}

void BattleCast::setSpellLevel(BattleCast::Value value)
{
	magicSkillLevel = std::make_optional(value);
}

void BattleCast::setEffectPower(BattleCast::Value value)
{
	effectPower = std::make_optional(value);
}

void BattleCast::setEffectDuration(BattleCast::Value value)
{
	effectDuration = std::make_optional(value);
}

void BattleCast::setOvercharge(BattleCast::Value value)
{
	overcharge = std::make_optional(value);
}

void BattleCast::setCureAffliction(SpellID value)
{
	cureAffliction = value;
}

void BattleCast::setCurePhysicalAffliction(std::string value)
{
	curePhysicalAffliction = std::move(value);
}

void BattleCast::setPurifyChoices(std::vector<std::pair<int32_t, SpellID>> value)
{
	purifyChoices = std::move(value);
}

void BattleCast::setForceNonSmartTargeting(bool value)
{
	forceNonSmartTargeting = value;
}

void BattleCast::setSelectiveDispel(bool value)
{
	selectiveDispel = value;
}

void BattleCast::setMassSlow(bool value)
{
	massSlow = value;
}

void BattleCast::setShadowGiftSacrificePercent(const int32_t value)
{
	shadowGiftSacrificePercent = value;
}

void BattleCast::setAutomaticHeroCast(AutomaticHeroCastReceipt value)
{
	value.validateShape();
	if(value.automatic() && mode != Mode::HERO)
		throw std::runtime_error("Automatic hero provenance requires HERO spell mechanics");
	automaticHeroCast = value;
}

void BattleCast::setHeroLuck(const newHorizonsHeroLuck::Receipt & value)
{
	value.validate();
	heroLuck = value;
}

void BattleCast::setEffectValue(BattleCast::Value64 value)
{
	effectValue = std::make_optional(value);
}

void BattleCast::applyEffects(ServerCallback * server, const Target & target, bool indirect, bool ignoreImmunity) const
{
	auto m = mechanicsForTarget(target);

	m->applyEffects(server, target, indirect, ignoreImmunity);
}

std::unique_ptr<Mechanics> BattleCast::mechanicsForTarget(const Target & target) const
{
	BattleCast resolved = *this;
	resolved.concentration = false;
	resolved.concentrationClassified = true;
	auto mechanics = spell->battleMechanics(&resolved);
	const auto * hero = dynamic_cast<const CGHeroInstance *>(caster);
	if(!mechanics || mode != Mode::HERO || !hero || !cb || !cb->getBattle()
		|| !newHorizonsMagic::rulesActive(cb->getBattle()->getMagicRules())
		|| !newHorizonsMagic::spellAllowedBySavedRoster(cb->getBattle()->getMagicRules(), spell->getId())
		|| !hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsSpellcraft::CONCENTRATION)))
		return mechanics;
	resolved.concentration = mechanics->getTargetedStackCount(target) == 1;
	return resolved.concentration ? spell->battleMechanics(&resolved) : std::move(mechanics);
}

void BattleCast::cast(ServerCallback * server, Target target)
{
	if(target.empty())
		target.emplace_back();

	auto m = mechanicsForTarget(target);

	m->cast(server, target);
}

void BattleCast::castEval(ServerCallback * server, Target target)
{
	//TODO: make equivalent to normal cast
	if(target.empty())
		target.emplace_back();
	auto m = mechanicsForTarget(target);

	//TODO: reflection
	//TODO: random effects evaluation

	m->castEval(server, target);
}

bool BattleCast::castIfPossible(ServerCallback * server, Target target)
{
	if(spell->canBeCast(cb, mode, caster))
	{
		cast(server, std::move(target));
		return true;
	}
	return false;
}

///ISpellMechanicsFactory
ISpellMechanicsFactory::ISpellMechanicsFactory(const CSpell * s)
	: spell(s)
{

}

//must be instantiated in .cpp file for access to complete types of all member fields
ISpellMechanicsFactory::~ISpellMechanicsFactory() = default;

std::unique_ptr<ISpellMechanicsFactory> ISpellMechanicsFactory::get(const CSpell * s)
{
	if(s->hasBattleEffects())
		return std::make_unique<ConfigurableMechanicsFactory>(s);
	else
		return std::make_unique<FallbackMechanicsFactory>(s);
}

///Mechanics
Mechanics::Mechanics()
	: caster(nullptr),
	casterSide(BattleSide::NONE)
{

}

Mechanics::~Mechanics() = default;

Target Mechanics::getPandemoniumTargets() const
{
	return newHorizonsPandemonium::targets(*this);
}

int64_t Mechanics::getPandemoniumDebuffCount(const battle::Unit * recipient) const
{
	if(!recipient || !newHorizonsPandemonium::enabled(*this))
		return 0;
	if(pandemoniumDebuffsCaptured)
	{
		const auto found = pandemoniumDebuffCounts.find(recipient->unitId());
		return found == pandemoniumDebuffCounts.end() ? 0 : static_cast<int64_t>(found->second);
	}
	const auto * projected = battle()->battleGetUnitByID(recipient->unitId());
	return projected ? static_cast<int64_t>(newHorizonsDebuffStatuses::snapshot(*projected).count()) : 0;
}

int64_t Mechanics::getPandemoniumDamage(const battle::Unit * recipient, int64_t capturedCount) const
{
	if(capturedCount < 0)
		throw std::invalid_argument("Pandemonium count cannot be negative");
	return newHorizonsPandemonium::damage(*this, recipient, static_cast<size_t>(capturedCount));
}

void Mechanics::capturePandemoniumDebuffs()
{
	std::map<uint32_t, size_t> captured;
	for(const auto & recipient : newHorizonsPandemonium::snapshot(*this))
		captured.emplace(recipient.unit->unitId(), recipient.statuses.count());
	pandemoniumDebuffCounts = std::move(captured);
	pandemoniumDebuffsCaptured = true;
}

void Mechanics::clearPandemoniumDebuffs()
{
	pandemoniumDebuffCounts.clear();
	pandemoniumDebuffsCaptured = false;
}

Target Mechanics::getNaturesWrathRoute(const battle::Unit * first) const
{
	return newHorizonsNaturesWrath::route(*this, first);
}

int64_t Mechanics::getNaturesWrathHopPower(int32_t hopIndex) const
{
	return newHorizonsNaturesWrath::hopPower(*this, hopIndex);
}

int64_t Mechanics::getNaturesWrathDamage(const battle::Unit * recipient, int32_t hopIndex) const
{
	return newHorizonsNaturesWrath::damage(*this, recipient, hopIndex);
}

const Creature * Mechanics::getElementalConvergenceCreature() const
{
	const auto * battleInfo = battle() ? battle()->getBattle() : nullptr;
	if(!battleInfo)
		return nullptr;
	const auto creature = newHorizonsElementalTerrain::primaryElemental(*battleInfo);
	return creature ? creature->toCreature() : nullptr;
}

BaseMechanics::BaseMechanics(const IBattleCast * event):
	owner(event->getSpell()),
	mode(event->getMode()),
	forceMassive(event->isForceMassive()),
	cb(event->getBattle())
{
	caster = event->getCaster();
	captureOriginalSpellSpecialties(event);
	heroLuck = event->getHeroLuck();
	automaticHeroCast = event->getAutomaticHeroCast();
	creatureAbilityDescriptor = event->getCreatureAbilityDescriptor();
	if(creatureAbilityDescriptor) creatureAbilityDescriptor->validate();
	automaticHeroCast.validateShape();
	massSlow = event->getMassSlow();
	shadowGiftSacrificePercent = event->getShadowGiftSacrificePercent();

	casterSide = cb->playerToSide(caster->getCasterOwner());
	metamagicCastProfile = newHorizonsMetamagic::captureHeroCastProfile(caster->getHeroCaster(), cb->getBattle(),
		mode == Mode::HERO, owner->getId());
	if(mode == Mode::PASSIVE)
		metamagicCastProfile = newHorizonsMetamagic::captureDelayedCastProfile(caster);
	initializeHeroCastContext(event);
	initializeCastPower(event);
	const auto * extendingHero = dynamic_cast<const CGHeroInstance *>(caster);
	extendSpellEligible = mode == Mode::HERO && extendingHero && cb
		&& newHorizonsMagic::heroCastExtendAvailable(*extendingHero, *owner, *cb, effectLevel);

	initializeEffectDuration(event);
	overcharge = event->getOvercharge().value_or(0);
	cureAffliction = event->getCureAffliction();
	curePhysicalAffliction = event->getCurePhysicalAffliction();
	selectiveDispel = event->getSelectiveDispel();
	purifyChoices = event->getPurifyChoices();
	forceNonSmartTargeting = event->getForceNonSmartTargeting();
	// Saved pre-v3 casts retain their historical raw-power conversion. The
	// v3 damage-component path composes Stormcaller before the final floor.
	if(!usesNewHorizonsMagicV3()
		&& newHorizonsMagic::hasStormcallerPerk(dynamic_cast<const CGHeroInstance *>(caster), owner))
		effectPower = effectPower * 115 / 100;
	initializeEffectValue(event);
}

BaseMechanics::~BaseMechanics() = default;

bool BaseMechanics::adaptGenericProblem(Problem & target) const
{
	MetaString text;
	// %s recites the incantations but they seem to have no effect.
	text.appendTextID("core.genrltxt.541");
	assert(caster);
	text.replaceTextID(caster->getCasterNameTextID());

	target.add(std::move(text), spells::Problem::NORMAL);
	return false;
}

bool BaseMechanics::adaptProblem(ESpellCastProblem source, Problem & target) const
{
	if(source == ESpellCastProblem::OK)
		return true;

	switch(source)
	{
	case ESpellCastProblem::SPELL_LEVEL_LIMIT_EXCEEDED:
		{
			MetaString text;
			//TODO: refactor
			const auto * hero = dynamic_cast<const CGHeroInstance *>(caster);
			if(!hero)
				return adaptGenericProblem(target);

			//Recanter's Cloak or similar effect. Try to retrieve bonus
			const auto b = hero->getFirstBonus(Selector::type()(BonusType::BLOCK_MAGIC_ABOVE));
			//TODO what about other values and non-artifact sources?
			if(b && b->val == 2 && b->source == BonusSource::ARTIFACT)
			{
				//The %s prevents %s from casting 3rd level or higher spells.
				text.appendTextID("core.genrltxt.536");
				text.replaceName(b->sid.as<ArtifactID>());
				text.replaceTextID(caster->getCasterNameTextID());
				target.add(std::move(text), spells::Problem::NORMAL);
			}
			else if(b && b->source == BonusSource::TERRAIN_OVERLAY && LIBRARY->battlefields()->getById(b->sid.as<BattleField>())->identifier == "cursed_ground")
			{
				text.appendTextID("core.genrltxt.537");
				target.add(std::move(text), spells::Problem::NORMAL);
			}
			else
			{
				return adaptGenericProblem(target);
			}
		}
		break;
	case ESpellCastProblem::WRONG_SPELL_TARGET:
	case ESpellCastProblem::STACK_IMMUNE_TO_SPELL:
	case ESpellCastProblem::NO_APPROPRIATE_TARGET:
		{
			MetaString text;
			text.appendTextID("core.genrltxt.185");
			target.add(std::move(text), spells::Problem::NORMAL);
		}
		break;
	case ESpellCastProblem::INVALID:
		{
			MetaString text;
			text.appendRawString("Internal error during check of spell cast.");
			target.add(std::move(text), spells::Problem::CRITICAL);
		}
		break;
	default:
		return adaptGenericProblem(target);
	}

	return false;
}

int32_t BaseMechanics::getSpellIndex() const
{
	return getSpellId().toEnum();
}

SpellID BaseMechanics::getSpellId() const
{
	return owner->getId();
}

std::string BaseMechanics::getSpellName() const
{
	return owner->getNameTranslated();
}

std::string BaseMechanics::getCasterNameTextID() const
{
	return caster->getCasterNameTextID();
}

int32_t BaseMechanics::getSpellLevel() const
{
	return cb->battleGetSpellLevel(owner->getId());
}

bool BaseMechanics::isSmart() const
{
	if(usesNewHorizonsEarthquake())
		return false;
	if(isNewHorizonsCure())
		return true;

	if(forceNonSmartTargeting)
		return false;

	if(usesNewHorizonsDispelRules())
		return false;

	if(usesNewHorizonsBerserkTargeting())
		return true;

	// Selective Dispel explicitly lets the caster choose either a friendly or
	// enemy stack.  The ordinary basic-level Dispel smart-target restriction
	// would otherwise hide the enemy half of the perk.
	if(isSelectiveDispel())
		return false;

	const CSpell::TargetInfo targetInfo(owner, getRangeLevel(), mode);
	return targetInfo.smart;
}

bool BaseMechanics::isMassive() const
{
	if(isNewHorizonsCure() || usesNewHorizonsBerserkTargeting() || usesNewHorizonsEarthquake())
		return false;

	if(forceMassive || isMassSlow())
		return true;

	const CSpell::TargetInfo targetInfo(owner, getRangeLevel(), mode);
	return targetInfo.massive;
}

bool BaseMechanics::requiresClearTiles() const
{
	const CSpell::TargetInfo targetInfo(owner, getRangeLevel(), mode);
	return targetInfo.clearAffected;
}

bool BaseMechanics::alwaysHitFirstTarget() const
{
	return mode == Mode::SPELL_LIKE_ATTACK;
}

bool BaseMechanics::isNegativeSpell() const
{
	if(usesNewHorizonsEarthquake())
		return true;
	return owner->isNegative();
}

bool BaseMechanics::isPositiveSpell() const
{
	return owner->isPositive();
}

bool BaseMechanics::isNeutralSpell() const
{
	if(usesNewHorizonsEarthquake())
		return false;
	return owner->isNeutral();
}

bool BaseMechanics::isMagicalEffect() const
{
	return owner->isMagical();
}

int32_t BaseMechanics::getPlaguePropagationLimit() const
{
	if(!cb || !cb->getBattle() || !owner || owner->getJsonKey() != newHorizonsPlague::SPELL_ID
		|| !usesNewHorizonsMagicV3())
		return 1;
	const auto normal = newHorizonsPlague::normalPropagationLimit(cb->getBattle()->getMagicRules());
	const auto * hero = mode == Mode::HERO && caster ? caster->getHeroCaster() : nullptr;
	return normal + (hero && hero->hasActivePerk(newHorizonsPlague::SKILL, newHorizonsPlague::PLAGUEBEARER) ? 1 : 0);
}

JsonNode Mechanics::getCapturedMdrPenetration() const
{
	return {};
}

void BaseMechanics::registerOverwhelmingFormulaCast(ServerCallback * server)
{
	if(!server || mode != Mode::HERO || !overwhelmingFormulaEligible
		|| overwhelmingFormulaToken != 0 || !cb || !cb->getBattle())
		return;
	auto state = cb->getBattle()->getOverwhelmingFormulaState(casterSide);
	const auto token = state.registerAcceptedEligibleCast();
	if(token == 0)
		return;
	SetOverwhelmingFormulaState registration;
	registration.battleID = cb->getBattle()->getBattleID();
	registration.side = casterSide;
	registration.state = state;
	server->apply(registration);
	// The token becomes usable only after the real or detached state accepts it.
	overwhelmingFormulaToken = token;
}


int64_t Mechanics::adjustDirectCreatureActivationDamage(const int64_t damage) const
{
	if(damage <= 0)
		return 0;
	const int percent = std::clamp(getDirectCreatureActivationDamagePercent(), 1, 100);
	const int inspiration = getInspirationalLeaderCreatureDamagePercent();
	const int64_t factor = int64_t(percent) * inspiration;
	// Combine both final-output factors before a single floor, without damage*factor overflow.
	constexpr int64_t denominator = 10000;
	const int64_t remainder = (damage % denominator) * factor / denominator;
	const int64_t quotient = damage / denominator;
	if(quotient > (std::numeric_limits<int64_t>::max() - remainder) / factor)
		return std::numeric_limits<int64_t>::max();
	return quotient * factor + remainder;
}

int32_t BaseMechanics::getInspirationalLeaderCreatureDamagePercent() const
{
	if(mode != Mode::CREATURE_ACTIVE || !caster || caster->getHeroCaster())
		return 100;
	const auto * callback = battle();
	if(!callback || !callback->getBattle())
		return 100;
	const auto * unit = getUnitCaster();
	if(!newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit)
		|| !callback->battleActiveUnit()
		|| callback->battleActiveUnit()->unitId() != unit->unitId())
		return 100;
	const auto bonuses = unit->getBonuses(Selector::typeSubtype(BonusType::PERCENTAGE_DAMAGE_BOOST,
		BonusSubtypeID(BonusCustomSubtype::damageTypeMelee)));
	for(const auto & bonus : *bonuses)
		if(bonus->stacking == "new-horizons:discipline.inspirationalLeader"
			&& bonus->source == BonusSource::HERO_SPECIAL
			&& bonus->duration == BonusDuration::STACK_ACTIVATION && bonus->val == 10)
			return 110;
	return 100;
}

int32_t BaseMechanics::getDirectCreatureActivationDamagePercent() const
{
	if(mode != Mode::CREATURE_ACTIVE || !caster || caster->getHeroCaster())
		return 100;
	const auto * callback = battle();
	if(!callback || !callback->getBattle())
		return 100;
	const auto * unit = getUnitCaster();
	if(!newHorizonsCombatSkills::isOrdinaryCreatureAttacker(unit)
		|| !callback->battleActiveUnit()
		|| callback->battleActiveUnit()->unitId() != unit->unitId())
		return 100;
	if(callback->battleIsMoraleExtraActivation(unit))
		return callback->getBattle()->getMoraleExtraDamagePercent();
	if(!heroCommands::isCanonicalRules(callback->getBattle()->getHeroCommandRules()))
		return 100;
	const auto side = callback->playerToSide(callback->battleGetOwner(unit));
	const auto order = callback->battleGetHeroOrderState(side, HeroCommand::SECOND_WIND);
	const auto * hero = callback->battleGetOwnerHero(unit);
	if(!hero || !order || !order->secondWindActive
		|| order->primaryTargetUnitId != unit->unitId()
		|| !order->scheduledFor(unit->unitId(), callback->battleGetRound()))
		return 100;
	// Second Wind and earned Morale are separate genuine origins: the live
	// activation transition clears Morale when beginning Second Wind, and the
	// completion transition spends Second Wind before any earned Morale check.
	return heroCommands::orderPotency(
		heroCommands::secondWindPercent(*hero,
			order->divineMandateEfficiencyBonusPercent()),
		newHorizonsDivineMandate::crownSecondWindPercent(*hero, *order, unit->unitId()),
		*order, callback->getBattle()->getMagicRules());
}

int32_t BaseMechanics::getCrownAndAltarBonusPercent(const battle::Unit * target) const
{
	return target && !target->isGhost()
		&& cb->battleGetOwner(target) == getCasterColor()
		&& std::binary_search(crownAndAltarRecipientUnitIds.begin(), crownAndAltarRecipientUnitIds.end(), target->unitId())
		? 20 : 0;
}

IBattleCast::Value64 BaseMechanics::getRecipientEffectValue(const battle::Unit * target) const
{
	if(effectValueWasOverridden || getCrownAndAltarBonusPercent(target) == 0)
		return getEffectValue();
	const auto * hero = getHeroCaster();
	const int specialty = hero ? hero->getNonDamageSpellSpecialtyBonusPercent(owner->getId()) : 0;
	const int coefficient = getSpellPowerCoefficientBasisPoints();
	const int multiplier = 100 + getCrownAndAltarBonusPercent(target);
	if(isNewHorizonsCure())
		return newHorizonsMagic::cureHealingBudget(hero, getEffectPower(), coefficient, getEmpowerSpellBonusPercent(), multiplier - 100);
	if(isNewHorizonsResurrection())
		return newHorizonsMagic::resurrectionRestorationPool(getEffectPower(), coefficient, getEmpowerSpellBonusPercent(), specialty, multiplier - 100);
	return getEffectValue();
}

int32_t BaseMechanics::getHeroLuckDamageMultiplierPercent() const
{
	return heroLuck && heroLuck->has(newHorizonsHeroLuck::Proc::DruidicLuck)
		? newHorizonsHeroLuck::damageMultiplierPercent(cb->getBattle()->getMagicRules()) : 100;
}

int64_t BaseMechanics::applySpellBonus(int64_t value, const battle::Unit * target) const
{
	const auto ordinary = caster->getSpellBonus(owner, value, target);
	return newHorizonsMetamagic::outgoingMagnitude(ordinary, getMetamagicCastProfile(), target);
}

int64_t BaseMechanics::applySpecificSpellBonus(int64_t value) const
{
	return newHorizonsMetamagic::outgoingMagnitude(caster->getSpecificSpellBonus(owner, value), getMetamagicCastProfile(), nullptr);
}

int64_t BaseMechanics::calculateRawEffectValue(int32_t basePowerMultiplier, int32_t levelPowerMultiplier) const
{
	return owner->calculateRawEffectValue(getEffectLevel(), basePowerMultiplier, levelPowerMultiplier, getEffectPowerDivisor());
}

Target BaseMechanics::canonicalizeTarget(const Target & aim) const
{
	return aim;
}

bool BaseMechanics::ownerMatches(const battle::Unit * unit) const
{
	if(owner->isNeutral())
		return true; // neutral spell: no ownership filtering

	return ownerMatches(unit, owner->isPositive());
}

bool BaseMechanics::ownerMatches(const battle::Unit * unit, const bool sameOwner) const
{
	return cb->battleMatchOwner(caster->getCasterOwner(), unit, sameOwner);
}

IBattleCast::Value BaseMechanics::getEffectLevel() const
{
	// Core Expert Dispel makes its status-removal effect optional and also adds
	// obstacle removal. The v3 single-target spell uses the unchanged full
	// Dispel effect from Advanced at every rank, without those inherited Expert
	// side effects.
	if(usesNewHorizonsDispelRules())
		return std::min<IBattleCast::Value>(effectLevel, static_cast<IBattleCast::Value>(MasteryLevel::ADVANCED));

	return effectLevel;
}

IBattleCast::Value BaseMechanics::getRangeLevel() const
{
	// New Horizons Cure and saved-v3 Berserk target one unit/stack at every
	// mastery rank. Keep each spell's effect level independent from this range.
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	const bool newHorizonsPhysicalPoison = caster && caster->getHeroCaster() && battleState
		&& newHorizonsMagic::physicalPoisonEnabled(battleState->getMagicRules(), owner->getId());
	if(isNewHorizonsCure() || usesNewHorizonsBerserkTargeting() || newHorizonsPhysicalPoison)
		return 0;

	// V3 restores single-target Expert targeting for the 23 core spells whose
	// vanilla static data is Mass. Keep this lookup saved-rule- and spell-specific
	// so v1/v2 battle snapshots use vanilla static spell data. Effect
	// mastery remains untouched, and isMassive() still honors explicit overrides.
	if(!forceMassive && !isMassSlow() && cb->getBattle()
		&& newHorizonsMagic::expertRangeIsSingleTarget(
			cb->getBattle()->getMagicRules(), owner->getId()))
		return std::min<IBattleCast::Value>(rangeLevel, 2);

	// Temporal Field owns Slow's mass mode explicitly. At Expert mastery the
	// legacy spell data would otherwise make the "Ordinary" branch a full-power,
	// single-cost mass cast and bypass the perk's saved budget and trade-off.
	// Preserve Expert effect magnitude while using Advanced targeting unless
	// this cast selected the authoritative Temporal Field mode.
	if(!isMassSlow() && owner->getId() == SpellID::SLOW && cb->getBattle()
		&& newHorizonsMagic::rulesActive(cb->getBattle()->getMagicRules()))
	{
		return std::min<IBattleCast::Value>(rangeLevel, 2);
	}
	return rangeLevel;
}

int32_t BaseMechanics::getEffectPowerDivisor() const
{
	return caster->getEffectPowerDivisor(owner);
}

IBattleCast::Value BaseMechanics::getEffectPower() const
{
	return effectPower;
}

int32_t BaseMechanics::getArcaneFocusBonusPercent() const
{
	return arcaneFocusBonusPercent;
}

int32_t BaseMechanics::getConsecratedCastingBonusPercent() const
{
	return consecratedCastingBonusPercent;
}


IBattleCast::Value BaseMechanics::getEffectDuration() const
{
	return effectDuration;
}

IBattleCast::Value BaseMechanics::adjustEffectDuration(IBattleCast::Value baseDuration) const
{
	if(baseDuration > 0 && mode == Mode::HERO)
		baseDuration = static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(),
			static_cast<int64_t>(baseDuration) + newHorizonsSorceryRevision::spellbinderBonus(getHeroCaster(), getSpellId())));
	return newHorizonsMagic::durationWithCastExtensions(baseDuration, extendSpellEligible, false);
}

IBattleCast::Value64 BaseMechanics::getEffectValue() const
{
	if(isNewHorizonsStormOfDaggers())
	{
		if(stormOfDaggersTargetCount > 0)
			return getStormOfDaggersDamagePerTarget(stormOfDaggersTargetCount);
		return getStormOfDaggersTotalDamage(1);
	}

	return effectValue;
}

IBattleCast::Value BaseMechanics::getOvercharge() const
{
	return overcharge;
}

bool BaseMechanics::isSelectiveDispel() const
{
	return selectiveDispel;
}

bool BaseMechanics::isNewHorizonsCure() const
{
	return cb && cb->getBattle()
		&& newHorizonsMagic::cureEnabled(cb->getBattle()->getMagicRules(), owner->getId());
}

bool BaseMechanics::isNewHorizonsResurrection() const
{
	return cb && cb->getBattle()
		&& newHorizonsMagic::resurrectionRestorationEnabled(cb->getBattle()->getMagicRules(), owner->getId());
}

bool BaseMechanics::usesNewHorizonsBerserkTargeting() const
{
	return owner->getId() == SpellID::BERSERK && !forceMassive && cb && cb->getBattle()
		&& newHorizonsMagic::berserkUsesSingleCreatureTarget(cb->getBattle()->getMagicRules());
}

bool BaseMechanics::usesNewHorizonsDispelRules() const
{
	return owner->getId() == SpellID::DISPEL && cb && cb->getBattle()
		&& newHorizonsMagic::dispelUsesNewHorizonsRules(cb->getBattle()->getMagicRules());
}

bool BaseMechanics::usesNewHorizonsTemporaryMagicDispel() const
{
	return owner->getId() == SpellID::DISPEL && cb && cb->getBattle()
		&& newHorizonsMagic::dispelRemovesTemporaryMagicalEffectsOnly(cb->getBattle()->getMagicRules());
}

SpellID BaseMechanics::getCureAffliction() const
{
	return cureAffliction;
}

std::string BaseMechanics::getCurePhysicalAffliction() const
{
	return curePhysicalAffliction;
}

std::vector<std::pair<int32_t, SpellID>> BaseMechanics::getPurifyChoices() const
{
	return purifyChoices;
}

bool BaseMechanics::isMassSlow() const
{
	return massSlow;
}

int32_t BaseMechanics::getShadowGiftSacrificePercent() const
{
	return shadowGiftSacrificePercent;
}

bool BaseMechanics::isNewHorizonsStormOfDaggers() const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	return battleState
		&& owner->getJsonKey() == newHorizonsMagic::STORM_OF_DAGGERS_SPELL
		&& newHorizonsMagic::rulesActive(battleState->getMagicRules())
		&& battleState->getMagicRules()["rulesetVersion"].Integer()
			>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
		&& newHorizonsMagic::spellAllowedBySavedRoster(battleState->getMagicRules(), owner->getId());
}

bool BaseMechanics::setStormOfDaggersTargetCount(int32_t selectedTargetCount)
{
	if(!isNewHorizonsStormOfDaggers() || selectedTargetCount < 1
		|| selectedTargetCount > newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS)
		return false;

	stormOfDaggersTargetCount = selectedTargetCount;
	return true;
}



AutomaticHeroCastReceipt BaseMechanics::getAutomaticHeroCast() const
{
	return automaticHeroCast;
}

bool BaseMechanics::usesNewHorizonsMagic() const
{
	return cb->getBattle() && newHorizonsMagic::rulesActive(cb->getBattle()->getMagicRules());
}

bool BaseMechanics::usesNewHorizonsMagicV3() const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	return battleState && newHorizonsMagic::rulesActive(battleState->getMagicRules())
		&& battleState->getMagicRules()["rulesetVersion"].Integer()
			== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
}

bool BaseMechanics::usesNewHorizonsQuicksandSelectedPlacement() const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	return battleState && newHorizonsMagic::quicksandSelectedPlacementEnabled(
		battleState->getMagicRules(), getSpellId());
}

bool BaseMechanics::usesNewHorizonsMultiplicativeMDR() const
{
	return cb && cb->battleUsesNewHorizonsMultiplicativeMDR();
}

bool BaseMechanics::isMagicMirror() const
{
	return mode == Mode::MAGIC_MIRROR;
}

PlayerColor BaseMechanics::getCasterColor() const
{
	return caster->getCasterOwner();
}

const CGHeroInstance * BaseMechanics::getHeroCaster() const
{
	return caster->getHeroCaster();
}

const battle::Unit * BaseMechanics::getUnitCaster() const
{
	if (caster->getHeroCaster() != nullptr)
		return nullptr;
	return battle()->battleGetUnitByID(static_cast<uint32_t>(caster->getCasterUnitId()));
}

std::vector<AimType> BaseMechanics::getTargetTypes() const
{
	if(usesNewHorizonsEarthquake())
		return {AimType::LOCATION};
	std::vector<AimType> ret;

	auto spellTargetType = usesNewHorizonsBerserkTargeting()
		? AimType::CREATURE : owner->getTargetType();

	if(isMassive())
		spellTargetType = AimType::NOTHING;
	else if(spellTargetType == AimType::OBSTACLE)
		spellTargetType = AimType::LOCATION;

	ret.push_back(spellTargetType);

	return ret;
}

const scripting::Service * BaseMechanics::scripts() const
{
	return LIBRARY->scripts(); //todo: redirect
}

const Service * BaseMechanics::spells() const
{
	return LIBRARY->spells(); //todo: redirect
}

const CBattleInfoCallback * BaseMechanics::battle() const
{
	return cb;
}

BattleID BaseMechanics::getBattleID() const
{
	return cb->getBattle()->getBattleID();
}

} //namespace spells

///IAdventureSpellMechanics
IAdventureSpellMechanics::IAdventureSpellMechanics(const CSpell * s)
	: owner(s)
{
}

std::unique_ptr<IAdventureSpellMechanics> IAdventureSpellMechanics::createMechanics(const CSpell * s)
{
	if (s->isCombat())
		return nullptr;

	return std::make_unique<AdventureSpellMechanics>(s);
}
