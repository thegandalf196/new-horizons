/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "NewHorizonsMagic.h"
#include "NewHorizonsSpellAvailability.h"
#include "NewHorizonsBlessDuration.h"
#include "NewHorizonsSorceryRevision.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../mapObjects/CGTownInstance.h"
#include <limits>
#include <algorithm>

namespace spells
{
void BaseMechanics::initializeEffectDuration(const IBattleCast * event)
{
	auto value = event->getEffectDuration();
	effectDuration = value.value_or(caster->getEnchantPower(owner));
	vstd::amax(effectDuration, 0); //???
	if(value.has_value() && extendSpellEligible && effectDuration < std::numeric_limits<decltype(effectDuration)>::max())
		++effectDuration;
	if(value && *value > 0 && mode == Mode::HERO)
		effectDuration = static_cast<int32_t>(std::min<int64_t>(std::numeric_limits<int32_t>::max(),
			static_cast<int64_t>(effectDuration) + newHorizonsSorceryRevision::spellbinderBonus(getHeroCaster(), getSpellId())));
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
			const auto ordinary = newHorizonsMagic::blessOrdinaryDuration(effectPower,
				getSpellPowerCoefficientBasisPoints(), getEmpowerSpellBonusPercent(),
				heroCaster->getNonDamageSpellSpecialtyBonusPercent(familyID));
			effectDuration = newHorizonsMagic::blessCasterDuration(*heroCaster, ordinary, infernoDurationBonus);
		}
		else
		{
			if(infernoDurationBonus && effectDuration <= std::numeric_limits<decltype(effectDuration)>::max() - 20)
				effectDuration += 20;
			if(effectDuration <= std::numeric_limits<decltype(effectDuration)>::max() - bonus)
				effectDuration += bonus;
		}

		// Default durations receive cast extensions exactly once. Literal round
		// timers use their own shared configured-duration path.
		effectDuration = adjustEffectDuration(effectDuration);
	}
}
}
