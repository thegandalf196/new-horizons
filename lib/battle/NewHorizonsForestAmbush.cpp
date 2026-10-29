/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "NewHorizonsForestAmbush.h"
#include "CUnitState.h"
#include "CBattleInfoCallback.h"
#include "../entities/hero/NewHorizonsForestAmbush.h"
#include "../CCreatureHandler.h"
namespace newHorizonsForestAmbush
{
bool entitled(const CBattleInfoCallback & battle, const battle::Unit * unit)
{
	if(!unit || !unit->alive() || unit->isGhost() || unit->isTurret() || unit->isTimeStopped())
		return false;
	const auto * creature = unit->creatureId().toCreature();
	if(!creature || (creature->getJsonKey() != "core:woodElf" && creature->getJsonKey() != "core:grandElf"))
		return false;
	const auto side = battle.playerToSide(battle.battleGetOwner(unit));
	return (side == BattleSide::ATTACKER || side == BattleSide::DEFENDER)
		&& newHorizonsHeroes::forestAmbushEnabled(battle.battleGetFightingHero(side));
}
bool hasBattlePolicy(const CBattleInfoCallback & battle)
{
	return newHorizonsHeroes::forestAmbushEnabled(battle.battleGetFightingHero(BattleSide::ATTACKER))
		|| newHorizonsHeroes::forestAmbushEnabled(battle.battleGetFightingHero(BattleSide::DEFENDER));
}
bool concealed(const battle::Unit * unit)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	return state && state->alive() && !state->isGhost() && state->forestAmbush.concealed;
}
bool protectsDirectTarget(const battle::Unit * unit)
{
	return unit && (unit->hasBonusOfType(BonusType::SANCTIFIED) || concealed(unit));
}
int shotLuck(const CBattleInfoCallback & battle, const battle::Unit * unit, bool shooting)
{
	const auto * state = dynamic_cast<const battle::CUnitState *>(unit);
	if(!shooting || !state || !entitled(battle, unit)
		|| unit->hasBonusOfType(BonusType::SPELL_LIKE_ATTACK))
		return 0;
	const auto * active = battle.battleActiveUnit();
	const bool ready = state->forestAmbush.concealed || (state->forestAmbush.shotPending
		&& active && active->unitId() == unit->unitId());
	return ready ? FIRST_SHOT_LUCK : 0;
}
void acceptWait(const CBattleInfoCallback & battle, battle::CUnitState & unit)
{
	if(hasBattlePolicy(battle) || unit.forestAmbush.hasState())
		unit.forestAmbush.acceptWait(entitled(battle, &unit));
}
void damageFromAttack(const CBattleInfoCallback & battle, battle::CUnitState & unit,
	int64_t & amount, battle::DamageProvenance provenance)
{
	receiveAttack(battle, unit);
	unit.damage(amount, false, provenance);
}
void receiveAttack(const CBattleInfoCallback & battle, battle::CUnitState & unit)
{
	if(hasBattlePolicy(battle) || unit.forestAmbush.hasState())
		unit.forestAmbush.receiveAttack();
}
}
