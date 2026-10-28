/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsManaLeeches.h"
#include "../CGameHandler.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
namespace newHorizonsManaLeeches
{
Resolution prepare(const CBattleInfoCallback & battle, const battle::Unit * source,
	const battle::Unit * primary, bool ranged, bool physical, bool secondary,
	const std::vector<BattleStackAttacked> & hits, bool sequenceSpent)
{
	if(sequenceSpent)
		return {};
	const auto rule = entitlement(battle, source, primary, ranged, physical, secondary);
	if(rule.amount <= 0)
		return {};
	const auto primaryHit = std::ranges::find(hits, primary->unitId(), &BattleStackAttacked::stackAttacked);
	const auto primaryDamage = primaryHit == hits.end() ? 0 : primaryHit->damageAmount;
	const auto * hero = battle.battleGetFightingHero(rule.beneficiary);
	const auto * enemy = battle.battleGetFightingHero(rule.victim);
	return {transfer(rule, primaryDamage, sequenceSpent, enemy->getManaAvailable(),
		hero->getNormalSpellPoints(), hero->manaLimit()), battle.getBattle()->getBattleID(), hero->id, enemy->id};
}
void apply(CGameHandler & handler, const Resolution & resolution, bool * sequenceSpent)
{
	if(resolution.amounts.stolen <= 0)
		return;
	SetMana spend(resolution.victim, SetMana::Operation::SPEND, resolution.amounts.stolen);
	handler.sendAndApply(spend);
	if(resolution.amounts.restored > 0)
	{
		SetMana restore(resolution.beneficiary, SetMana::Operation::RESTORE_NORMAL_MANA_LEECHES, resolution.amounts.restored);
		handler.sendAndApply(restore);
	}
	if(sequenceSpent)
		*sequenceSpent = true;
	BattleLogMessage feedback;
	feedback.battleID = resolution.battleID;
	MetaString line;
	line.appendRawString("Mana Leeches steals ");
	line.appendNumber(resolution.amounts.stolen);
	line.appendRawString(" enemy Mana");
	if(resolution.amounts.restored > 0)
	{
		line.appendRawString(" and restores ");
		line.appendNumber(resolution.amounts.restored);
		line.appendRawString(" Normal Mana");
	}
	line.appendRawString(".");
	feedback.lines.push_back(std::move(line));
	handler.sendAndApply(feedback);
}
}
