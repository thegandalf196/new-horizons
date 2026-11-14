/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "CreatureSupplementalStats.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/ArcaneInspiration.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsCommandResources.h"
#include "../../lib/battle/NewHorizonsElementalAttunement.h"
#include "../../lib/battle/UnitMagicResistanceQuery.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/combatScripts/NativePhysicalProtection.h"
#include "../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace creatureSupplementalStats
{
namespace
{
Retaliations retaliationAllowance(const CStackInstance & armyStack, const CStack * battleStack,
	const CGHeroInstance * hero)
{
	if(battleStack)
		return {std::max(0, battleStack->counterAttacks.available()),
			std::max(0, battleStack->counterAttacks.total()), !battleStack->counterAttacks.isLimited()};
	if(armyStack.hasBonusOfType(BonusType::SIEGE_WEAPON) || armyStack.hasBonusOfType(BonusType::NO_RETALIATION))
		return {};
	const int64_t ordinary = std::max<int64_t>(0, 1LL + armyStack.valOfBonuses(BonusType::ADDITIONAL_RETALIATION));
	const int64_t total = ordinary + newHorizonsNecromancy::tombGuardAdditionalRetaliations(hero, armyStack.getCreatureID());
	const int bounded = static_cast<int>(std::clamp<int64_t>(total, 0, std::numeric_limits<int32_t>::max()));
	return {bounded, bounded, armyStack.hasBonusOfType(BonusType::UNLIMITED_RETALIATIONS)};
}

std::optional<int> physicalReduction(const CStackInstance & armyStack, const CStack * battleStack,
	const CGHeroInstance * hero, const JsonNode & rules)
{
	const int cap = newHorizonsMagic::physicalDamageReductionCapPercent(rules);
	if(cap < 0)
		return std::nullopt;
	const IBonusBearer & bearer = battleStack ? *battleStack->getBonusBearer() : *armyStack.getBonusBearer();
	// The canonical pure fold reads only these scalar inputs and detached facts;
	// no attacker, attack admission, projection or battlefield state is created.
	DamageAttackInfo inputs;
	inputs.physicalDamageReductionCapPercent = cap;
	inputs.newHorizonsArmorerReductionPercent = newHorizonsCombatSkills::armorerReductionPercent(hero);
	inputs.intrinsicPhysicalDamageReductionPercent = battleStack
		? battleStack->intrinsicPhysicalDamageReductionPercent()
		: newHorizonsNecromancy::graveplatePhysicalReductionPercent(hero, armyStack.getCreatureID());
	inputs.stoneboundPhysicalReductionBasisPoints = battleStack
		? battleStack->elementalAttunementPassives().physicalReductionBasisPoints
		: newHorizonsElementalAttunement::captureProfile(hero).stoneboundBasisPoints;
	if(battleStack && battleStack->getBattle())
		inputs.arcaneBulwarkReductionPercent = arcaneInspiration::physicalReductionPercent(
			*battleStack, battleStack->getBattle()->battleGetOwner(battleStack));
	const auto facts = nativeDamage::captureUniversalPhysicalProtection(bearer);
	const double remaining = nativeDamage::physicalDamageReductionFactor(inputs, facts);
	constexpr int64_t precision = 100'000'000;
	int64_t damage = std::llround(std::clamp(remaining, 0.0, 1.0) * precision);
	if(battleStack && battleStack->getBattle())
		damage = newHorizonsCommand::mitigateIncomingDamage(*battleStack->getBattle(), battleStack, damage);
	return static_cast<int>(std::llround((precision - damage) * 10000.0 / precision));
}
}

Readout query(const CStackInstance & armyStack, const CStack * battleStack,
	const CGHeroInstance * hero, const JsonNode & magicRules)
{
	// Live protection follows the current controller, including Hypnotize.
	if(battleStack && battleStack->getBattle())
		hero = battleStack->getBattle()->battleGetOwnerHero(battleStack);
	Readout result;
	result.retaliations = retaliationAllowance(armyStack, battleStack, hero);
	result.physicalReductionBasisPoints = physicalReduction(armyStack, battleStack, hero, magicRules);
	result.magicResistancePercent = battleStack ? battleStack->magicResistance()
		: battle::combinedMagicResistance(armyStack.valOfBonuses(BonusType::MAGIC_RESISTANCE),
			newHorizonsNecromancy::bonewardMagicResistancePercent(hero, armyStack.getCreatureID()), 0,
			newHorizonsMagic::rulesActive(magicRules));
	return result;
}

std::string retaliationText(const Retaliations & value)
{
	if(value.unlimited)
		return "∞";
	if(value.available == value.total)
		return std::to_string(value.total);
	return std::to_string(value.available) + "/" + std::to_string(value.total);
}

std::string percentageText(int basisPoints)
{
	basisPoints = std::clamp(basisPoints, 0, 10'000);
	std::string result = std::to_string(basisPoints / 100);
	const int fraction = std::abs(basisPoints % 100);
	if(fraction != 0)
	{
		result += ".";
		if(fraction < 10)
			result += "0";
		result += std::to_string(fraction);
		if(result.back() == '0')
			result.pop_back();
	}
	return result + "%";
}
}
