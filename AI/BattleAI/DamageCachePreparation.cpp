/*
 * DamageCachePreparation.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "AttackPossibility.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/battle/HeroCommand.h"
#include "../../lib/combatScripts/IDamageCalculatorScript.h"
#include "../../lib/scripting/ScriptService.h"
#include <exception>

void DamageCache::buildDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side)
{
	if(parent == nullptr)
		buildObstacleDamageCache(hb, side);
	buildUnitDamageCache(hb, side, false);
}

void DamageCache::buildCreaturePressureCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side)
{
	// This terminal root cache is not a baseline for child DamageCaches. Its
	// current/original queries always bypass pair entries under these saved rules.
	const bool populated = !damageCache.empty() || !obstacleDamage.empty()
		|| !rangedMarkTargets.empty() || !evasiveShroudTargets.empty()
		|| !ambusherAttackers.empty() || !shadowAssaultTargetSides.empty()
		|| !nightProwlerAttackers.empty() || !frozenTargets.empty() || !intrinsicMdrTargets.empty();
	if(parent || populated
		|| !heroCommands::supportedByRules(hb->getBattle()->getHeroCommandRules(), HeroCommand::FOCUS_FIRE))
	{
		buildDamageCache(hb, side);
		return;
	}

	// Obstacle knowledge/damage remains exactly the ordinary root preparation.
	buildObstacleDamageCache(hb, side);
	buildUnitDamageCache(hb, side, true);
}

void DamageCache::buildUnitDamageCache(std::shared_ptr<HypotheticBattle> hb, BattleSide side, bool terminalPressure)
{
	auto stacks = hb->battleGetUnitsIf([=](const battle::Unit * u) -> bool
		{
			return u->isValidTarget();
		});

	battle::Units ourUnits;
	battle::Units enemyUnits;

	for(auto stack : stacks)
	{
		if(stack->unitSide() == side)
			ourUnits.push_back(stack);
		else
			enemyUnits.push_back(stack);
	}

	for(auto ourUnit : ourUnits)
	{
		if(!ourUnit->alive())
			continue;

		for(auto enemyUnit : enemyUnits)
		{
			if(enemyUnit->alive())
			{
				cacheDamage(ourUnit, enemyUnit, hb);
				cacheDamage(enemyUnit, ourUnit, hb);
				if(terminalPressure && !damageCache.empty())
				{
					// Retain first-use initialization and this first pair before
					// checking whether the remaining pair entries are unobservable.
					terminalPressure = false;
					const auto * calculator = LIBRARY->scriptTypes()->getDamageCalculator();
					try
					{
						if(calculator && calculator->forecastImplementation(*hb) == DamageForecastImplementation::CANONICAL)
							return;
					}
					catch(const std::exception &)
					{
						// Capability-query failure retains ordinary pair preparation.
					}
				}
			}
		}
	}
}
