/*
 * NewHorizonsNaturesWrath.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "NewHorizonsNaturesWrath.h"
#include "NewHorizonsSpellAvailability.h"
#include "ISpellMechanics.h"
#include "CSpell.h"
#include "MagicalDamageReduction.h"
#include "NewHorizonsMagic.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../mapObjects/CGHeroInstance.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <algorithm>
#include <limits>
#include <set>
#include <stdexcept>

namespace newHorizonsNaturesWrath
{
namespace
{
int footprintDistance(const battle::Unit & first, const battle::Unit & second)
{
	int distance = GameConstants::BFIELD_SIZE;
	for(const auto from : first.getHexes())
		for(const auto to : second.getHexes())
			if(from.isAvailable() && to.isAvailable())
				distance = std::min(distance, static_cast<int>(BattleHex::getDistance(from, to)));
	return distance;
}
}

bool enabled(const spells::Mechanics & mechanics)
{
	const auto * spell = mechanics.getSpell();
	const auto * callback = mechanics.battle();
	const auto * battle = callback ? callback->getBattle() : nullptr;
	return spell && spell->getJsonKey() == SPELL_KEY && battle && mechanics.usesNewHorizonsMagicV3()
		&& newHorizonsMagic::spellAllowedBySavedRoster(battle->getMagicRules(), spell->getId());
}

bool hasWorldroot(const CGHeroInstance * hero)
{
	return hero && hero->hasActivePerk("new-horizons:natureMagic", "new-horizons:natureMagic.worldroot");
}

bool validConductor(const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && !unit->isTurret()
		&& !unit->isTimeStopped() && unit->getPosition().isAvailable();
}

spells::Target route(const CBattleInfoCallback & battle, const battle::Unit * first, int maximumRecipients)
{
	spells::Target result;
	if(!validConductor(first) || maximumRecipients < 1 || maximumRecipients > WORLDROOT_RECIPIENTS)
		return result;
	const auto candidates = battle.battleGetUnitsIf(validConductor);
	const auto firstCandidate = std::ranges::find_if(candidates, [first](const battle::Unit * candidate)
		{ return candidate->unitId() == first->unitId(); });
	if(firstCandidate == candidates.end())
		return result;

	std::set<uint32_t> visited;
	// A detached callback owns the authoritative projection of this ID. Never
	// accidentally route from the live position of a caller-supplied pointer.
	const auto * current = *firstCandidate;
	while(current && result.size() < static_cast<size_t>(maximumRecipients))
	{
		result.emplace_back(current);
		visited.insert(current->unitId());
		const battle::Unit * next = nullptr;
		int nearestDistance = GameConstants::BFIELD_SIZE;
		for(const auto * candidate : candidates)
		{
			if(visited.contains(candidate->unitId()))
				continue;
			const auto distance = footprintDistance(*current, *candidate);
			if(distance < nearestDistance
				|| (distance == nearestDistance && (!next || candidate->unitId() < next->unitId())))
			{
				next = candidate;
				nearestDistance = distance;
			}
		}
		current = next;
	}
	return result;
}

spells::Target route(const spells::Mechanics & mechanics, const battle::Unit * first)
{
	if(!enabled(mechanics))
		return {};
	return route(*mechanics.battle(), first,
		hasWorldroot(mechanics.getHeroCaster()) ? WORLDROOT_RECIPIENTS : BASE_RECIPIENTS);
}

int64_t hopPower(int32_t spellPower, int32_t coefficientBasisPoints, int32_t warcastingPercent,
	int32_t empowerPercent, bool worldroot, int32_t hopIndex)
{
	if(coefficientBasisPoints < 0 || coefficientBasisPoints > 100000
		|| warcastingPercent < 0 || warcastingPercent > 1000 || empowerPercent < 0 || empowerPercent > 1000
		|| hopIndex < 0 || hopIndex >= (worldroot ? WORLDROOT_RECIPIENTS : BASE_RECIPIENTS))
		throw std::invalid_argument("Invalid Nature's Wrath power projection");
	using boost::multiprecision::cpp_int;
	// 10000 basis points, two percentage modifiers, and Worldroot's percentage.
	cpp_int denominator = cpp_int(10000) * 100 * 100 * 100;
	cpp_int numerator = cpp_int(BASE_POWER) * denominator
		+ cpp_int(SPELL_POWER_MULTIPLIER) * std::max(0, spellPower) * coefficientBasisPoints
			* (100 + warcastingPercent) * (100 + empowerPercent) * (worldroot ? 110 : 100);
	for(int hop = 0; hop < hopIndex; ++hop)
	{
		numerator *= HOP_RETENTION_PERCENT;
		denominator *= 100;
	}
	const cpp_int value = numerator / denominator;
	if(value > std::numeric_limits<int64_t>::max())
		throw std::overflow_error("Nature's Wrath power exceeds the effect packet range");
	return value.convert_to<int64_t>();
}

int64_t hopPower(const spells::Mechanics & mechanics, int32_t hopIndex)
{
	if(!enabled(mechanics))
		return 0;
	return hopPower(mechanics.getEffectPower(), mechanics.getSpellPowerCoefficientBasisPoints(),
		mechanics.getWarcastingBonusPercent(), mechanics.getEmpowerSpellBonusPercent(),
		hasWorldroot(mechanics.getHeroCaster()), hopIndex);
}

int64_t damage(const spells::Mechanics & mechanics, const battle::Unit * recipient, int32_t hopIndex)
{
	if(!enabled(mechanics) || !validConductor(recipient) || recipient->isInvincible()
		|| !mechanics.caster || mechanics.ownerMatches(recipient, true) || !mechanics.isReceptive(recipient))
		return 0;
	const auto * spell = dynamic_cast<const CSpell *>(mechanics.getSpell());
	if(!spell)
		return 0;
	const auto * callback = mechanics.battle();
	const auto penetrations = spells::capturedMdrPenetrations(mechanics.getCapturedMdrPenetration(),
		recipient->unitId(), callback->getBattle());
	const bool independent = mechanics.usesNewHorizonsMultiplicativeMDR();
	const int legacyPenetration = penetrations.empty() ? 0 : *std::max_element(penetrations.begin(), penetrations.end());
	return spell->adjustRawDamage(mechanics.caster, recipient, hopPower(mechanics, hopIndex),
		independent ? 0 : legacyPenetration,
		callback->battleGetHoldTheLineMagicalReductionBasisPoints(recipient), 100,
		independent, mechanics.usesNewHorizonsMagicV3(), true,
		callback->battleGetPerkMagicalReductionBasisPoints(recipient),
		independent ? penetrations : std::vector<int>{});
}
}
