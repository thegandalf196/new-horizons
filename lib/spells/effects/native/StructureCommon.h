/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#pragma once
#include "SpellEffect.h"
#include "StatusHelpers.h"
#include "../../../battle/CObstacleInstance.h"

namespace spells::effects::native::structure
{
using Obstacles = std::vector<std::shared_ptr<const CObstacleInstance>>;
inline constexpr std::array<EWallPart, 8> sections{EWallPart::KEEP, EWallPart::BOTTOM_TOWER, EWallPart::BOTTOM_WALL,
	EWallPart::BELOW_GATE, EWallPart::OVER_GATE, EWallPart::UPPER_WALL, EWallPart::UPPER_TOWER, EWallPart::GATE};
void addObstacle(const nativeEvents::Context &, SpellCreatedObstacle obstacle);
void removeObstacle(const nativeEvents::Context &, const std::shared_ptr<const CObstacleInstance> &);
DLL_LINKAGE void catapultAttack(const nativeEvents::Context &, const battle::Unit *, EWallPart, int damage);
void damageFortification(const nativeEvents::Context &, EWallPart, int damage);
SpellCreatedObstacle obstacle(const Mechanics &, BattleHex);
bool clearOfWalls(const CBattleInfoCallback &, BattleHex);
bool hexAvailable(const CBattleInfoCallback &, BattleHex, bool mustBeClear);
BattleHexArray area(BattleHex, int radius);
std::array<int, GameConstants::BFIELD_SIZE> distances(BattleHex);
BattleHex firstHex(const Target &);
}
namespace nativeSpells
{
std::shared_ptr<spells::effects::Effect> createStructureEffect(const ScriptTypeDescription &);
std::shared_ptr<spells::effects::Effect> createObstacleEffect(const ScriptTypeDescription &);
}
