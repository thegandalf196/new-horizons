/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "TemporaryGatingAI.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/Unit.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include <array>
#include <cmath>
#include <limits>

namespace temporaryGatingAI
{
namespace
{
struct Anchor
{
	BattleHex hex;
	int score = std::numeric_limits<int>::max();
	void consider(BattleHex candidate, int value)
	{
		if(value < score || (value == score && (!hex.isValid() || candidate.toInt() < hex.toInt())))
		{ hex = candidate; score = value; }
	}
};
void appendUnique(std::vector<BattleHex> & values, BattleHex hex)
{
	if(hex.isAvailable() && std::find(values.begin(), values.end(), hex) == values.end())
		values.push_back(hex);
}
int distance(BattleHex first, BattleHex second)
{
	return second.isValid() ? BattleHex::getDistance(first, second) : 0;
}
}
std::vector<Candidate> candidates(const CBattleInfoCallback & battle,
	const battle::Unit & source, BattleSide side, Workload * output)
{
	Workload work;
	std::vector<Candidate> result;
	const auto finish = [&]()
	{
		if(output) *output = work;
		return result;
	};
	if(!newHorizonsDemonicGating::eligibleOriginalStack(battle, source, side))
		return finish();
	const auto * hero = battle.battleGetFightingHero(side);
	Anchor threat, beacon;
	for(const auto * unit : battle.battleGetAllUnits(false))
	{
		++work.visibleUnits;
		if(!unit || !unit->alive() || !unit->isValidTarget(false)) continue;
		const int range = distance(source.getPosition(), unit->getPosition());
		if(battle.battleGetOwner(unit) != battle.sideToPlayer(side))
			threat.consider(unit->getPosition(), range);
		else if(unit->unitId() != source.unitId() && unit->creatureId().toCreature()->getFactionID() == FactionID::INFERNO)
			beacon.consider(unit->getPosition(), range);
	}
	const auto * threatUnit = threat.hex.isValid() ? battle.battleGetUnitByPos(threat.hex, true) : nullptr;
	if(!threatUnit) return finish();
	const auto access = battle.getAccessibility();
	const auto from = source.getPosition();
	std::array<Anchor, DEPLOYMENT_LIMIT> anchors;
	for(int index = 0; index < GameConstants::BFIELD_SIZE; ++index)
	{
		++work.geometryHexes;
		const BattleHex hex(index);
		if(!hex.isAvailable() || distance(hex, from) > newHorizonsDemonicGating::placementRange(*hero)
			|| hex == from || (source.doubleWide() && hex == source.occupiedHex(from))
			|| !access.accessible(hex, source.doubleWide(), side)) continue;
		anchors[0].consider(hex, distance(hex, threat.hex));
		anchors[1].consider(hex, -distance(hex, threat.hex));
		anchors[2].consider(hex, hex.getX());
		anchors[3].consider(hex, -hex.getX());
		anchors[4].consider(hex, hex.getY());
		anchors[5].consider(hex, -hex.getY());
		anchors[6].consider(hex, distance(hex, beacon.hex));
		anchors[7].consider(hex, distance(hex, from));
	}
	std::vector<BattleHex> deployments;
	for(const auto & anchor : anchors) appendUnique(deployments, anchor.hex);
	for(const auto to : deployments)
	{
		BattleAction action;
		action.actionType = EActionType::DEMONIC_GATING;
		action.side = side;
		action.stackNumber = source.unitId();
		action.gatingCreature = source.creatureId();
		action.aimToHex(to);
		++work.nativePlans;
		const auto plan = newHorizonsDemonicGating::plan(battle, action);
		if(plan && plan->temporary) result.push_back({std::move(action), *plan, threatUnit->unitId()});
	}
	return finish();
}
std::optional<BattleAction> choose(const std::vector<Candidate> & candidates,
	const MarginalValue & value, bool & complete, Workload * workload)
{
	complete = false;
	if(!value) return std::nullopt;
	float best = 0;
	std::optional<BattleAction> selected;
	for(const auto & candidate : candidates)
	{
		if(workload) ++workload->scoredPlans;
		const auto score = value(candidate);
		if(!score || !std::isfinite(*score)) return std::nullopt;
		if(*score > best)
		{
			best = *score;
			selected = candidate.action;
		}
	}
	complete = true;
	return selected;
}
}
