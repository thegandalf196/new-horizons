/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "ArcaneInspirationCompletion.h"
#include "ISpellMechanics.h"
#include "NewHorizonsMagic.h"
#include "ObstacleCasterProxy.h"
#include "../battle/ArcaneInspiration.h"
#include "../battle/CBattleInfoCallback.h"
#include "../battle/IBattleState.h"
#include "../battle/Unit.h"
#include "../bonuses/BonusList.h"
#include "../mapObjects/CGHeroInstance.h"
#include "../networkPacks/PacksForClientBattle.h"
#include "../networkPacks/SetStackEffect.h"
#include <vcmi/ServerCallback.h>
#include <set>
namespace arcaneInspiration
{
void publishSpellDamage(ServerCallback & server, const CBattleInfoCallback & battle,
	BattleSide side, std::span<const BattleStackAttacked> injuries)
{
	if(!battle.getBattle() || (side != BattleSide::ATTACKER && side != BattleSide::DEFENDER)
		|| !newHorizonsMagic::rulesActive(battle.getBattle()->getMagicRules())
		|| !profile(battle.battleGetFightingHero(side)).spellshock)
		return;
	SetStackEffect packet;
	packet.battleID = battle.getBattle()->getBattleID();
	const auto owner = battle.sideToPlayer(side);
	std::set<uint32_t> seen;
	for(const auto & injury : injuries)
	{
		const auto * unit = battle.battleGetUnitByID(injury.stackAttacked);
		if(injury.damageAmount <= 0 || !unit || !unit->alive() || unit->isGhost()
			|| unit->isInvincible() || battle.battleGetOwner(unit) == owner || !seen.insert(unit->unitId()).second)
			continue;
		std::vector<Bonus> previous;
		for(const auto & effect : *unit->getBonuses(CSelector(isSpellshock))) previous.push_back(*effect);
		if(!previous.empty()) packet.toRemove.emplace_back(unit->unitId(), std::move(previous));
		packet.toAdd.emplace_back(unit->unitId(), std::vector<Bonus>{spellshockMarker(owner, battle.battleGetRound())});
	}
	if(!packet.toAdd.empty())
	{
		server.apply(packet);
		server.recordArcaneInspirationEffects(packet);
	}
}
void publishDelayedSpellDamage(ServerCallback & server, const CBattleInfoCallback & battle,
	const spells::Mechanics & mechanics, std::span<const BattleStackAttacked> injuries)
{
	const auto * proxy = dynamic_cast<const spells::ObstacleCasterProxy *>(mechanics.caster);
	if(!proxy || !battle.getBattle()) return;
	const auto & obstacle = proxy->sourceObstacle();
	if(obstacle.casterSide != BattleSide::ATTACKER && obstacle.casterSide != BattleSide::DEFENDER) return;
	const auto * hero = battle.battleGetFightingHero(obstacle.casterSide);
	const auto & savedHero = obstacle.capturedMdrPenetration["sourceHeroCasterId"];
	if(!hero || proxy->delegatedCaster() != hero || savedHero.getType() != JsonNode::JsonType::DATA_INTEGER
		|| savedHero.Integer() != hero->id.getNum()) return;
	publishSpellDamage(server, battle, obstacle.casterSide, injuries);
}
}
