/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Timed.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsSpellAvailability.h"
#include "../../NewHorizonsTimedStatusPlan.h"
#include "../../NewHorizonsTimedBuffMagnitude.h"
#include "../../../battle/Unit.h"
#include "../../../mapObjects/CGHeroInstance.h"
#include "../../../bonuses/BonusList.h"
#include "../../../bonuses/BonusParameters.h"
#include <vcmi/spells/Spell.h>
#include <cmath>

namespace spells::effects::native
{
void Timed::specialty(const Mechanics * mechanics, const battle::Unit * unit, JsonMap & buffer, double * preparedValue) const
{
	const auto * hero = mechanics->getHeroCaster();
	if(!hero) return;
	const auto * spell = newHorizonsMagic::effectSpellForCast(*mechanics);
	const auto key = spell->getJsonKey();
	if(key == "core:forgetfulness" && mechanics->usesNewHorizonsMagicV3()) return;
	for(const auto * exempt : {"new-horizons:holyArmor", "new-horizons:heavenlyGale", "new-horizons:guardianSpirit",
		"new-horizons:divineRetribution", "new-horizons:crusade", "new-horizons:entangle", "new-horizons:vengefulVines"})
		if(key == exempt) return;
	if(key == "core:misfortune")
	{
		for(auto & [name, bonus] : buffer)
			bonus["val"].Float() = newHorizonsMagic::misfortuneValueForRecipient(*mechanics, *unit, bonus["val"].Float());
		return;
	}
	const auto modifyValues = [&](const auto & transform)
	{
		for(auto & [name, bonus] : buffer)
			bonus["val"].Float() = transform(bonus["val"].Float());
		if(preparedValue)
			*preparedValue = transform(*preparedValue);
	};
	const int tier = std::max(unit->creatureLevel(), 1);
	const auto scaling = hero->getBonusesOfType(BonusType::SPECIAL_SPELL_SCALING, spell->getId());
	if(!scaling->empty())
	{
		const double percent = scaling->front()->val * std::floor(static_cast<double>(hero->level) / tier);
		if(percent != 0)
			modifyValues([percent](double value) { return std::floor(value * (100 + percent) / 100); });
	}
	const auto peculiar = hero->getBonusesOfType(BonusType::SPECIAL_PECULIAR_ENCHANT, spell->getId());
	if(!peculiar->empty())
	{
		const auto & first = peculiar->front();
		const auto levels = first->parameters && first->parameters->isVector() ? first->parameters->toVector() : std::vector<int32_t>{};
		double power;
		if(!levels.empty()) power = levels[std::clamp<size_t>(tier, 1, levels.size()) - 1];
		else
		{
			power = tier <= 2 ? 3 : tier <= 4 ? 2 : tier <= 6 ? 1 : 0;
			if(mechanics->isNegativeSpell()) power = -power;
		}
		if(power != 0)
			modifyValues([power](double value) { return value + power; });
	}
	modifyValues([&](double value)
	{
		return newHorizonsMagic::casterEnchantValueOverride(hero, spell->getId(), value).value_or(value);
	});
}

void Timed::recipientPower(const Mechanics * mechanics, const battle::Unit * unit, const std::string & key, JsonMap & buffer) const
{
	for(auto & [name, bonus] : buffer)
	{
		const auto type = bonus["type"].String();
		if(key == "new-horizons:holyArmor" && type == "SPELL_DAMAGE_REDUCTION")
			bonus["val"].Integer() = newHorizonsMagic::holyArmorReductionForRecipient(*mechanics, static_cast<int>(bonus["val"].Float()), *unit);
		if(key == "new-horizons:heavenlyGale" && type == "HEAVENLY_GALE")
			bonus["val"].Integer() = newHorizonsMagic::heavenlyGaleReductionForRecipient(*mechanics, *unit);
		if(key == "new-horizons:shieldOfChaos" && mechanics->usesNewHorizonsMagicV3()
			&& (type == "PHYSICAL_DAMAGE_REDUCTION_BASIS_POINTS" || type == "SPELL_DAMAGE_REDUCTION_BASIS_POINTS"))
			bonus["val"].Integer() = newHorizonsMagic::chaosShieldReductionForCast(*mechanics);
		if(key == "new-horizons:guardianSpirit" && type == "GUARDIAN_SPIRIT")
			bonus["val"].Integer() = mechanics->getGuardianSpiritHitPoints(unit);
		if(key == "core:fireShield" && type == "COMBAT_EVENT_TRIGGER" && mechanics->getHeroCaster())
			bonus["addInfo"]["mdrPenetration"] = mechanics->getCapturedMdrPenetration();
		if(key == "new-horizons:divineRetribution" && type == "DIVINE_RETRIBUTION")
		{
			bonus["val"].Integer() = mechanics->getDivineRetributionCap(unit);
			const auto * hero = mechanics->getHeroCaster();
			bonus["addInfo"] = JsonNode();
			bonus["addInfo"]["mdrPenetration"] = mechanics->getCapturedMdrPenetration();
			bonus["addInfo"]["retributionistPercent"].Integer() = hero
				&& hero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.retributionist") ? 120 : 100;
			if(const auto payout = mechanics->getRecipientDivineRetributionPayoutBasisPoints(unit))
				bonus["addInfo"]["payoutRatioBasisPoints"].Integer() = *payout;
		}
	}
	if(key == "new-horizons:crusade" && mechanics->usesNewHorizonsMagicV3())
	{
		const auto power = std::max(0, mechanics->getEffectPower());
		const auto attributes = std::min<int64_t>(6, 3 + mechanics->scaleRecipientSpellPowerComponent(power, 75, unit));
		const auto initiative = std::min<int64_t>(3, 1 + mechanics->scaleRecipientSpellPowerComponent(power, 100, unit));
		const auto reduction = std::min<int64_t>(2500, 1200 + mechanics->scaleRecipientSpellPowerComponent(13LL * power, 2, unit));
		for(auto & [name, bonus] : buffer)
		{
			const auto type = bonus["type"].String();
			if(type == "PRIMARY_SKILL") bonus["val"].Integer() = attributes;
			else if(type == "STACKS_INITIATIVE_FLAT") bonus["val"].Integer() = initiative;
			else if(type == "SPELL_DAMAGE_REDUCTION_BASIS_POINTS") bonus["val"].Integer() = reduction;
		}
	}
}
}
