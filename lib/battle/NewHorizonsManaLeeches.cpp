/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsManaLeeches.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../entities/hero/NewHorizonsManaLeeches.h"
#include "../mapObjects/CGHeroInstance.h"
namespace newHorizonsManaLeeches
{
Entitlement entitlement(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * primary, bool ranged, bool physical, bool secondary)
{
	if(!source || !primary || ranged || !physical || secondary
		|| (source->creatureId() != CreatureID::IMP && source->creatureId() != CreatureID::FAMILIAR)
		|| battle.battleGetOwner(source) == battle.battleGetOwner(primary)
		|| !battle.battleMatchActionController(source, primary))
		return {};
	const auto side = battle.playerToSide(battle.battleGetOwner(source));
	if(side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		return {};
	const auto * hero = battle.battleGetFightingHero(side);
	if(!newHorizonsHeroes::hasManaLeeches(hero))
		return {};
	const auto enemySide = battle.otherSide(side);
	if(!battle.battleGetFightingHero(enemySide))
		return {};
	return {side, enemySide, *newHorizonsHeroes::manaLeechesAmount(hero->getPrimaryGrowthRules()),
		source->creatureId() == CreatureID::FAMILIAR};
}
Transfer transfer(const Entitlement & rule, int64_t primaryDamage, bool sequenceSpent,
	int64_t enemyAvailable, int32_t ownNormal, int32_t ownCapacity)
{
	if(primaryDamage <= 0 || sequenceSpent || rule.amount <= 0)
		return {};
	const int stolen = static_cast<int>(std::min<int64_t>(rule.amount, std::max<int64_t>(0, enemyAvailable)));
	const auto room = std::max<int64_t>(0, static_cast<int64_t>(ownCapacity) - ownNormal);
	return {stolen, rule.restore ? static_cast<int>(std::min<int64_t>(stolen, room)) : 0};
}
}
