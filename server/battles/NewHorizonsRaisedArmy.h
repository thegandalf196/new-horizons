/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#pragma once
#include "../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../lib/GameConstants.h"
#include <optional>
#include <vector>
#include <utility>
class CArmedInstance;
class CGameHandler;
class CGTownInstance;
struct BattleResult;
namespace newHorizonsNecromancy
{
struct RaisedArmyAddition
{
	SlotID slot;
	CreatureID creature;
	TQuantity count;
};

using RaisedArmyPlan = std::vector<RaisedArmyAddition>;


struct PreparedBattleNecromancy
{
	NecromancyResult ordinary;
	NecromancyResult enhanced;
	bool ossuary = false;
};
struct RaisedArmyDelivery
{
	NecromancyResult summary;
	const CArmedInstance * army = nullptr;
	RaisedArmyPlan additions;
};
std::vector<std::pair<CreatureID, int64_t>> raisedOutputs(const NecromancyResult & result);
bool validRaisedArmyOutputs(const std::vector<std::pair<CreatureID, int64_t>> & outputs);
bool validRaisedArmyState(const CArmedInstance & army);
std::optional<RaisedArmyPlan> planRaisedArmy(const CArmedInstance & army,
	const std::vector<std::pair<CreatureID, int64_t>> & outputs);
std::optional<RaisedArmyDelivery> selectRaisedArmyDelivery(const CGHeroInstance & hero,
	const PlayerState * ownerState, const PreparedBattleNecromancy & prepared);
void applyRaisedArmy(CGameHandler & handler, const CArmedInstance & army, const RaisedArmyPlan & plan);
PreparedBattleNecromancy prepareBattleNecromancy(const BattleResult & result,
	const newHorizonsCreatures::CreatureCategoryRules & categoryRules,
	int32_t initialMana, const CGHeroInstance & hero, const PlayerState * ownerState);
}
