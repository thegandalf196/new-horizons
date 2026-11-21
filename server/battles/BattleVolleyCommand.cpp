/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleActionProcessor.h"
#include "../CGameHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include "../../lib/battle/NewHorizonsAttackControllers.h"
#include "../../lib/entities/hero/NewHorizonsCastleSpecialties.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"

namespace
{
/// Friendly ordinary shooters that have not acted this round, in unit order.
std::vector<uint32_t> volleyShooters(const CBattleInfoCallback & battle, BattleSide side, const battle::Unit & target)
{
	std::vector<uint32_t> result;
	for(const auto * unit : battle.battleGetUnitsIf([side](const battle::Unit * candidate)
		{ return candidate->unitSide() == side && candidate->alive() && !candidate->isGhost(); }))
		if(newHorizonsArchery::isOrdinaryPhysicalShooter(unit) && unit->willMove(0)
			&& battle.battleCanShoot(unit, target.getPosition()))
			result.push_back(unit->unitId());
	std::sort(result.begin(), result.end());
	return result;
}
}

void BattleActionProcessor::performVolleyCommand(const CBattleInfoCallback & battle, BattleSide side)
{
	const int percent = newHorizonsHeroes::volleyCommandDamagePercent(battle.battleGetFightingHero(side));
	const auto mark = percent > 0 ? battle.battleGetFocusFireState(side) : std::nullopt;
	if(!mark)
		return;
	const auto battleId = battle.getBattle()->getBattleID();
	const auto * nativeOwner = gameHandler->gameState().getBattle(battleId);
	const auto targetId = mark->targetUnitId;
	const auto * target = battle.battleGetStackByID(targetId, false);
	if(!target)
		return;
	const auto shooters = volleyShooters(battle, side, *target);
	if(shooters.empty())
		return;
	BattleLogMessage message;
	message.battleID = battleId;
	MetaString line;
	line.appendRawString("Volley Command: every ready shooter fires at the Focus Fire! target.");
	message.lines.push_back(std::move(line));
	gameHandler->sendAndApply(message);
	for(const auto shooterId : shooters)
	{
		if(gameHandler->gameState().getBattle(battleId) != nativeOwner)
			return;
		const auto * shooter = battle.battleGetStackByID(shooterId, false);
		target = battle.battleGetStackByID(targetId, false);
		if(!target || !target->alive())
			return;
		// Each shot uses the ordinary shot admission, so ammo and intervening
		// deaths or blockers are respected; the shooter keeps its activation.
		if(!shooter || !shooter->alive() || !shooter->shots.canUse()
			|| !battle.battleCanShoot(shooter, target->getPosition()))
			continue;
		const auto impact = target->getPosition();
		auto rain = beginRainOfArrows(battle, shooter, target);
		RelentlessAssaultActionContext relentless;
		int64_t kills = 0;
		makeAttack(battle, shooter, target, {.targetHex = impact, .first = true, .ranged = true,
			.archeryRangedDamageMultiplierPercent = percent, .hostileCreatureKills = &kills, .heroVolley = true},
			nullptr, &relentless, &rain);
		if(gameHandler->gameState().getBattle(battleId) != nativeOwner)
			return;
		shooter = battle.battleGetStackByID(shooterId, false);
		target = battle.battleGetStackByID(targetId, false);
		if(!shooter || !target)
			return;
		BonusList shooterBonuses = *shooter->getAllBonuses(Bonus::untilAfterAttackSequence);
		BonusList targetBonuses = *target->getAllBonuses(Bonus::untilAfterAttackSequence);
		performOrdinaryRangedRetaliation(battle, target, shooter);
		if(gameHandler->gameState().getBattle(battleId) != nativeOwner)
			return;
		shooter = battle.battleGetStackByID(shooterId, false);
		target = battle.battleGetStackByID(targetId, false);
		if(!shooter || !target)
			return;
		if(kills > 0)
			performRelentlessBarrage(battle, shooter, target, impact,
				newHorizonsAttackControllers::barrageShots(battle, *shooter), relentless, rain);
		if(gameHandler->gameState().getBattle(battleId) != nativeOwner)
			return;
		shooter = battle.battleGetStackByID(shooterId, false);
		target = battle.battleGetStackByID(targetId, false);
		if(!shooter || !target)
			return;
		removeBonuses(battle, shooter, shooterBonuses);
		removeBonuses(battle, target, targetBonuses);
		resolveRainOfArrows(battle, shooter, rain);
	}
}
