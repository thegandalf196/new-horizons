/*
 * CStackConstruction.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CStack.h"
#include "mapObjects/army/CStackInstance.h"
#include "mapObjects/army/CStackBasicDescriptor.h"
#include <stdexcept>

CStack::CStack(const CStackInstance * Base, const PlayerColor & O, int I, BattleSide Side, const SlotID & S):
	CBonusSystemNode(BonusNodeType::STACK_BATTLE),
	base(Base),
	ID(I),
	typeID(Base->getId()),
	baseAmount(Base->getCount()),
	owner(O),
	slot(S),
	side(Side)
{
	health.init(); //???
	doubleWideCached = battle::CUnitState::doubleWide();
}

CStack::CStack():
	CBonusSystemNode(BonusNodeType::STACK_BATTLE),
	owner(PlayerColor::NEUTRAL),
	slot(SlotID(255)),
	initialPosition(BattleHex())
{
}

CStack::CStack(const CStackBasicDescriptor * stack, const PlayerColor & O, int I, BattleSide Side,
	const SlotID & S, const bool hypothetical) :
	CStack(stack, O, I, Side, S, hypothetical, {})
{
}

CStack::CStack(const CStackBasicDescriptor * stack, const PlayerColor & O, int I, BattleSide Side,
	const SlotID & S, const bool hypothetical, UnitQueryTerrain terrain):
	CBonusSystemNode(BonusNodeType::STACK_BATTLE, hypothetical),
	ID(I),
	typeID(stack->getId()),
	baseAmount(stack->getCount()),
	owner(O),
	slot(S),
	side(Side),
	queryTerrain(terrain)
{
	if(!hypothetical && terrain.terrain)
		throw std::invalid_argument("Terrain-only query metadata requires a hypothetical identity");
	health.init(); //???
	doubleWideCached = battle::CUnitState::doubleWide();
}

std::optional<TerrainId> CStack::getQueryTerrain() const
{
	return queryTerrain.terrain;
}
