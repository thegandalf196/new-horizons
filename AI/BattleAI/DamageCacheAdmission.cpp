/*
 * DamageCacheAdmission.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "DamageCacheAdmission.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../lib/battle/NewHorizonsOffense.h"
#include "../../lib/battle/NewHorizonsShroud.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/mapObjects/CGHeroInstance.h"

bool DamageCacheAdmission::matches(const battle::Unit & candidate, const CBattleInfoCallback & branch) const
{
	return &candidate == &actor && &branch == &battle;
}

BattleSide DamageCacheAdmission::side()
{
	if(!actorSide)
		actorSide = battle.playerToSide(battle.battleGetOwner(&actor));
	return *actorSide;
}

const CGHeroInstance * DamageCacheAdmission::hero()
{
	if(!actorHero)
		actorHero = side() == BattleSide::ATTACKER || side() == BattleSide::DEFENDER
			? battle.battleGetFightingHero(side()) : nullptr;
	return *actorHero;
}

bool DamageCacheAdmission::hasAmbusher()
{
	return remember(AMBUSHER, [&] { return newHorizonsShroud::hasAmbusher(hero()); });
}

bool DamageCacheAdmission::hasShadowAssault()
{
	return remember(SHADOW, [&] { return newHorizonsShroud::hasShadowAssault(hero()); });
}

bool DamageCacheAdmission::hasNightProwler()
{
	return remember(NIGHT, [&] { return newHorizonsShroud::hasNightProwler(hero()); });
}

bool DamageCacheAdmission::hasNightProwlerEffect()
{
	return remember(NIGHT_EFFECT, [&] { return actor.hasBonus(CSelector(newHorizonsShroud::isNightProwlerBonus)); });
}

bool DamageCacheAdmission::hasRelentlessAssault()
{
	return remember(RELENTLESS, [&]
	{
		const auto * value = hero();
		return value && value->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::RELENTLESS_ASSAULT);
	});
}

bool DamageCacheAdmission::hasGamblerPenalty()
{
	return remember(GAMBLER, [&]
	{
		static const CSelector selector([](const Bonus * bonus)
		{
			return newHorizonsCombatSkills::isGamblerLuckPenalty(bonus);
		});
		return actor.hasBonus(selector);
	});
}

bool DamageCacheAdmission::hasReducedExtraActivation()
{
	return remember(REDUCED_ACTIVATION, [&]
	{
		return battle.battleGetActivationOutputPercent(&actor) < 100
			|| battle.battleIsMoraleExtraActivation(&actor);
	});
}

bool DamageCacheAdmission::hasBloodragePain()
{
	return remember(BLOODRAGE, [&]
	{
		const auto * info = battle.getBattle();
		return actor.getPersonalBloodrageIncrement() > 0
			|| (info && (side() == BattleSide::ATTACKER || side() == BattleSide::DEFENDER)
				&& info->getBloodragePainIncrement(side()) > 0);
	});
}
