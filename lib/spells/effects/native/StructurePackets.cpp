/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "StructureCommon.h"
#include "../../ISpellMechanics.h"
#include "../../NewHorizonsMagic.h"
#include "../../../battle/SiegeInfo.h"
#include "../../../networkPacks/PacksForClientBattle.h"

namespace spells::effects::native::structure
{
void addObstacle(const nativeEvents::Context & context, SpellCreatedObstacle obstacle)
{
	if(obstacle.casterPowerDivisor <= 0) throw std::runtime_error("Invalid spell obstacle power divisor");
	if(obstacle.movementCost < 0 || obstacle.movementCost > SpellCreatedObstacle::MAX_MOVEMENT_COST)
		throw std::runtime_error("Invalid spell obstacle movement cost");
	obstacle.damageSnapshot = obstacle.damageSnapshot || (SpellID(obstacle.ID) == SpellID::LAND_MINE
		&& obstacle.casterPowerDivisor == newHorizonsMagic::DIRECT_DAMAGE_POWER_DIVISOR
		&& obstacle.minimalDamage > 0 && obstacle.hidden && !obstacle.nativeVisible && obstacle.removeOnTrigger);
	obstacle.uniqueID = context.battle.nextObstacleId();
	BattleObstaclesChanged packet;
	packet.battleID = context.battle.getBattle()->getBattleID();
	obstacle.toInfo(packet.change);
	context.server.apply(packet);
}

void removeObstacle(const nativeEvents::Context & context, const std::shared_ptr<const CObstacleInstance> & obstacle)
{
	if(!obstacle) return;
	BattleObstaclesChanged packet;
	packet.battleID = context.battle.getBattle()->getBattleID();
	packet.change = ObstacleChanges(obstacle->uniqueID, BattleChanges::EOperation::REMOVE);
	const_cast<CObstacleInstance *>(obstacle.get())->toInfo(packet.change, BattleChanges::EOperation::REMOVE);
	context.server.apply(packet);
}

void catapultAttack(const nativeEvents::Context & context, const battle::Unit * attacker, EWallPart part, int damage)
{
	const auto & battle = context.battle;
	ui16 structuralDamage = 0;
	if(damage > 0 && attacker && battle.getWallStructuralHP(part) > 0)
		structuralDamage = static_cast<ui16>(std::clamp<int32_t>(battle.battleGetCatapultStructuralDamage(attacker, damage), 0, 65535));
	CatapultAttack packet;
	packet.battleID = battle.getBattle()->getBattleID();
	packet.attacker = attacker ? attacker->unitId() : -1;
	packet.attackedPart = part;
	packet.destinationTile = battle.wallPartToBattleHex(part).toInt();
	packet.damageDealt = static_cast<ui8>(std::clamp(damage, 0, 255));
	packet.structuralDamage = structuralDamage;
	BreachmakerPreview overflow;
	if(structuralDamage > 0 && attacker) overflow = battle.battleGetBreachmakerPreview(attacker, part, structuralDamage);
	packet.killedTowerShooter = -1;
	if(part == EWallPart::KEEP || part == EWallPart::BOTTOM_TOWER || part == EWallPart::UPPER_TOWER)
	{
		const auto health = battle.getWallStructuralHP(part);
		const auto appliedDamage = structuralDamage > 0 ? structuralDamage : packet.damageDealt;
		const auto after = health > 0 ? SiegeInfo::stateFromStructuralHP(part, health - appliedDamage)
			: SiegeInfo::applyDamage(battle.battleGetWallState(part), appliedDamage);
		if(after == EWallState::DESTROYED)
			if(const auto * shooter = battle.battleGetUnitByPos(battle.getTowerShooterHex(part), false); shooter && !shooter->isGhost())
				packet.killedTowerShooter = shooter->unitId();
	}
	context.server.apply(packet);
	if(overflow.damage > 0) damageFortification(context, overflow.part, overflow.damage);
}

void damageFortification(const nativeEvents::Context & context, EWallPart part, int damage)
{
	if(damage < 0 || damage > std::numeric_limits<ui16>::max()) throw std::runtime_error("Invalid absolute fortification damage");
	const auto & battle = context.battle;
	if(damage == 0 || !battle.hasFortifications() || !battle.isWallPartAttackable(part)) return;
	const auto destination = battle.wallPartToBattleHex(part);
	const auto health = battle.getWallStructuralHP(part);
	if(!destination.isValid() || health <= 0) return;
	CatapultAttack packet;
	packet.battleID = battle.getBattle()->getBattleID();
	packet.attacker = -1;
	packet.attackedPart = part;
	packet.destinationTile = destination.toInt();
	packet.damageDealt = 1;
	packet.structuralDamage = static_cast<ui16>(damage);
	packet.killedTowerShooter = -1;
	if(part == EWallPart::KEEP || part == EWallPart::BOTTOM_TOWER || part == EWallPart::UPPER_TOWER)
		if(SiegeInfo::stateFromStructuralHP(part, health - damage) == EWallState::DESTROYED)
			if(const auto * shooter = battle.battleGetUnitByPos(battle.getTowerShooterHex(part), false); shooter && !shooter->isGhost())
				packet.killedTowerShooter = shooter->unitId();
	context.server.apply(packet);
}

SpellCreatedObstacle obstacle(const Mechanics & mechanics, BattleHex position)
{
	SpellCreatedObstacle result;
	result.pos = position;
	result.ID = mechanics.getSpellId().getNum();
	result.casterSpellPower = mechanics.getEffectPower();
	result.casterPowerDivisor = mechanics.getEffectPowerDivisor();
	result.spellLevel = mechanics.getEffectLevel();
	result.casterSide = mechanics.getCasterSide();
	result.trigger = SpellID::NONE;
	return result;
}
}
