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
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsImplosion.h"
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
#include "../CStack.h"
#include "../CCreatureHandler.h"
#include "../mapObjects/army/CStackBasicDescriptor.h"
#include "../mapObjects/CGTownInstance.h"
#include "../bonuses/Bonus.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/HeroActionAllowanceState.h"
#include "../battle/HeroCommand.h"
#include "../battle/IBattleState.h"
#include "../battle/AlternatingHeroActionState.h"
#include "../battle/NewHorizonsDivineMandate.h"
#include "../battle/NewHorizonsWarcasting.h"
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
constexpr std::string_view HAVOC_MINE_LAYER_PERK_ID = "new-horizons:havocMagic.mineLayer";
constexpr int HAVOC_MINE_LAYER_ADDITIONAL_MINES = 1;

int64_t multiplyDivideFloor(int64_t value, uint64_t multiplier, int64_t divisor)
{
	// value is smaller than divisor. This bitwise quotient/remainder loop avoids
	// forming value * multiplier, which may overflow even when the final quotient
	// is small. The coefficient/Warcasting/Empower/specialty product is bounded
	// below 2^42. Specialty scaling may make the denominator as large as
	// INT32_MAX * 1e8 * 20, so keep the intermediate remainder unsigned.
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

int64_t checkedStormMultiply(int64_t left, int64_t right)
{
	if(left < 0 || right < 0
		|| (right != 0 && left > std::numeric_limits<int64_t>::max() / right))
		throw std::overflow_error("Storm of Daggers damage pool overflows");
	return left * right;
}

int64_t stormOfDaggersRoundedDamage(int64_t base, int64_t powerCoefficient, int32_t effectPower,
	int32_t powerDivisor, int32_t spellPowerCoefficientBasisPoints, int32_t warcastingBonusPercent,
	int32_t empowerSpellBonusPercent, int32_t selectedTargetCount, bool total)
{
	if(base < 0 || powerCoefficient < 0 || effectPower < 0 || powerDivisor <= 0
		|| spellPowerCoefficientBasisPoints < 0 || spellPowerCoefficientBasisPoints > 100000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000
		|| selectedTargetCount < 1
		|| selectedTargetCount > newHorizonsMagic::STORM_OF_DAGGERS_MAX_TARGETS)
		return 0;

	// Keep the authored fixed base and the full rational Spell Power component
	// together until the final displayed-total or equal-target rounding. The
	// generic direct-damage path truncates at the pool boundary, which differs
	// from the Storm table at positive half values (for example, SP=1 yields 47).
	const int64_t powerDenominator = checkedStormMultiply(
		checkedStormMultiply(powerDivisor, 1000000), 100);
	int64_t spellPowerNumerator = checkedStormMultiply(powerCoefficient, effectPower);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator, spellPowerCoefficientBasisPoints);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator,
		100LL + warcastingBonusPercent);
	spellPowerNumerator = checkedStormMultiply(spellPowerNumerator,
		100LL + empowerSpellBonusPercent);
	const int64_t fixedNumerator = checkedStormMultiply(base, powerDenominator);
	if(spellPowerNumerator > std::numeric_limits<int64_t>::max() - fixedNumerator)
		throw std::overflow_error("Storm of Daggers damage pool overflows");
	const int64_t poolNumerator = fixedNumerator + spellPowerNumerator;

	const int64_t multiplierPercent = 100
		+ newHorizonsMagic::STORM_OF_DAGGERS_EXTRA_TARGET_DAMAGE_PERCENT * (selectedTargetCount - 1);
	const int64_t finalDivisor = (total ? 100LL : 100LL * selectedTargetCount);
	const int64_t denominator = checkedStormMultiply(powerDenominator, finalDivisor);
	const int64_t scaledNumerator = checkedStormMultiply(poolNumerator, multiplierPercent);
	if(scaledNumerator > std::numeric_limits<int64_t>::max() - denominator / 2)
		throw std::overflow_error("Storm of Daggers damage pool overflows");

	// The authored reference values use ordinary positive half-up rounding for
	// both the displayed total and the equal per-stack share, without first
	// truncating a fractional Spell Power term at the pool boundary.
	return (scaledNumerator + denominator / 2) / denominator;
}
}

int64_t scaleWarcastingSpellPowerComponent(const int64_t numerator, const int64_t divisor, const int32_t bonusPercent)
{
	// The ranked caller multiplies an int32 spell divisor by at most 100.
	// Keeping that bound also makes the quotient/remainder doubling below safe.
	constexpr int64_t MAX_SCALED_DIVISOR = static_cast<int64_t>(std::numeric_limits<int32_t>::max()) * 100;
	if(numerator < 0 || divisor <= 0 || divisor > MAX_SCALED_DIVISOR || bonusPercent < 0)
		throw std::invalid_argument("Invalid Warcasting spell component inputs");
	if(numerator == 0)
		return 0;

	const int64_t denominator = static_cast<int64_t>(divisor) * 100;
	const uint64_t multiplier = static_cast<uint64_t>(100LL + bonusPercent);
	const int64_t whole = numerator / denominator;
	const int64_t remainder = numerator % denominator;
	const int64_t maximum = std::numeric_limits<int64_t>::max();
	if(whole > maximum / static_cast<int64_t>(multiplier))
		throw std::overflow_error("Warcasting spell component overflows");

	const int64_t scaledWhole = whole * static_cast<int64_t>(multiplier);
	const int64_t scaledRemainder = multiplyDivideFloor(remainder, multiplier, denominator);
	if(scaledWhole > maximum - scaledRemainder)
		throw std::overflow_error("Warcasting spell component overflows");
	return scaledWhole + scaledRemainder;
}

int64_t scaleSpellPowerComponentWithCoefficientBasisPoints(const int64_t numerator, const int32_t divisor,
	const int32_t coefficientBasisPoints, const int32_t warcastingBonusPercent,
	const int32_t empowerSpellBonusPercent, const int32_t damageSpecialtyPercent)
{
	if(numerator < 0 || divisor <= 0 || coefficientBasisPoints < 0 || coefficientBasisPoints > 100000
		|| warcastingBonusPercent < 0 || warcastingBonusPercent > 1000
		|| empowerSpellBonusPercent < 0 || empowerSpellBonusPercent > 1000
		|| (damageSpecialtyPercent != 0 && damageSpecialtyPercent != 15 && damageSpecialtyPercent != 20))
		throw std::invalid_argument("Invalid Spell Power basis-point coefficient inputs");
	if(coefficientBasisPoints == 0 || numerator == 0)
		return 0;

	// Keep coefficient, Warcasting, Empower Spell, and the divisor in one
	// rational expression until the final floor. Quotient/remainder scaling
	// avoids multiplying a potentially large Spell Power numerator directly.
	int64_t denominator = static_cast<int64_t>(divisor) * 100000000;
	uint64_t multiplier = static_cast<uint64_t>(coefficientBasisPoints)
		* (100LL + warcastingBonusPercent) * (100LL + empowerSpellBonusPercent);
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
		coefficientBasisPoints, getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), damageSpecialtyPercent);
}

int64_t Mechanics::getHexOfPainFlatDamage() const
{
	if(getSpellId().toSpell()->getJsonKey() != "new-horizons:hexOfPain")
		return 0;
	const auto * hero = getHeroCaster();
	return newHorizonsMagic::hexOfPainFlatDamage(getEffectPower(), getSpellPowerCoefficientBasisPoints(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(),
		hero ? hero->getDamageSpellSpecialtyBonusPercent(getSpellId()) : 0,
		usesNewHorizonsMagicV3() && hero
			&& hero->hasActivePerk("new-horizons:shadowMagic", "new-horizons:shadowMagic.painweaver"));
}

int64_t Mechanics::getPlagueTickDamage() const
{
	if(getSpellId().toSpell()->getJsonKey() != "new-horizons:plague")
		return 0;
	const auto * hero = getHeroCaster();
	// Preserve Plague's authored initial snapshot: coefficient, not an extra
	// Warcasting/Empower application. Children reuse this exact captured value.
	return newHorizonsMagic::plagueTickDamage(getEffectPower(), getSpellPowerCoefficientBasisPoints(),
		hero ? hero->getDamageSpellSpecialtyBonusPercent(getSpellId()) : 0);
}

int64_t Mechanics::getPhantomArmyIntegrity(const battle::Unit * source) const
{
	if(!source || getSpellId().toSpell()->getJsonKey() != "new-horizons:phantomArmy")
		return 0;
	const auto * hero = getHeroCaster();
	return std::max<int64_t>(1, newHorizonsSorcery::phantomArmyIntegrityWithModifiers(
		source->getAvailableHealth(), std::max(0, getEffectPower()),
		hero && hero->hasActivePerk("new-horizons:sorceryMagic", "new-horizons:sorceryMagic.illusionist"),
		getSpellPowerCoefficientBasisPoints(), getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(),
		hero ? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0));
}

int32_t Mechanics::getFrailtyDefenseLossBasisPoints() const
{
	if(getSpellId().toSpell()->getJsonKey() != newHorizonsMagic::SHADOW_FRAILTY_SPELL)
		return 0;
	constexpr int32_t BASE_LOSS = 1000;
	constexpr int32_t POWER_LOSS_PER_POINT = 5;
	constexpr int32_t PER_CAST_CAP = 2000;
	constexpr int32_t WITHERING_TOUCH_LOSS = 500;
	const auto * hero = getHeroCaster();
	const auto powerLoss = scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
		static_cast<int64_t>(std::max(0, getEffectPower())) * POWER_LOSS_PER_POINT, 1,
		getSpellPowerCoefficientBasisPoints(), hero ? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0);
	const int32_t result = static_cast<int32_t>(std::min<int64_t>(PER_CAST_CAP, BASE_LOSS + powerLoss));
	if(hero && hero->hasActivePerk(std::string(newHorizonsMagic::SHADOW_MAGIC_SKILL),
		std::string(newHorizonsMagic::SHADOW_WITHERING_TOUCH_PERK)))
		return result + WITHERING_TOUCH_LOSS;
	return result;
}

int64_t Mechanics::getGuardianSpiritHitPoints(const battle::Unit * target) const
{
	if(getSpellId().toSpell()->getJsonKey() != "new-horizons:guardianSpirit")
		return 0;
	const auto * hero = getHeroCaster();
	const int specialtyPercent = hero ? hero->getNonDamageSpellSpecialtyBonusPercent(getSpellId()) : 0;
	auto powerTerm = scaleRecipientSpellPowerComponentWithSpecialty(
		2LL * getEffectPower(), 1, target, specialtyPercent);
	if(hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"))
		powerTerm = powerTerm * 120 / 100;
	auto pool = 50 + powerTerm;
	if(hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.guardian"))
		pool = pool * 125 / 100;
	return pool;
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

int32_t Mechanics::getSpellPowerCoefficientBasisPoints() const
{
	const auto * battleCallback = battle();
	const auto * battleState = battleCallback ? battleCallback->getBattle() : nullptr;
	if(!battleState)
		return 10000;

	return newHorizonsMagic::spellPowerCoefficientBasisPoints(
		battleState->getMagicRules(), getHeroCaster(), getSpellId(), getCastSpellPowerComponentBonusPercent());
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
		getHeroCaster(), getSpellId(), getEffectPower(), getEffectPowerDivisor(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), getCastSpellPowerComponentBonusPercent());
	return patchCount.value_or(0);
}

int32_t Mechanics::getNewHorizonsLandMinePatchCount() const
{
	if(!usesNewHorizonsMagic() || !newHorizonsMagic::isLandMine(getSpellId()))
		return 0;

	const int32_t count = newHorizonsMagic::landMineHexCount(
		getEffectPower(), getSpellPowerCoefficientBasisPoints());
	const auto * hero = getHeroCaster();
	if(hero && hero->hasActivePerk(std::string(newHorizonsMagic::HAVOC_MAGIC_SKILL),
		std::string(HAVOC_MINE_LAYER_PERK_ID)))
		return count + HAVOC_MINE_LAYER_ADDITIONAL_MINES;

	return count;
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
	const auto divisor = getNewHorizonsEarthquakeParameter("powerPerSection");
	const auto extra = scaleSpellPowerComponentWithCoefficientBasisPoints(
		std::max<int64_t>(0, getEffectPower()), divisor, getSpellPowerCoefficientBasisPoints());
	return getNewHorizonsEarthquakeParameter("baseSections") + static_cast<int32_t>(std::min<int64_t>(extra,
		getNewHorizonsEarthquakeParameter("maxSections") - getNewHorizonsEarthquakeParameter("baseSections")));
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

	const int64_t rawDamage = std::max<int64_t>(0, getEffectValue());
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
		getSpellPowerCoefficientBasisPoints(), getWarcastingBonusPercent(), getEmpowerSpellBonusPercent());
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

static bool spellsShareSchool(const CSpell * first, const CSpell * second)
{
	if(!first || !second)
		return false;
	bool shared = false;
	first->forEachSchool([&](const SpellSchool & school, bool & stop)
	{
		if(second->hasSchool(school))
		{
			shared = true;
			stop = true;
		}
	});
	return shared;
}

BattleCast::BattleCast(const CBattleInfoCallback * cb_, const Caster * caster_, const Mode mode_, const CSpell * spell_):
	spell(spell_),
	cb(cb_),
	caster(caster_),
	mode(mode_)
{
}

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

bool BattleCast::isMetamagicFollowup() const
{
	return metamagicFollowup;
}

bool BattleCast::isMetamagicGrand() const
{
	return metamagicGrand;
}

uint32_t BattleCast::getMetamagicTargetUnitId() const
{
	return metamagicTargetUnitId;
}

int32_t BattleCast::getMetamagicManaRefund() const
{
	return metamagicManaRefund;
}

BattleCast::OptionalValue64 BattleCast::getEffectValue() const
{
	return effectValue;
}

BattleSide BattleCast::getCounterspellSide() const
{
	return counterspellSide;
}

bool BattleCast::isCounterspellNegated() const
{
	return counterspellNegated;
}

int32_t BattleCast::getCounterspellManaSpent() const
{
	return counterspellManaSpent;
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

void BattleCast::setMetamagicFollowup(bool value)
{
	metamagicFollowup = value;
}

void BattleCast::setMetamagicGrand(bool value)
{
	metamagicGrand = value;
}

void BattleCast::setMetamagicTargetUnitId(uint32_t value)
{
	metamagicTargetUnitId = value;
}

void BattleCast::setMetamagicManaRefund(int32_t value)
{
	metamagicManaRefund = value;
}

void BattleCast::setEffectValue(BattleCast::Value64 value)
{
	effectValue = std::make_optional(value);
}

void BattleCast::setCounterspell(BattleSide wardSide, bool negated, int32_t manaSpent)
{
	counterspellSide = wardSide;
	counterspellNegated = negated;
	counterspellManaSpent = manaSpent;
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

int32_t Mechanics::getSummonedCreatureMaxHealth(const Creature * creature, const bool natureSummoned) const
{
	if(!creature)
		return 1;

	const auto * engineCreature = dynamic_cast<const CCreature *>(creature);
	if(!engineCreature)
		return std::max(1, static_cast<int32_t>(creature->getMaxHealth()));

	CStackBasicDescriptor descriptor(engineCreature, 1);
	CStack hypothetical(&descriptor, getCasterColor(), -1, getCasterSide(), SlotID::SUMMONED_SLOT_PLACEHOLDER, true);
	hypothetical.summoned = true;
	hypothetical.natureSummoned = natureSummoned;

	// A hypothetical node inherits the same source bonuses and applies the same
	// limiter pipeline as a real CStack. Source-only links avoid registering a
	// live child or invalidating caches on the hero/creature graph.
	const auto * battleState = battle() ? battle()->getBattle() : nullptr;
	const auto * army = battleState ? battleState->getSideArmy(getCasterSide()) : nullptr;
	if(army)
		hypothetical.attachToSource(*army);
	else if(const auto * hero = getHeroCaster())
		hypothetical.attachToSource(*hero);
	hypothetical.attachToSource(*engineCreature);

	return std::max(1, hypothetical.valOfBonuses(BonusType::STACK_HEALTH));
}

BaseMechanics::BaseMechanics(const IBattleCast * event):
	owner(event->getSpell()),
	mode(event->getMode()),
	forceMassive(event->isForceMassive()),
	cb(event->getBattle())
{
	caster = event->getCaster();
	metamagicFollowup = event->isMetamagicFollowup();
	massSlow = event->getMassSlow();
	shadowGiftSacrificePercent = event->getShadowGiftSacrificePercent();

	casterSide = cb->playerToSide(caster->getCasterOwner());
	if(mode == Mode::HERO && dynamic_cast<const CGHeroInstance *>(caster)
		&& (casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER))
	{
		const auto * battleInfo = cb->getBattle();
		const auto * hero = dynamic_cast<const CGHeroInstance *>(caster);
		std::optional<HeroActionAllowanceState::Selection> spellAllowance;
		if(!event->isMetamagicFollowup() && owner)
			spellAllowance = cb->battleGetSpellActionAllowance(casterSide, owner->getId());
		bool spendsHeroAllowance = !event->isMetamagicFollowup();
		const int32_t battleRound = battleInfo ? battleInfo->getRound() : -1;
		if(battleInfo && !event->isMetamagicFollowup() && battleRound >= 0
			&& heroCommands::supportedByRules(battleInfo->getHeroCommandRules(), HeroCommand::CHARGE))
		{
			spendsHeroAllowance = spellAllowance
				&& spellAllowance->allowance == HeroActionAllowanceState::AllowanceKind::HERO;
		}
		if(battleInfo && hero && owner && battleInfo->getSideHero(casterSide) == hero
			&& spellAllowance
			&& spellAllowance->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
			&& spellAllowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE)
			consecratedCastingBonusPercent =
				newHorizonsDivineMandate::consecratedCastingBonusPercent(hero);
		if(battleInfo && hero && spellAllowance
			&& spellAllowance->source == HeroActionAllowanceState::GrantSource::DIVINE_MANDATE
			&& spellAllowance->allowance == HeroActionAllowanceState::AllowanceKind::SPELL
			&& newHorizonsDivineMandate::hasCrownAndAltarPerk(hero))
		{
			const auto & grants = battleInfo->getHeroActionAllowances(casterSide).grants;
			const auto grant = std::find_if(grants.begin(), grants.end(), [&spellAllowance](const auto & entry)
				{ return entry.id == spellAllowance->grantId; });
			if(grant != grants.end())
				crownAndAltarRecipientUnitIds = grant->divineMandateRecipients;
		}
		if(battleInfo && spendsHeroAllowance
			&& newHorizonsWarcasting::enabled(battleInfo->getMagicRules()))
			warcastingBonusPercent = newHorizonsWarcasting::spellBonus(hero,
				battleInfo->getWarcastingState(casterSide), battleInfo->getRound());
		combatCastingEligible = warcastingBonusPercent > 0 && hero
			&& hero->hasActivePerk("new-horizons:warcasting", "new-horizons:warcasting.combatCasting");
		overwhelmingFormulaEligible = battleInfo && hero && owner
			&& (owner->isNegative() || newHorizonsMagic::isLandMine(owner->getId())
				|| newHorizonsMagic::isFireWall(owner->getId()))
			&& owner->isMagical() && newHorizonsMagic::rulesActive(battleInfo->getMagicRules())
			&& battleInfo->getSideHero(casterSide) == hero
			&& hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
				"new-horizons:spellcraft.overwhelmingFormula");

		if(battleInfo && hero && battleInfo->getSideHero(casterSide) == hero
			&& !battleInfo->hasCompletedHeroSpellCast(casterSide)
			&& hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
				std::string(newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS)))
			arcaneFocusBonusPercent = newHorizonsMagic::SPELLCRAFT_ARCANE_FOCUS_BONUS_PERCENT;

		if(battleInfo && hero && owner && battleInfo->getSideHero(casterSide) == hero
			&& battleRound >= 0 && battleInfo->getSpellResponseState(casterSide).isReadyAt(battleRound)
			&& hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
				std::string(newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE)))
			counterpressureBonusPercent = newHorizonsMagic::SPELLCRAFT_COUNTERPRESSURE_BONUS_PERCENT;

		if(battleInfo && hero && owner && battleInfo->getSideHero(casterSide) == hero)
			crossSchoolFormulaBonusPercent = newHorizonsCrossSchoolFormula::bonusPercent(*battleInfo, casterSide, owner->getId());

		const int spellLevel = battleInfo && owner ? cb->battleGetSpellLevel(owner->getId()) : 0;
		if(battleInfo && hero && battleInfo->getSideHero(casterSide) == hero
			&& (spellLevel == 4 || spellLevel == 5)
			&& !battleInfo->hasCompletedHeroSpellLevel(casterSide, 4)
			&& !battleInfo->hasCompletedHeroSpellLevel(casterSide, 5)
			&& hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
				std::string(newHorizonsMagic::SPELLCRAFT_GRAND_FORMULA)))
			grandFormulaMultiplierPercent = newHorizonsMagic::SPELLCRAFT_GRAND_FORMULA_MULTIPLIER_PERCENT;
	}

	{
		auto value = event->getSpellLevel();
		rangeLevel = value.value_or(caster->getSpellSchoolLevel(owner));
		vstd::abetween(rangeLevel, 0, 3);
	}
	{
		auto value = event->getSpellLevel();
		effectLevel = value.value_or(caster->getEffectLevel(owner));
		vstd::abetween(effectLevel, 0, 3);
	}
	{
		auto value = event->getEffectPower();
		effectPower = value.value_or(caster->getEffectPower(owner));
		vstd::amax(effectPower, 0);

		// Inferno's New Horizons Brimstone Stormclouds are a siege-only
		// Spell Power bonus for the defending hero.  Keep it in the battle
		// mechanics boundary so it affects every ordinary spell effect while
		// never leaking onto an adventure-map hero or an attacking hero.
		const auto *heroCaster = caster->getHeroCaster();
		const auto *defendedTown = cb->battleGetDefendedTown();
		if(heroCaster && newHorizonsMagic::rulesActive(heroCaster->getMagicRules()) && defendedTown
			&& defendedTown->getFactionID() == FactionID::INFERNO
			&& defendedTown->hasBuilt(BuildingID::SPECIAL_2)
			&& cb->battleGetFightingHero(BattleSide::DEFENDER) == heroCaster)
			effectPower += 20;
	}
	extendSpellEligible = mode == Mode::HERO && cb && cb->getBattle()
		&& (casterSide == BattleSide::ATTACKER || casterSide == BattleSide::DEFENDER)
		&& caster == cb->getBattle()->getSideHero(casterSide)
		&& newHorizonsSpellcraft::extendAvailable(*cb->getBattle(), casterSide, *owner, effectLevel);
	const auto * concentrationHero = dynamic_cast<const CGHeroInstance *>(caster);
	concentrationBonusPercent = event->isConcentrated() && mode == Mode::HERO && concentrationHero
		&& cb && cb->getBattle() && caster == cb->getBattle()->getSideHero(casterSide)
		&& newHorizonsMagic::rulesActive(cb->getBattle()->getMagicRules())
		&& newHorizonsMagic::spellAllowedBySavedRoster(cb->getBattle()->getMagicRules(), owner->getId())
		&& concentrationHero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			std::string(newHorizonsSpellcraft::CONCENTRATION)) ? 15 : 0;
	{
		auto value = event->getEffectDuration();
		effectDuration = value.value_or(caster->getEnchantPower(owner));
		vstd::amax(effectDuration, 0); //???
		if(value.has_value() && extendSpellEligible && effectDuration < std::numeric_limits<decltype(effectDuration)>::max())
			++effectDuration;
		if(!value.has_value())
		{
			const auto * heroCaster = caster->getHeroCaster();
			const auto * defendedTown = cb->battleGetDefendedTown();
			const auto * battleState = cb->getBattle();
			const SpellID familyID = battleState
				? newHorizonsMagic::spellVariantBase(battleState->getMagicRules(), owner->getId())
				: owner->getId();
			const bool v3Slow = mode == Mode::HERO && heroCaster && battleState
				&& familyID == SpellID::SLOW
				&& newHorizonsMagic::rulesActive(battleState->getMagicRules())
				&& battleState->getMagicRules()["rulesetVersion"].Integer()
					== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
			if(!v3Slow && mode == Mode::HERO && heroCaster && battleState
				&& newHorizonsMagic::rulesActive(battleState->getMagicRules())
				&& battleState->getMagicRules()["rulesetVersion"].Integer()
					== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
				&& battleState->getMagicRules().Struct().contains("spellcraftEfficiencyPercent"))
			{
				// Hero timed effects use Spell Power as their ordinary duration
				// term. Scale that term alone; the caster's common and spell-specific
				// SPELL_DURATION bonuses remain additive and are applied after it.
				const int32_t divisor = std::max<int32_t>(1, caster->getEffectPowerDivisor(owner));
				const int64_t rawSpellPower = std::max<int32_t>(0, caster->getEffectPower(owner));
				const int64_t originalPowerTerm = std::max<int64_t>(1, rawSpellPower / divisor);
				const int64_t flatDuration = static_cast<int64_t>(effectDuration) - originalPowerTerm;
				const int specialtyPercent = familyID == SpellID::HASTE
					? heroCaster->getNonDamageSpellSpecialtyBonusPercent(familyID) : 0;
				const int64_t scaledPowerTerm = std::max<int64_t>(1,
					scaleDamageSpellPowerComponentWithCoefficientBasisPoints(rawSpellPower, divisor,
						getSpellPowerCoefficientBasisPoints(), specialtyPercent));
				const int64_t duration = scaledPowerTerm + flatDuration;
				effectDuration = static_cast<decltype(effectDuration)>(std::clamp<int64_t>(duration, 0,
					std::numeric_limits<decltype(effectDuration)>::max()));
			}
			const bool v3Bless = mode == Mode::HERO && heroCaster && familyID == SpellID::BLESS
				&& cb->getBattle() && newHorizonsMagic::rulesActive(cb->getBattle()->getMagicRules())
				&& cb->getBattle()->getMagicRules()["rulesetVersion"].Integer()
					== newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION;
			const bool infernoDurationBonus = heroCaster
				&& newHorizonsMagic::rulesActive(heroCaster->getMagicRules()) && defendedTown
				&& defendedTown->getFactionID() == FactionID::INFERNO
				&& defendedTown->hasBuilt(BuildingID::SPECIAL_2)
				&& cb->battleGetFightingHero(BattleSide::DEFENDER) == heroCaster;

			const int bonus = newHorizonsMagic::spellDurationBonus(
				dynamic_cast<const CGHeroInstance *>(caster), familyID);
			if(v3Slow)
			{
				int64_t duration = newHorizonsMagic::SLOW_BASE_DURATION_ROUNDS;
				duration += heroCaster->valOfBonuses(BonusType::SPELL_DURATION, BonusSubtypeID());
				duration += heroCaster->valOfBonuses(BonusType::SPELL_DURATION,
					BonusSubtypeID(SpellID(SpellID::SLOW)));
				duration += bonus;
				if(infernoDurationBonus)
					duration += 20;
				effectDuration = static_cast<decltype(effectDuration)>(std::clamp<int64_t>(duration, 0,
					std::numeric_limits<decltype(effectDuration)>::max()));
			}
			else if(v3Bless)
			{
				const int specialtyPercent = heroCaster->getNonDamageSpellSpecialtyBonusPercent(familyID);
				const int64_t spellPowerTerm = scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
					effectPower, newHorizonsMagic::BLESS_SPELL_POWER_DURATION_DIVISOR,
					getSpellPowerCoefficientBasisPoints(), specialtyPercent);
				int64_t duration = newHorizonsMagic::blessDurationFromPowerTerm(spellPowerTerm);
				duration += heroCaster->valOfBonuses(BonusType::SPELL_DURATION, BonusSubtypeID());
				duration += heroCaster->valOfBonuses(BonusType::SPELL_DURATION,
					BonusSubtypeID(SpellID(SpellID::BLESS)));
				duration += bonus;
				if(infernoDurationBonus)
					duration += 20;
				if(newHorizonsMagic::hasBenedictionPerk(heroCaster))
					++duration;
				effectDuration = static_cast<decltype(effectDuration)>(std::clamp<int64_t>(duration, 0,
					std::numeric_limits<decltype(effectDuration)>::max()));
			}
			else
			{
				if(infernoDurationBonus && effectDuration <= std::numeric_limits<decltype(effectDuration)>::max() - 20)
					effectDuration += 20;
				if(effectDuration <= std::numeric_limits<decltype(effectDuration)>::max() - bonus)
					effectDuration += bonus;
			}

			// Echoed Duration applies only to an additional cast and only when
			// the spell did not provide an explicit duration override.
			effectDuration = adjustEffectDuration(effectDuration);
		}
	}
	overcharge = event->getOvercharge().value_or(0);
	cureAffliction = event->getCureAffliction();
	curePhysicalAffliction = event->getCurePhysicalAffliction();
	counterspellSide = event->getCounterspellSide();
	counterspellNegated = event->isCounterspellNegated();
	counterspellManaSpent = event->getCounterspellManaSpent();
	selectiveDispel = event->getSelectiveDispel();
	metamagicGrand = event->isMetamagicGrand();
	metamagicTargetUnitId = event->getMetamagicTargetUnitId();
	metamagicManaRefund = event->getMetamagicManaRefund();
	forceNonSmartTargeting = event->getForceNonSmartTargeting();
	if(metamagicFollowup && mode == Mode::HERO)
	{
		const auto * hero = dynamic_cast<const CGHeroInstance *>(caster);
		const auto & sequence = cb->getBattle()->getMetamagicSequenceSpells(casterSide);
		// BattleSpellCast is applied before the spell effects.  Applying the
		// follow-up packet consumes the last pending leg and clears the live
		// sequence, so retain the first target and Focused Pairing eligibility
		// while the authoritative pre-cast state is still available.
		metamagicFirstTargetUnitId = cb->getBattle()->getMetamagicFirstTargetUnitId(casterSide);
		metamagicFocusedPairingEligible = newHorizonsMagic::hasMetamagicPerk(
			hero, newHorizonsMagic::METAMAGIC_FOCUSED_PAIRING)
			&& metamagicTargetUnitId != newHorizonsMagic::INVALID_METAMAGIC_TARGET
			&& metamagicFirstTargetUnitId != newHorizonsMagic::INVALID_METAMAGIC_TARGET
			&& metamagicTargetUnitId == metamagicFirstTargetUnitId;
		int powerBonus = 0;
		if(newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_SPELL_SEQUENCING)
			&& !sequence.empty() && !spellsShareSchool(sequence.front().toSpell(), owner))
			powerBonus += 15;
		if(newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_SPLIT_FOCUS)
			&& event->getMetamagicTargetUnitId() != newHorizonsMagic::INVALID_METAMAGIC_TARGET
			&& cb->getBattle()->getMetamagicFirstTargetUnitId(casterSide) != newHorizonsMagic::INVALID_METAMAGIC_TARGET
			&& event->getMetamagicTargetUnitId() != cb->getBattle()->getMetamagicFirstTargetUnitId(casterSide))
			powerBonus += 10;
		if(newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_SPELL_ECHO)
			&& !sequence.empty() && owner->getId() == sequence.front())
			powerBonus += 25;
		const bool distinctSequence = std::all_of(sequence.begin(), sequence.end(), [&sequence](const SpellID & spellId)
		{
			return std::count(sequence.begin(), sequence.end(), spellId) == 1;
		});
		if(newHorizonsMagic::hasMetamagicPerk(hero, newHorizonsMagic::METAMAGIC_PERFECT_SEQUENCE)
			&& distinctSequence
			&& std::none_of(sequence.begin(), sequence.end(), [this](const SpellID & spellId)
			{
				return spellId == owner->getId();
			}))
			powerBonus += 20;
		if(powerBonus > 0)
			effectPower = effectPower * (100 + powerBonus) / 100;
	}
	if(newHorizonsMagic::hasStormcallerPerk(dynamic_cast<const CGHeroInstance *>(caster), owner))
		effectPower = effectPower * 115 / 100;
	{
		const auto value = event->getEffectValue();
		effectValueWasOverridden = value.has_value();
		const auto * reanimateBattle = cb->getBattle();
		const auto * reanimateHero = caster->getHeroCaster();
		if(reanimateBattle && reanimateHero && newHorizonsMagic::reanimateEnabled(
			reanimateBattle->getMagicRules(), owner->getId()))
		{
			// Re-animate historically derives its pool from raw hero Attributes,
			// ignoring generic effect-value overrides. Keep that contract while
			// sharing the same rational pool with its preview and specialty.
			effectValue = *newHorizonsMagic::reanimateBaseHealingPool(
				reanimateBattle->getMagicRules(), reanimateHero, owner->getId(),
				std::max(reanimateHero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 0),
				getCastSpellPowerComponentBonusPercent());
		}
		else if(value.has_value())
			effectValue = *value; // Explicit zero is an override, not absence.
		else if(const auto casterValue = caster->getEffectValue(owner); casterValue != 0)
			effectValue = casterValue; // Legacy numeric caster zero means absence.
		else
		{
		const auto * battle = cb->getBattle();
		const int spellPowerCoefficientBasisPoints = battle
			? newHorizonsMagic::spellPowerCoefficientBasisPoints(
				battle->getMagicRules(), caster->getHeroCaster(), owner->getId(), getCastSpellPowerComponentBonusPercent())
			: 10000;
		const auto * heroCaster = caster->getHeroCaster();
		const bool damageSpell = owner->isDamage() || usesNewHorizonsEarthquake();
		const int damagePerkBonusPercent = battle && damageSpell
			? newHorizonsMagic::spellPowerDamagePerkBonusPercent(battle->getMagicRules(), heroCaster, owner)
			: 0;
		const int damageSpecialtyPercent = damageSpell && heroCaster
			? heroCaster->getDamageSpellSpecialtyBonusPercent(owner->getId())
			: 0;
		const int damageCoefficientBasisPoints = damageSpell
			? spellPowerCoefficientBasisPoints * (100 + damagePerkBonusPercent) / 100
			: 10000;
		const int effectPowerCoefficientBasisPoints = damageSpell
			? damageCoefficientBasisPoints
			: spellPowerCoefficientBasisPoints;
			const int empowerBonusPercent = battle
				? newHorizonsMagic::empowerSpellBonusPercent(
					battle->getMagicRules(), caster->getHeroCaster(), owner->getId(), isMassSlow() ? 3 : 1)
				: 0;
			if(usesNewHorizonsEarthquake())
			{
				effectValue = getNewHorizonsEarthquakeParameter("baseDamage")
					+ scaleSpellPowerComponentWithCoefficientBasisPoints(
						static_cast<int64_t>(getNewHorizonsEarthquakeParameter("powerNumerator")) * effectPower,
						getNewHorizonsEarthquakeParameter("powerDivisor"),
						damageCoefficientBasisPoints);
			}
			else if(battle && owner->getJsonKey() == "new-horizons:hydrasVitality"
				&& newHorizonsMagic::rulesActive(battle->getMagicRules())
				&& battle->getMagicRules()["rulesetVersion"].Integer()
					>= newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION)
			{
				// Millionths of one percent preserve fractional School scaling
				// until the effect rounds the resulting creature HP. Capacity uses
				// the canonical raw attribute, not the legacy divisor.
				const auto * hero = caster->getHeroCaster();
				const int specialtyPercent = hero ? hero->getNonDamageSpellSpecialtyBonusPercent(owner->getId()) : 0;
				effectValue = std::min<int64_t>(50'000'000, 25'000'000
					+ scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
						150'000LL * std::max(effectPower, 0), 1, spellPowerCoefficientBasisPoints, specialtyPercent));
			}
			else if(battle && newHorizonsMagic::cureEnabled(battle->getMagicRules(), owner->getId()))
			{
				// The specialty scales only Cure's Spell-Power component. Its fixed
				// component and downstream target/effect modifiers retain their
				// existing stages in the usual heal.lua application path.
				const auto * hero = caster->getHeroCaster();
				const int specialtyPercent = hero
					? hero->getNonDamageSpellSpecialtyBonusPercent(owner->getId()) : 0;
				auto powerHealing = scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
					3LL * effectPower, 2, spellPowerCoefficientBasisPoints, specialtyPercent);
				if(hero && hero->hasActivePerk(
					"new-horizons:lightMagic", "new-horizons:lightMagic.healer"))
					powerHealing = powerHealing * 120 / 100;
				effectValue = 25 + powerHealing;
			}
			else if(battle && newHorizonsMagic::resurrectionRestorationEnabled(
				battle->getMagicRules(), owner->getId()))
			{
				// Both the fixed pool and the Spell Power-derived pool remain separate:
				// School/Spellcraft, shared cast modifiers and the specialty affect only
				// the raw Spell Power component.
				const auto * hero = caster->getHeroCaster();
				const int specialtyPercent = hero
					? hero->getNonDamageSpellSpecialtyBonusPercent(owner->getId()) : 0;
				effectValue = newHorizonsMagic::RESURRECTION_BASE_POOL_HP
					+ scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
						static_cast<int64_t>(newHorizonsMagic::RESURRECTION_SPELL_POWER_HP_PER_POINT)
							* std::max<int64_t>(effectPower, 0),
						1, spellPowerCoefficientBasisPoints, specialtyPercent);
			}
			else
			{
				const auto modifiers = newHorizonsMagic::magicArrowOverchargeModifiers(
					dynamic_cast<const CGHeroInstance *>(caster));
				std::optional<int64_t> magicArrowValue;
				if(battle && newHorizonsMagic::magicArrowOverchargeEnabled(battle->getMagicRules(), owner->getId()))
				{
					const int maximumOvercharge = newHorizonsMagic::magicArrowMaxOvercharge(
						battle->getMagicRules(), owner->getId(), effectPower, modifiers);
					if(getOvercharge() >= 0 && getOvercharge() <= maximumOvercharge)
					{
						const auto formula = newHorizonsMagic::spellDirectDamage(
							battle->getMagicRules(), owner->getJsonKey())
							.value_or(newHorizonsMagic::DirectDamageFormula{20, 20});
						const int64_t baseDamage = formula.evaluateBasisPoints(
							effectPower, getEffectPowerDivisor(), damageCoefficientBasisPoints, empowerBonusPercent,
							damageSpecialtyPercent);
						const int64_t overchargeMultiplier = 1000LL
							+ static_cast<int64_t>(modifiers.damagePercentTenths) * getOvercharge();
						magicArrowValue = baseDamage * overchargeMultiplier / 1000;
					}
				}
				if(magicArrowValue && warcastingBonusPercent > 0)
				{
					const auto formula = newHorizonsMagic::spellDirectDamage(battle->getMagicRules(), owner->getJsonKey())
						.value_or(newHorizonsMagic::DirectDamageFormula{20, 20});
					const int64_t powerNumerator = static_cast<int64_t>(formula.powerCoefficient) * effectPower;
					const int64_t baseDamage = formula.base
						+ scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
							powerNumerator, getEffectPowerDivisor(), damageCoefficientBasisPoints, damageSpecialtyPercent);
					const int64_t overchargeMultiplier = 1000LL
						+ static_cast<int64_t>(modifiers.damagePercentTenths) * getOvercharge();
					magicArrowValue = baseDamage * overchargeMultiplier / 1000;
				}
				std::optional<int64_t> savedValue;
				if(battle && !magicArrowValue)
				{
					if(const auto formula = newHorizonsMagic::spellDirectDamage(
						battle->getMagicRules(), owner->getJsonKey()))
						savedValue = formula->evaluateBasisPoints(
							effectPower, getEffectPowerDivisor(), damageCoefficientBasisPoints, empowerBonusPercent,
							damageSpecialtyPercent);
				}
				if(savedValue && warcastingBonusPercent > 0)
				{
					const auto formula = newHorizonsMagic::spellDirectDamage(battle->getMagicRules(), owner->getJsonKey());
					if(formula)
					{
						const int64_t powerNumerator = static_cast<int64_t>(formula->powerCoefficient) * effectPower;
						*savedValue = formula->base
							+ scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
								powerNumerator, getEffectPowerDivisor(), damageCoefficientBasisPoints, damageSpecialtyPercent);
					}
				}
				if(magicArrowValue)
					effectValue = *magicArrowValue;
				else if(savedValue)
					effectValue = *savedValue;
				else if(warcastingBonusPercent > 0 || effectPowerCoefficientBasisPoints != 10000
					|| empowerBonusPercent > 0 || damageSpecialtyPercent > 0)
				{
					const int64_t powerNumerator = static_cast<int64_t>(owner->getBasePower()) * effectPower;
					effectValue = owner->getLevelPower(effectLevel)
						+ scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
							powerNumerator, getEffectPowerDivisor(), effectPowerCoefficientBasisPoints, damageSpecialtyPercent);
				}
				else
					effectValue = owner->calculateRawEffectValue(effectLevel, effectPower, 1, getEffectPowerDivisor());
			}
		}
		vstd::amax(effectValue, 0);
	}
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
	if(!server || mode != Mode::HERO || !overwhelmingFormulaEligible || isCounterspellNegated()
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

JsonNode BaseMechanics::getCapturedMdrPenetration() const
{
	if(mode == Mode::PASSIVE)
		if(const auto * obstacle = dynamic_cast<const ObstacleCasterProxy *>(caster))
			return obstacle->getCapturedMdrPenetration();
	JsonNode result;
	const auto * hero = caster ? caster->getHeroCaster() : nullptr;
	if(mode != Mode::HERO || (!isNegativeSpell() && !overwhelmingFormulaEligible
		&& !newHorizonsNaturesWrath::enabled(*this)) || !hero
		|| !newHorizonsMagic::rulesActive(hero->getMagicRules()))
		return result;
	const auto add = [&result](int percent)
	{
		result["penetrations"].Vector().emplace_back(percent);
	};
	if(hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
		"new-horizons:spellcraft.spellPenetration"))
		add(20);
	if(newHorizonsMagic::hasAnnihilatorPerk(hero, owner))
		add(20);
	if(combatCastingEligible)
		add(15);
	if(overwhelmingFormulaEligible && overwhelmingFormulaToken != 0)
	{
		result["overwhelmingFormulaSide"].Integer() = static_cast<int32_t>(casterSide);
		// A decimal string survives Lua's double-only numerical representation.
		result["overwhelmingFormulaToken"].String() = std::to_string(overwhelmingFormulaToken);
	}
	else if(overwhelmingFormulaEligible && cb && cb->getBattle()
		&& cb->getBattle()->getOverwhelmingFormulaState(casterSide).winningCastToken == 0
		&& cb->getBattle()->getOverwhelmingFormulaState(casterSide).lastCandidateCastToken
			!= std::numeric_limits<uint64_t>::max())
	{
		// Unaccepted UI prediction is read-only. Execution registers above before
		// preparing effects, so this temporary contribution is never saved on one.
		add(50);
	}
	if(metamagicFollowup && metamagicFocusedPairingEligible
		&& metamagicFirstTargetUnitId != std::numeric_limits<uint32_t>::max())
	{
		result["focusedTargetUnitId"].Integer() = metamagicFirstTargetUnitId;
		result["focusedPenetrationPercent"].Integer() = 20;
	}
	return result;
}

int64_t Mechanics::adjustDirectCreatureActivationDamage(const int64_t damage) const
{
	if(damage <= 0)
		return 0;
	const int percent = std::clamp(getDirectCreatureActivationDamagePercent(), 1, 100);
	// Divide first to retain exact int64 HP without overflowing a damage * percent product.
	return (damage / 100) * percent + (damage % 100) * percent / 100;
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
	return newHorizonsDivineMandate::crownSecondWindPercent(*hero, *order, unit->unitId());
}

int64_t Mechanics::adjustRecipientDamage(const battle::Unit * target, int64_t rawDamage) const
{
	const auto * spell = dynamic_cast<const CSpell *>(getSpell());
	if(!spell || !target || rawDamage <= 0)
		return 0;
	const auto * callback = battle();
	const int holdReductionBasisPoints = callback && spell->isMagical()
		? callback->battleGetHoldTheLineMagicalReductionBasisPoints(target) : 0;
	const int perkReductionBasisPoints = callback && spell->isMagical()
		? callback->battleGetPerkMagicalReductionBasisPoints(target) : 0;
	const bool hostileRecipient = callback && caster
		&& callback->battleGetOwner(target) != caster->getCasterOwner();
	const auto penetrations = hostileRecipient
		? capturedMdrPenetrations(getCapturedMdrPenetration(), target->unitId(),
			callback ? callback->getBattle() : nullptr) : std::vector<int>{};
	return spell->adjustRawDamage(caster, target, rawDamage, 0,
		holdReductionBasisPoints, 100, usesNewHorizonsMultiplicativeMDR(),
		usesNewHorizonsMagicV3(), false, perkReductionBasisPoints, penetrations);
}

int64_t BaseMechanics::adjustEffectValue(const battle::Unit * target) const
{
	return adjustEffectValueImpl(target, true);
}

int64_t BaseMechanics::adjustEffectValueBeforeExecution(const battle::Unit * target) const
{
	return adjustEffectValueImpl(target, false);
}

int64_t BaseMechanics::adjustEffectValueImpl(const battle::Unit * target, const bool applyExecution) const
{
	const auto * hero = caster ? caster->getHeroCaster() : nullptr;
	const bool hostileRecipient = cb && caster && target
		&& cb->battleGetOwner(target) != caster->getCasterOwner();
	const bool spellPenetration = mode == Mode::HERO && isNegativeSpell() && target
		&& hostileRecipient && hero
		&& newHorizonsMagic::rulesActive(hero->getMagicRules())
		&& hero->hasActivePerk(std::string(newHorizonsMagic::SPELLCRAFT_SKILL),
			"new-horizons:spellcraft.spellPenetration");
	std::vector<int> penetrations;
	if(metamagicFollowup && isNegativeSpell() && target
		&& metamagicFocusedPairingEligible && target->unitId() == metamagicFirstTargetUnitId)
		penetrations.push_back(20);
	if(newHorizonsMagic::hasAnnihilatorPerk(hero, owner))
		penetrations.push_back(20);
	if(spellPenetration)
		penetrations.push_back(20);
	if(combatCastingEligible && mode == Mode::HERO && isNegativeSpell() && hostileRecipient)
		penetrations.push_back(15);
	if(overwhelmingFormulaEligible && mode == Mode::HERO && hostileRecipient && cb && cb->getBattle())
	{
		const auto & state = cb->getBattle()->getOverwhelmingFormulaState(casterSide);
		if(overwhelmingFormulaToken != 0 ? state.canPenetrate(overwhelmingFormulaToken)
			: state.winningCastToken == 0 && state.lastCandidateCastToken != std::numeric_limits<uint64_t>::max())
			penetrations.push_back(50);
	}
	if(mode == Mode::PASSIVE && dynamic_cast<const ObstacleCasterProxy *>(caster) && hostileRecipient)
		penetrations = capturedMdrPenetrations(getCapturedMdrPenetration(), target->unitId(),
			cb ? cb->getBattle() : nullptr);
	const int holdReductionBasisPoints = cb && owner->isMagical() && target
		? cb->battleGetHoldTheLineMagicalReductionBasisPoints(target) : 0;
	const int perkReductionBasisPoints = cb && owner->isMagical() && target
		? cb->battleGetPerkMagicalReductionBasisPoints(target) : 0;
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	const bool useIndependentMagicalDamageReduction = usesNewHorizonsMultiplicativeMDR();
	const int legacyIgnoreReduction = penetrations.empty() ? 0 : *std::max_element(penetrations.begin(), penetrations.end());
	int finalDamageMultiplierPercent = 100;
	if(target && owner->getJsonKey() == "new-horizons:holyWrath")
	{
		if(battleState)
		{
			const auto & magicRules = battleState->getMagicRules();
			if(newHorizonsMagic::rulesActive(magicRules)
				&& magicRules["rulesetVersion"].Integer() == newHorizonsMagic::SCHOOL_RANK_POWER_COEFFICIENT_RULESET_VERSION
				&& newHorizonsMagic::spellAllowedBySavedRoster(magicRules, owner->getId())
				&& (target->hasBonusOfType(BonusType::UNDEAD)
					|| target->getFactionID() == FactionID::INFERNO))
			{
				// Undead may be from any faction; Demonic currently means a base
				// Inferno faction. A creature with both traits is still multiplied once.
				finalDamageMultiplierPercent = 150;
			}
		}
	}
	int64_t rawDamage = getEffectValue();
	if(target && battleState)
	{
		const auto & magicRules = battleState->getMagicRules();
		if(const auto implosion = newHorizonsImplosion::rulesFor(magicRules, owner->getId()))
			rawDamage = newHorizonsImplosion::damage(*implosion, target->getAvailableHealth(),
				std::max(0, getEffectPower()), getSpellPowerCoefficientBasisPoints(),
				getWarcastingBonusPercent(), getEmpowerSpellBonusPercent());
		if(const auto missingHealthDamage = newHorizonsMagic::soulReaperMissingHealthDamage(
			magicRules, owner->getId(), target->getShadowGiftMaximumHealth(), target->getAvailableHealth()))
		{
			if(*missingHealthDamage > 0 && rawDamage > std::numeric_limits<int64_t>::max() - *missingHealthDamage)
				rawDamage = std::numeric_limits<int64_t>::max();
			else
				rawDamage += *missingHealthDamage;
		}
	}
	int64_t adjustedDamage = owner->adjustRawDamage(caster, target, rawDamage,
		useIndependentMagicalDamageReduction ? 0 : legacyIgnoreReduction,
		holdReductionBasisPoints, finalDamageMultiplierPercent, useIndependentMagicalDamageReduction,
		usesNewHorizonsMagicV3(), true, perkReductionBasisPoints,
		useIndependentMagicalDamageReduction ? penetrations : std::vector<int>{});
	if(applyExecution && target && battleState && newHorizonsMagic::soulReaperEnabled(
		battleState->getMagicRules(), owner->getId()))
		adjustedDamage = newHorizonsMagic::soulReaperDamageAfterExecution(
			target->getShadowGiftMaximumHealth(), target->getAvailableHealth(), adjustedDamage);
	return adjustedDamage;
}

int32_t BaseMechanics::getCrownAndAltarBonusPercent(const battle::Unit * target) const
{
	return target && !target->isGhost() && !target->isTimeStopped()
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
	{
		auto component = scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
			3LL * getEffectPower() * multiplier, 200, coefficient, specialty);
		if(hero && hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.healer"))
			component = component * 120 / 100;
		return 25 + component;
	}
	if(isNewHorizonsResurrection())
		return newHorizonsMagic::RESURRECTION_BASE_POOL_HP
			+ scaleDamageSpellPowerComponentWithCoefficientBasisPoints(
				static_cast<int64_t>(newHorizonsMagic::RESURRECTION_SPELL_POWER_HP_PER_POINT)
					* std::max<int64_t>(0, getEffectPower()) * multiplier, 100, coefficient, specialty);
	return getEffectValue();
}

int64_t BaseMechanics::applySpellBonus(int64_t value, const battle::Unit * target) const
{
	return caster->getSpellBonus(owner, value, target);
}

int64_t BaseMechanics::applySpecificSpellBonus(int64_t value) const
{
	return caster->getSpecificSpellBonus(owner, value);
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

int32_t BaseMechanics::getWarcastingBonusPercent() const
{
	return warcastingBonusPercent;
}

int32_t BaseMechanics::getArcaneFocusBonusPercent() const
{
	return arcaneFocusBonusPercent;
}

int32_t BaseMechanics::getConsecratedCastingBonusPercent() const
{
	return consecratedCastingBonusPercent;
}

int32_t BaseMechanics::getCastSpellPowerComponentBonusPercent() const
{
	const int combinedMultiplierPercent = static_cast<int>((100LL + arcaneFocusBonusPercent)
		* grandFormulaMultiplierPercent * (100 + consecratedCastingBonusPercent)
		* (100 + counterpressureBonusPercent) * (100 + concentrationBonusPercent)
		* (100 + crossSchoolFormulaBonusPercent) / 10000000000LL);
	return combinedMultiplierPercent - 100;
}

IBattleCast::Value BaseMechanics::getEffectDuration() const
{
	return effectDuration;
}

IBattleCast::Value BaseMechanics::adjustEffectDuration(IBattleCast::Value baseDuration) const
{
	const int extra = static_cast<int>(extendSpellEligible && baseDuration > 0)
		+ static_cast<int>(isMetamagicFollowup()
			&& newHorizonsMagic::hasMetamagicPerk(dynamic_cast<const CGHeroInstance *>(caster),
				newHorizonsMagic::METAMAGIC_ECHOED_DURATION));
	if(baseDuration < std::numeric_limits<IBattleCast::Value>::max())
		baseDuration += std::min<IBattleCast::Value>(extra,
			std::numeric_limits<IBattleCast::Value>::max() - std::max<IBattleCast::Value>(baseDuration, 0));
	return baseDuration;
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

BattleSide BaseMechanics::getCounterspellSide() const
{
	return counterspellSide;
}

bool BaseMechanics::isCounterspellNegated() const
{
	return counterspellNegated;
}

int32_t BaseMechanics::getCounterspellManaSpent() const
{
	return counterspellManaSpent;
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

int64_t BaseMechanics::getStormOfDaggersDamagePerTarget(int32_t selectedTargetCount) const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	if(!isNewHorizonsStormOfDaggers() || !battleState)
		return 0;

	const auto formula = newHorizonsMagic::spellDirectDamage(
		battleState->getMagicRules(), owner->getJsonKey());
	if(!formula)
		return 0;
	return stormOfDaggersRoundedDamage(formula->base, formula->powerCoefficient,
		getEffectPower(), getEffectPowerDivisor(), getSpellPowerCoefficientBasisPoints(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), selectedTargetCount, false);
}

int64_t BaseMechanics::getStormOfDaggersTotalDamage(int32_t selectedTargetCount) const
{
	const auto * battleState = cb ? cb->getBattle() : nullptr;
	if(!isNewHorizonsStormOfDaggers() || !battleState)
		return 0;

	const auto formula = newHorizonsMagic::spellDirectDamage(
		battleState->getMagicRules(), owner->getJsonKey());
	if(!formula)
		return 0;
	return stormOfDaggersRoundedDamage(formula->base, formula->powerCoefficient,
		getEffectPower(), getEffectPowerDivisor(), getSpellPowerCoefficientBasisPoints(),
		getWarcastingBonusPercent(), getEmpowerSpellBonusPercent(), selectedTargetCount, true);
}

bool BaseMechanics::isMetamagicFollowup() const
{
	return metamagicFollowup;
}

bool BaseMechanics::isMetamagicGrand() const
{
	return metamagicGrand;
}

uint32_t BaseMechanics::getMetamagicTargetUnitId() const
{
	return metamagicTargetUnitId;
}

int32_t BaseMechanics::getMetamagicManaRefund() const
{
	return metamagicManaRefund;
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

const CreatureService * BaseMechanics::creatures() const
{
	return LIBRARY->creatures(); //todo: redirect
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
