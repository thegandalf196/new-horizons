/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "GameStatePackVisitor.h"
#include "CGameState.h"
#include "../battle/BattleInfo.h"
#include "../battle/ResidualLife.h"
#include "../entities/hero/NewHorizonsNecropolisCombatSpecialties.h"
#include "../mapObjects/CGHeroInstance.h"
void GameStatePackVisitor::visitRecordResidualLife(RecordResidualLife & pack)
{
	if(pack.damage < 0 || (pack.side != BattleSide::ATTACKER && pack.side != BattleSide::DEFENDER))
		throw std::runtime_error("Invalid Residual Life receipt");
	auto * battle = gs.getBattle(pack.battleID);
	if(!battle) throw std::runtime_error("Residual Life requires an active battle");
	auto & side = battle->getSide(pack.side);
	auto * hero = gs.getHero(side.heroID);
	const auto threshold = newHorizonsHeroes::residualLifeThreshold(hero);
	const auto limit = newHorizonsHeroes::residualLifeLimit(hero);
	const auto next = battle::residualLife::record(side.residualLifeDamage, pack.damage, threshold, limit);
	if(next.grant > 0 && !hero->grantBufferSpellPoints(next.grant))
		throw std::runtime_error("Invalid Residual Life Buffer grant");
	side.residualLifeDamage = next.accumulatedDamage;
}
