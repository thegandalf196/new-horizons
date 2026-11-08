/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "Catapult.h"
#include "StructureCommon.h"
#include "../../ISpellMechanics.h"
#include <vstd/RNG.h>

namespace spells::effects::native
{
double Catapult::hitChance(EWallPart part) const
{
	const char * key = part == EWallPart::GATE ? "chanceToHitGate" : part == EWallPart::KEEP ? "chanceToHitKeep"
		: part == EWallPart::BOTTOM_TOWER || part == EWallPart::UPPER_TOWER ? "chanceToHitTower" : "chanceToHitWall";
	return parameters[key].Float();
}

int Catapult::randomDamage(ServerCallback & server) const
{
	const double critical = std::clamp(parameters["chanceToCrit"].Float(), 0.0, 100.0);
	const double hit = std::clamp(parameters["chanceToNormalHit"].Float(), 0.0, 100.0 - critical);
	const std::array<double, 3> chances{100 - hit - critical, hit, critical};
	const auto roll = server.getRNG()->nextInt(0, 99);
	double accumulated = 0;
	for(int damage = 0; damage <= 2; ++damage)
	{
		accumulated += chances[damage];
		if(roll < accumulated) return damage;
	}
	return 0;
}

std::vector<EWallPart> Catapult::potentialTargets(const CBattleInfoCallback & battle, bool bypassGate, bool bypassTower) const
{
	std::vector<EWallPart> result;
	for(const auto part : {EWallPart::BOTTOM_WALL, EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL})
		if(battle.isWallPartAttackable(part)) result.push_back(part);
	if((result.empty() || bypassGate) && battle.isWallPartAttackable(EWallPart::GATE)) result.push_back(EWallPart::GATE);
	if(result.empty() || bypassTower)
		for(const auto part : {EWallPart::BOTTOM_TOWER, EWallPart::KEEP, EWallPart::UPPER_TOWER})
			if(battle.isWallPartAttackable(part)) result.push_back(part);
	return result;
}

bool Catapult::applicableGeneral(Problem & problem, const Mechanics * mechanics) const
{
	const auto & battle = *mechanics->battle();
	if(!battle.hasFortifications() || (mechanics->isSmart() && mechanics->getCasterSide() != BattleSide::ATTACKER)
		|| potentialTargets(battle, true, true).empty()) return status::reject(mechanics, &problem);
	return true;
}

void Catapult::apply(ServerCallback * server, const Mechanics * mechanics, const Target & target) const
{
	const auto & battle = *mechanics->battle();
	const auto * attacker = mechanics->getUnitCaster();
	const auto count = parameters["targetsToAttack"].Integer();
	const nativeEvents::Context context{*server, battle, nullptr, nullptr, parameters, {}};
	if(mechanics->isMassive())
	{
		const auto pool = potentialTargets(battle, true, true);
		if(pool.empty()) return;
		std::vector<std::pair<EWallPart, int>> order;
		for(int64_t i = 0; i < count; ++i)
		{
			const auto part = pool[server->getRNG()->nextInt(1, static_cast<int>(pool.size())) - 1];
			auto found = std::find_if(order.begin(), order.end(), [&](const auto & entry) { return entry.first == part; });
			if(found == order.end()) found = order.insert(order.end(), {part, 0});
			found->second += randomDamage(*server);
		}
		for(const auto & [part, damage] : order) structure::catapultAttack(context, attacker, part, damage);
		return;
	}
	const auto desired = battle.battleHexToWallPart(target.at(0).hexValue);
	const bool locked = attacker && attacker->unitType() && attacker->isCatapult() && mechanics->usesNewHorizonsMagicV3()
		&& battle.playerToSide(battle.battleGetActionController(attacker)) == BattleSide::ATTACKER
		&& battle.battleHasWarMachinesPerk(attacker, "new-horizons:warMachines.precisionBombardment");
	for(int64_t i = 0; i < count; ++i)
	{
		if(locked && !battle.isWallPartAttackable(desired)) break;
		EWallPart actual = EWallPart::INVALID;
		if(battle.isWallPartAttackable(desired) && server->getRNG()->nextInt(0, 99) < hitChance(desired)) actual = desired;
		else if(locked)
		{
			randomDamage(*server);
			structure::catapultAttack(context, attacker, desired, 0);
		}
		else
		{
			const auto pool = potentialTargets(battle, false, false);
			if(pool.empty()) break;
			actual = pool[server->getRNG()->nextInt(1, static_cast<int>(pool.size())) - 1];
		}
		if(actual != EWallPart::INVALID) structure::catapultAttack(context, attacker, actual, randomDamage(*server));
	}
}
}
