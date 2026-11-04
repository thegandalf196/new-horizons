/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "DemonicGateArrival.h"
#include "NewHorizonsDemonicGating.h"
#include "NewHorizonsMagicalAbilityDamage.h"
#include "CBattleInfoCallback.h"
#include "Unit.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../spells/IncomingMagicalDamage.h"
namespace newHorizonsDemonicGating
{
ArrivalPerks arrivalPerks(const CGHeroInstance * hero)
{
	const auto has = [hero](const char * name)
	{ return hero && hero->hasActivePerk("new-horizons:demonicGating", std::string("new-horizons:demonicGating.") + name); };
	const bool temporary = newHorizonsDemonicGating::usesTemporaryGating(hero);
	return {hero, has("swiftGate"), has("hellfireArrival"), has("reinforcedGate"), has("infernalBeacon"),
		has(temporary ? "infernalDiscipline" : "reserveDiscipline"), temporary,
		temporary && has("infernalOnslaught"), temporary && has("hellstride")};
}
bool beaconEligible(const CBattleInfoCallback & battle, const battle::Unit & stack, BattleSide side)
{
	return std::ranges::any_of(battle.battleAdjacentUnits(&stack), [&](const auto * adjacent)
	{
		return adjacent && adjacent->alive() && adjacent->unitId() != stack.unitId()
			&& battle.playerToSide(battle.battleGetOwner(adjacent)) == side
			&& adjacent->creatureId().toCreature()->getFactionID() == FactionID::INFERNO;
	});
}
int64_t reinforcedHitPoints(int64_t currentHealth)
{
	return currentHealth * 20 / 100;
}
int64_t hellfireRawPerEnemy(int64_t currentHealth, size_t enemyCount)
{
	const int64_t total = currentHealth * 15 / 100;
	return enemyCount == 0 ? 0 : total / enemyCount;
}
std::vector<Bonus> arrivalBonuses(const ArrivalPerks & perks, bool endPhase, bool beacon)
{
	std::vector<Bonus> bonuses;
	if(perks.onslaught)
	{
		// The ordinary attack calculator consumes this for melee, shooting and
		// retaliation. Hellfire Arrival uses its separate raw magical-damage path.
		Bonus damage(BonusDuration::ONE_BATTLE, BonusType::GENERAL_DAMAGE_PREMY,
			BonusSource::HERO_SPECIAL, INFERNAL_ONSLAUGHT_DAMAGE_PERCENT, BonusSourceID(perks.hero->id));
		damage.description.appendRawString("Infernal Onslaught");
		bonuses.push_back(std::move(damage));
	}
	if(perks.hellstride)
	{
		Bonus speed(BonusDuration::ONE_BATTLE, BonusType::STACKS_MOVEMENT_RANGE,
			BonusSource::HERO_SPECIAL, HELLSTRIDE_SPEED, BonusSourceID(perks.hero->id));
		speed.description.appendRawString("Hellstride");
		bonuses.push_back(std::move(speed));
	}
	if(beacon)
	{
		Bonus initiative(BonusDuration::N_TURNS, BonusType::STACKS_INITIATIVE_FLAT,
			BonusSource::HERO_SPECIAL, 2, BonusSourceID(perks.hero->id));
		initiative.turnsRemain = endPhase ? 2 : 1;
		initiative.description.appendRawString("Infernal Beacon");
		bonuses.push_back(std::move(initiative));
	}
	if(perks.discipline)
	{
		Bonus floor(perks.temporary ? BonusDuration::ONE_BATTLE : BonusDuration::N_TURNS,
			BonusType::MINIMUM_MORALE, BonusSource::HERO_SPECIAL, 0, BonusSourceID(perks.hero->id));
		floor.turnsRemain = perks.temporary ? 0 : endPhase ? 2 : 1;
		floor.description.appendRawString(perks.temporary ? "Infernal Discipline" : "Reserve Discipline");
		bonuses.push_back(std::move(floor));
	}
	return bonuses;
}
std::vector<const battle::Unit *> hellfireTargets(const CBattleInfoCallback & battle,
	const battle::Unit & gated, BattleSide side, bool temporary)
{
	std::vector<const battle::Unit *> enemies;
	for(const auto * adjacent : battle.battleAdjacentUnits(&gated))
		if(adjacent && adjacent->alive() && (temporary
			? battle.battleGetOwner(adjacent) != battle.sideToPlayer(side) : adjacent->unitSide() != side))
			enemies.push_back(adjacent);
	return enemies;
}
int64_t hellfireDamage(const CBattleInfoCallback & battle, const battle::Unit & enemy, int64_t each, bool temporary)
{
	return temporary ? newHorizonsMagicalAbilityDamage::adjustFireDamage(battle, enemy, each)
		: spells::adjustUnmitigatedMagicalDamage(enemy, each);
}
}
