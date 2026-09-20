/*
 * NewHorizonsSorcery.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "NewHorizonsSorcery.h"

#include <algorithm>
#include <stdexcept>

namespace newHorizonsSorcery
{
namespace
{
void validateSpellPower(int32_t spellPower)
{
	if(spellPower < 0)
		throw std::invalid_argument("Sorcery spell power cannot be negative");
}
}

int32_t phantomArmyIntegrityBasisPoints(int32_t spellPower, bool illusionist)
{
	validateSpellPower(spellPower);

	const int64_t uncapped = static_cast<int64_t>(PHANTOM_ARMY_BASE_INTEGRITY_PERCENT) * 100
		+ static_cast<int64_t>(spellPower) * 15;
	const int32_t base = static_cast<int32_t>(std::min<int64_t>(
		PHANTOM_ARMY_INTEGRITY_CAP_PERCENT * 100,
		uncapped));
	if(!illusionist)
		return base;

	// Illusionist is a multiplicative 25% integrity increase applied after the
	// canonical base cap.  Keeping the result in basis points preserves the
	// 43.75% value at Spell Power 100 instead of silently rounding it to 43%.
	return base * (100 + PHANTOM_ARMY_ILLUSIONIST_BONUS_PERCENT) / 100;
}

int64_t phantomArmyIntegrity(int64_t sourceCurrentHealth, int32_t spellPower, bool illusionist)
{
	if(sourceCurrentHealth < 0)
		throw std::invalid_argument("Phantom Army source health cannot be negative");

	const int32_t basisPoints = phantomArmyIntegrityBasisPoints(spellPower, illusionist);
	// Divide before multiplying to avoid an unnecessary wide intermediate while
	// retaining exact integer floor semantics for ordinary game-sized stacks.
	const int64_t whole = sourceCurrentHealth / 10000;
	const int64_t remainder = sourceCurrentHealth % 10000;
	return whole * basisPoints + remainder * basisPoints / 10000;
}

int phantomArmyDamageTakenPercent(bool magical)
{
	return magical ? PHANTOM_ARMY_MAGICAL_DAMAGE_TAKEN_PERCENT : PHANTOM_ARMY_PHYSICAL_DAMAGE_TAKEN_PERCENT;
}

int timeStopRadius(int32_t spellPower, bool chronomancer)
{
	validateSpellPower(spellPower);
	const int maximumRadius = TIME_STOP_BASE_MAX_RADIUS
		+ (chronomancer ? TIME_STOP_CHRONOMANCER_RADIUS_BONUS : 0);
	return std::min(maximumRadius, TIME_STOP_BASE_RADIUS + spellPower / TIME_STOP_POWER_PER_EXTRA_RADIUS);
}

int spellLockDuration(int32_t spellPower, bool spellbinder)
{
	validateSpellPower(spellPower);
	const int baseDuration = std::min(
		SPELL_LOCK_BASE_DURATION_CAP,
		1 + spellPower / SPELL_LOCK_POWER_PER_EXTRA_ROUND);
	if(!spellbinder)
		return baseDuration;

	return std::min(
		SPELL_LOCK_SPELLBINDER_DURATION_CAP,
		baseDuration + SPELL_LOCK_SPELLBINDER_DURATION_BONUS);
}

SpellLockPolicy spellLockPolicy(bool friendlyTarget)
{
	SpellLockPolicy policy;
	if(friendlyTarget)
	{
		policy.removeHostile = true;
		policy.preserveBeneficial = true;
	}
	else
	{
		policy.removeBeneficial = true;
		policy.preserveHostile = true;
	}
	return policy;
}
}
