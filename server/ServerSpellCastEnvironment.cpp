/*
 * ServerSpellCastEnvironment.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "ServerSpellCastEnvironment.h"

#include "CGameHandler.h"
#include "battles/BattleProcessor.h"
#include "queries/QueriesProcessor.h"
#include "queries/CQuery.h"

#include "../lib/battle/IBattleInfoCallback.h"
#include "../lib/battle/CBattleInfoCallback.h"
#include "../lib/battle/IBattleState.h"
#include "../lib/battle/Unit.h"
#include "../lib/callback/GameRandomizer.h"
#include "../lib/gameState/CGameState.h"
#include "../lib/mapObjects/army/CArmedInstance.h"
#include "../lib/networkPacks/PacksForClientBattle.h"
#include "../lib/networkPacks/SetStackEffect.h"
#include "../lib/spells/ExternalCaster.h"

///ServerSpellCastEnvironment
ServerSpellCastEnvironment::ServerSpellCastEnvironment(CGameHandler * gh)
	: gh(gh)
{
}

bool ServerSpellCastEnvironment::describeChanges() const
{
	return true;
}

void ServerSpellCastEnvironment::complain(const std::string & problem)
{
	gh->complain(problem);
}

vstd::RNG * ServerSpellCastEnvironment::getRNG()
{
	return &gh->getRandomGenerator();
}

bool ServerSpellCastEnvironment::rollCombatAbility(const IBattleInfoCallback & battle, const battle::Unit & actor, int percentageChance)
{
	const auto * army = battle.getBattle()->getSideArmy(actor.unitSide());
	return gh->randomizer->rollFavorableCreatureAbility(army->id, actor, percentageChance);
}

bool ServerSpellCastEnvironment::rollHostileCombatAbility(const IBattleInfoCallback & battle,
	const battle::Unit & actor, const battle::Unit & recipient, int percentageChance)
{
	const auto * army = battle.getBattle()->getSideArmy(actor.unitSide());
	const int effectiveChance = gh->randomizer->prepareFavorableCreatureAbilityChance(actor, percentageChance);
	const auto draw = [this, army, effectiveChance]()
	{
		return gh->randomizer->rollCombatAbility(army->id, effectiveChance);
	};

	const auto * cb = dynamic_cast<const CBattleInfoCallback *>(&battle);
	if(!cb)
		return draw();
	const BattleSide actorControllerSide = cb->playerToSide(cb->battleGetOwner(&actor));
	const BattleSide harmedSide = cb->playerToSide(cb->battleGetOwner(&recipient));
	const auto validBattleSide = [](BattleSide side)
	{
		return side == BattleSide::ATTACKER || side == BattleSide::DEFENDER;
	};
	const bool hostile = validBattleSide(actorControllerSide) && validBattleSide(harmedSide)
		&& actorControllerSide != harmedSide;
	if(!actor.alive() || !recipient.alive() || !recipient.isValidTarget(false) || recipient.isInvincible()
		|| !hostile || effectiveChance <= 0 || effectiveChance >= 100)
		return draw();

	return resolveAdverseCombatRoll(battle.getBattle()->getBattleID(), harmedSide, true, true, draw);
}

std::function<void()> ServerSpellCastEnvironment::prepareAdventureSpellCastCompletion(const spells::Caster * caster, SpellID spell)
{
	if(!caster || dynamic_cast<const spells::ExternalCaster *>(caster))
		return {};

	if(const auto * hero = caster->getHeroCaster())
		return gh->prepareChargeBasedSpellCompletion(hero->id, spell);

	return {};
}

bool ServerSpellCastEnvironment::resolveAdverseCombatRoll(const BattleID & battleID, BattleSide affectedSide,
	bool stochastic, bool adverseOnTrue, const std::function<bool()> & draw)
{
	return gh->battles->resolveAdverseCombatRoll(battleID, affectedSide, stochastic, adverseOnTrue, draw);
}

void ServerSpellCastEnvironment::apply(CPackForClient & pack)
{
	gh->sendAndApply(pack);
}

void ServerSpellCastEnvironment::apply(BattleLogMessage & pack)
{
	gh->sendAndApply(pack);
}

void ServerSpellCastEnvironment::apply(BattleStackMoved & pack)
{
	gh->sendAndApply(pack);
}

void ServerSpellCastEnvironment::apply(BattleUnitsChanged & pack)
{
	gh->sendAndApply(pack);
}

void ServerSpellCastEnvironment::apply(SetStackEffect & pack)
{
	gh->sendAndApply(pack);
}

void ServerSpellCastEnvironment::apply(StacksInjured & pack)
{
	gh->sendAndApply(pack);
}

void ServerSpellCastEnvironment::apply(BattleObstaclesChanged & pack)
{
	gh->sendAndApply(pack);
}

void ServerSpellCastEnvironment::apply(CatapultAttack & pack)
{
	gh->sendAndApply(pack);
}

const IGameInfoCallback * ServerSpellCastEnvironment::getCb() const
{
	return &gh->gameInfo();
}

const CMap * ServerSpellCastEnvironment::getMap() const
{
	return &gh->gameState().getMap();
}

bool ServerSpellCastEnvironment::moveHero(ObjectInstanceID hid, int3 dst, EMovementMode mode)
{
	return gh->moveHero(hid, dst, mode, false);
}

void ServerSpellCastEnvironment::createBoat(const int3 & visitablePosition, BoatId type, PlayerColor initiator)
{
	return gh->createBoat(visitablePosition, type, initiator);
}

void ServerSpellCastEnvironment::showGarrisonDialog(ObjectInstanceID upobj, ObjectInstanceID hid, bool removableUnits, const MetaString & customTitle)
{
	gh->showGarrisonDialog(upobj, hid, removableUnits, customTitle);
}

void ServerSpellCastEnvironment::genericQuery(Query * request, PlayerColor color, std::function<void(std::optional<int32_t>)> callback)
{
	auto query = std::make_shared<CGenericQuery>(gh, color, callback);
	request->queryID = query->queryID;
	gh->queries->addQuery(query);
	gh->sendAndApply(*request);
}
