/*
 * NewHorizonsPandemonium.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsPandemonium.h"
#include "NewHorizonsSpellAvailability.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "MagicalDamageReduction.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace newHorizonsPandemonium
{
bool enabled(const spells::Mechanics & mechanics)
{
	const auto * spell = mechanics.getSpell();
	const auto * callback = mechanics.battle();
	const auto * battle = callback ? callback->getBattle() : nullptr;
	return spell && spell->getJsonKey() == SPELL_KEY && battle && mechanics.usesNewHorizonsMagicV3()
		&& newHorizonsMagic::spellAllowedBySavedRoster(battle->getMagicRules(), spell->getId());
}

bool hasMaster(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:chaosMagic", "new-horizons:chaosMagic.pandemoniumMaster");
}

bool validRecipient(const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && !unit->isTurret()
		&& unit->getPosition().isAvailable();
}

spells::Target targets(const spells::Mechanics & mechanics)
{
	spells::Target result;
	if(!enabled(mechanics))
		return result;
	auto units = mechanics.battle()->battleGetUnitsIf(validRecipient);
	std::ranges::sort(units, {}, &battle::Unit::unitId);
	for(const auto * unit : units)
		result.emplace_back(unit);
	return result;
}

std::vector<RecipientSnapshot> snapshot(const spells::Mechanics & mechanics)
{
	std::vector<RecipientSnapshot> result;
	for(const auto & destination : targets(mechanics))
		result.push_back({destination.unitValue, newHorizonsDebuffStatuses::snapshot(*destination.unitValue)});
	return result;
}

int64_t rawPower(int32_t spellPower, int32_t coefficientBasisPoints, int32_t warcastingPercent,
	int32_t empowerPercent, bool master, size_t debuffCount)
{
	if(coefficientBasisPoints < 0 || coefficientBasisPoints > 100000
		|| warcastingPercent < 0 || warcastingPercent > 1000 || empowerPercent < 0 || empowerPercent > 1000)
		throw std::invalid_argument("Invalid Pandemonium power projection");
	using boost::multiprecision::cpp_int;
	const cpp_int denominator = cpp_int(4) * 10000 * 100 * 100 * 100;
	const cpp_int numerator = cpp_int(debuffCount)
		* (cpp_int(20) * 4 * 10000 * 100 * 100
			+ cpp_int(std::max(0, spellPower)) * coefficientBasisPoints
				* (100 + warcastingPercent) * (100 + empowerPercent))
		* (master ? 125 : 100);
	const cpp_int value = numerator / denominator;
	if(value > std::numeric_limits<int64_t>::max())
		throw std::overflow_error("Pandemonium power exceeds the effect packet range");
	return value.convert_to<int64_t>();
}

int64_t rawPower(const spells::Mechanics & mechanics, size_t debuffCount)
{
	if(!enabled(mechanics))
		return 0;
	return rawPower(mechanics.getEffectPower(), mechanics.getSpellPowerCoefficientBasisPoints(),
		mechanics.getWarcastingBonusPercent(), mechanics.getEmpowerSpellBonusPercent(),
		hasMaster(mechanics.getHeroCaster()), debuffCount);
}

int64_t damage(const spells::Mechanics & mechanics, const battle::Unit * recipient, size_t debuffCount)
{
	if(debuffCount == 0 || !enabled(mechanics) || !validRecipient(recipient) || recipient->isInvincible()
		|| !mechanics.caster || !mechanics.isReceptive(recipient))
		return 0;
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if(!spell)
		return 0;
	const auto * callback = mechanics.battle();
	const auto penetrations = spells::capturedMdrPenetrations(mechanics.getCapturedMdrPenetration(),
		recipient->unitId(), callback->getBattle());
	const bool independent = mechanics.usesNewHorizonsMultiplicativeMDR();
	const int legacyPenetration = penetrations.empty() ? 0 : *std::max_element(penetrations.begin(), penetrations.end());
	return spell->adjustRawDamage(mechanics.caster, recipient, rawPower(mechanics, debuffCount),
		independent ? 0 : legacyPenetration,
		callback->battleGetHoldTheLineMagicalReductionBasisPoints(recipient), 100,
		independent, mechanics.usesNewHorizonsMagicV3(), true,
		callback->battleGetPerkMagicalReductionBasisPoints(recipient),
		independent ? penetrations : std::vector<int>{});
}
}
