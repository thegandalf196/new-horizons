/*
 * HeroOrderCandidateRules.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#pragma once

#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/battle/IBattleState.h"

namespace heroOrderCandidates
{
constexpr HeroCommand riposteCommand()
{
	return HeroCommand::RIPOSTE;
}

constexpr HeroCommand braceCommand()
{
	return HeroCommand::BRACE;
}

constexpr HeroCommand protectCommand()
{
	return HeroCommand::PROTECT;
}

constexpr HeroCommand flankCommand()
{
	return HeroCommand::FLANK;
}

constexpr HeroCommand secondWindCommand()
{
	return HeroCommand::SECOND_WIND;
}

inline bool isEligibleOrderUnit(const CBattleInfoCallback & battle, BattleSide side,
	const battle::Unit * unit)
{
	return unit && unit->alive() && !unit->isGhost() && unit->isValidTarget()
		&& battle.battleGetOwner(unit) == battle.sideToPlayer(side)
		&& !unit->isTurret() && !unit->hasBonusOfType(BonusType::SIEGE_WEAPON);
}

inline bool adjacentForProtect(const battle::Unit * first, const battle::Unit * second)
{
	if(!first || !second || !first->getPosition().isValid() || !second->getPosition().isValid())
		return false;
	for(const auto & firstHex : first->getHexes())
		for(const auto & secondHex : second->getHexes())
			if(BattleHex::getDistance(firstHex, secondHex) <= 1)
				return true;
	return false;
}

/// A value-only fallback for older snapshots and for runtime APIs that expose
/// only the ordinary one-target query.  It is intentionally conservative: the
/// authoritative callback remains the final arbiter before an action is sent.
template <typename Battle>
std::vector<std::vector<uint32_t>> orderTargetOptions(const Battle & battle,
	BattleSide side, HeroCommand command)
{
	std::vector<std::vector<uint32_t>> result;

	// Canonical runtime builds validate the complete target tuple (including the
	// Protector/Ward ordering and round eligibility) through this snapshot API.
	// Enumerate only the small candidate identity set exposed by the callback;
	// never infer authority from local geometry or activation flags.
	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, std::vector<uint32_t>{}); })
	{
		// The method is present on every current callback, including legacy
		// targeted-command snapshots.  Only the canonical ruleset gives it
		// authority; legacy Focus Fire still uses its scalar validator below.
		const bool canonicalRules = battle.getBattle()
			&& heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules());
		if(canonicalRules)
		{
			const auto candidates = battle.battleGetHeroCommandTargets(side, command);
			if(command == protectCommand())
			{
				for(const auto protector : candidates)
					for(const auto ward : candidates)
						if(protector != ward && battle.battlePrepareHeroOrderState(side, command, {protector, ward}))
							result.push_back({protector, ward});
			}
			else
			{
				for(const auto targetId : candidates)
					if(battle.battlePrepareHeroOrderState(side, command, {targetId}))
						result.push_back({targetId});
			}
			return result;
		}
	}

	if(command == protectCommand())
	{
		const auto units = battle.battleGetAllUnits(false);
		for(size_t i = 0; i < units.size(); ++i)
		{
			if(!isEligibleOrderUnit(battle, side, units[i]))
				continue;
			for(size_t j = 0; j < units.size(); ++j)
			{
				if(i != j && isEligibleOrderUnit(battle, side, units[j]) && adjacentForProtect(units[i], units[j]))
					result.push_back({units[i]->unitId(), units[j]->unitId()});
			}
		}
		return result;
	}

	// The existing callback already returns authoritative Focus Fire targets;
	// the runtime extension uses the same shape for Flank and Second Wind.
	for(const auto targetId : battle.battleGetHeroCommandTargets(side, command))
		result.push_back({targetId});
	return result;
}

template <typename Battle>
bool commandTargetIsLegal(const Battle & battle, BattleSide side, HeroCommand command,
	const std::vector<uint32_t> & targetIds)
{
	if(targetIds.empty())
		return battle.battleCanUseHeroCommand(side, command);

	if constexpr(requires { battle.battlePrepareHeroOrderState(side, command, targetIds); })
	{
		if(battle.getBattle() && heroCommands::isCanonicalRules(battle.getBattle()->getHeroCommandRules()))
			return battle.battlePrepareHeroOrderState(side, command, targetIds).has_value();
	}
	if constexpr(requires { battle.battleGetHeroCommandTargetOptions(side, command); })
	{
		for(const auto & option : battle.battleGetHeroCommandTargetOptions(side, command))
			if(std::vector<uint32_t>(option.begin(), option.end()) == targetIds)
				return true;
		return false;
	}
	else if constexpr(requires { battle.battleCanConfirmHeroCommand(side, command, targetIds); })
		return battle.battleCanConfirmHeroCommand(side, command, targetIds);
	else if(targetIds.size() == 1)
		return battle.battleCanConfirmHeroCommand(side, command, targetIds.front());
	else
		// Protect's pair legality is checked atomically by the new server API.
		// No pair must be sent from a legacy callback that cannot validate it.
		return false;
}

}
