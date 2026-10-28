/*
 * Part of VCMI engine. Authors: listed in AUTHORS.
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "../../lib/StdInc.h"
#include "BattleActionExport.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/battle/BattleInfo.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/networkPacks/PacksForClientBattle.h"
#include "../../lib/networkPacks/PacksForServer.h"
#include "../../lib/serializer/CTypeList.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/texts/CGeneralTextHandler.h"

namespace battleActionExport
{
namespace
{
template<class Packet> bool identifyBattle(const CPack & packet, JsonNode & value, const char * kind)
{
	const auto * typed = dynamic_cast<const Packet *>(&packet);
	if(!typed)
		return false;
	value["kind"].String() = kind;
	value["battleID"].Integer() = typed->battleID.getNum();
	value["detailScope"].String() = "ownership only; full effect remains in source packet";
	return true;
}
template<class Range> JsonNode numbers(const Range & values)
{
	if(values.size() > 4096)
		throw std::runtime_error("Packet collection exceeds action-export item limit");
	JsonNode result;
	result.Vector();
	for(const auto value : values)
		result.Vector().emplace_back(static_cast<int64_t>(value));
	return result;
}
void spell(JsonNode & value, SpellID id)
{
	value["spellID"].Integer() = id.getNum();
	if(LIBRARY && LIBRARY->spellh && id.getNum() >= 0
		&& static_cast<size_t>(id.getNum()) < LIBRARY->spellh->objects.size()
		&& LIBRARY->spellh->objects[id.getNum()])
	{
		value["spellKey"].String() = LIBRARY->spellh->objects[id.getNum()]->getJsonKey();
		value["spellName"].String() = LIBRARY->spellh->objects[id.getNum()]->getNameTranslated();
	}
}
JsonNode startData(const BattleStart & packet)
{
	JsonNode value;
	value["kind"].String() = "BattleStart";
	value["battleID"].Integer() = packet.battleID.getNum();
	if(!packet.info)
		return value;
	if(packet.info->getBattleID() != packet.battleID || packet.info->stacks.size() > 4096)
		throw std::runtime_error("Invalid initial battle descriptor");
	value["initialRound"].Integer() = packet.info->getRound();
	value["stackStateKind"].String() = "serialized_initial_descriptor";
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		JsonNode army;
		army["side"].Integer() = static_cast<int>(side);
		army["player"].Integer() = packet.info->getSidePlayer(side).getNum();
		army["heroObjectID"].Integer() = packet.info->getSide(side).heroID.getNum();
		army["armyObjectID"].Integer() = packet.info->getSide(side).armyObjectID.getNum();
		if(const auto * hero = packet.info->getSideHero(side))
		{
			army["heroTypeID"].Integer() = hero->getHeroTypeID().getNum();
			army["heroKey"].String() = hero->getHeroType()->getJsonKey();
		}

		value["armies"].Vector().push_back(std::move(army));
	}
	for(const auto & unit : packet.info->stacks)
	{
		if(!unit)
			throw std::runtime_error("Null stack in initial battle descriptor");
		JsonNode stack;
		stack["unitID"].Integer() = unit->unitId();
		stack["side"].Integer() = static_cast<int>(unit->unitSide());
		stack["creatureID"].Integer() = unit->creatureId().getNum();
		stack["count"].Integer() = unit->unitBaseAmount();
		stack["hex"].Integer() = unit->initialPosition.toInt();
		const auto id = unit->creatureId().getNum();
		if(LIBRARY && LIBRARY->creh && id >= 0 && static_cast<size_t>(id) < LIBRARY->creh->objects.size()
			&& LIBRARY->creh->objects[id])
		{
			stack["creatureKey"].String() = LIBRARY->creh->objects[id]->getJsonKey();
			stack["creatureName"].String() = LIBRARY->creh->objects[id]->getNamePluralTranslated();
		}
		value["stacks"].Vector().push_back(std::move(stack));
	}
	return value;
}
JsonNode actionData(const StartAction & packet)
{
	JsonNode value;
	value["kind"].String() = "StartAction";
	value["status"].String() = "published_action_start; not proof of every requested effect";
	value["battleID"].Integer() = packet.battleID.getNum();
	value["side"].Integer() = static_cast<int>(packet.ba.side);
	value["stackNumber"].Integer() = packet.ba.stackNumber;
	value["actionType"].Integer() = static_cast<int>(packet.ba.actionType);
	value["command"].Integer() = static_cast<int>(packet.ba.command);
	value["actionText"].String() = packet.ba.toString();
	value["spellOvercharge"].Integer() = packet.ba.spellOvercharge;
	value["metamagicFollowup"].Bool() = packet.ba.metamagicFollowup;
	spell(value, packet.ba.spell);
	if(packet.ba.target.size() > 4096)
		throw std::runtime_error("Action target list exceeds export limit");
	for(const auto & target : packet.ba.target)
	{
		JsonNode item;
		item["unitID"].Integer() = target.unitValue;
		item["hex"].Integer() = target.hexValue.toInt();
		value["targets"].Vector().push_back(std::move(item));
	}
	if(packet.orderState)
	{
		auto & order = value["orderState"];
		order["fieldScope"].String() = "issuance identity and targets; full snapshot remains in source packet";
		order["command"].Integer() = static_cast<int>(packet.orderState->command);
		order["issuedRound"].Integer() = packet.orderState->issuedRound;
		order["primaryTargetUnitID"].Integer() = packet.orderState->primaryTargetUnitId;
		order["secondaryTargetUnitID"].Integer() = packet.orderState->secondaryTargetUnitId;
		order["warcastingBonusPercent"].Integer() = packet.orderState->warcastingBonusPercent;
		order["hasRevision"].Bool() = packet.orderState->revision.has_value();
	}
	return value;
}
JsonNode spellData(const BattleSpellCast & packet)
{
	JsonNode value;
	value["kind"].String() = "BattleSpellCast";
	value["status"].String() = "published_cast_announcement; counterspell may suppress effects";
	value["battleID"].Integer() = packet.battleID.getNum();
	spell(value, packet.spellID);
	value["side"].Integer() = static_cast<int>(packet.side);
	value["castByHero"].Bool() = packet.castByHero;
	value["casterStack"].Integer() = packet.casterStack;
	value["activeCast"].Bool() = packet.activeCast;
	value["counterspellNegated"].Bool() = packet.counterspellNegated;
	value["counterspellSide"].Integer() = static_cast<int>(packet.counterspellSide);
	value["metamagicFollowup"].Bool() = packet.metamagicFollowup;
	value["metamagicGrand"].Bool() = packet.metamagicGrand;
	value["metamagicManaRefund"].Integer() = packet.metamagicManaRefund;
	value["paidHeroManaCost"].Integer() = packet.paidHeroManaCost;
	value["paidCounterspellManaCost"].Integer() = packet.paidCounterspellManaCost;
	value["manaGained"].Integer() = packet.manaGained;
	value["affectedCres"] = numbers(packet.affectedCres);
	value["resistedCres"] = numbers(packet.resistedCres);
	value["reflectedCres"] = numbers(packet.reflectedCres);
	return value;
}
JsonNode hits(const std::vector<BattleStackAttacked> & source)
{
	if(source.size() > 4096)
		throw std::runtime_error("Damage victim list exceeds export limit");
	JsonNode result;
	result.Vector();
	for(const auto & hit : source)
	{
		JsonNode item;
		item["targetUnitID"].Integer() = hit.stackAttacked;
		item["attackerUnitID"].Integer() = hit.attackerID;
		item["damage"].Integer() = hit.damageAmount;
		item["killed"].Integer() = hit.killedAmount;
		item["flags"].Integer() = hit.flags;
		item["newState"] = hit.newState.data;
		result.Vector().push_back(std::move(item));
	}
	return result;
}
}
JsonNode describe(const CPack & packet)
{
	if(const auto * start = dynamic_cast<const BattleStart *>(&packet)) return startData(*start);
	if(const auto * action = dynamic_cast<const StartAction *>(&packet)) return actionData(*action);
	if(const auto * cast = dynamic_cast<const BattleSpellCast *>(&packet)) return spellData(*cast);
	JsonNode value;
	value["kind"].String() = "other_recorded_packet";
	value["ownership"].String() = "unclassified";
	if(const auto * next = dynamic_cast<const BattleNextRound *>(&packet))
	{
		value["kind"].String() = "BattleNextRound";
		value["battleID"].Integer() = next->battleID.getNum();
	}
	else if(const auto * active = dynamic_cast<const BattleSetActiveStack *>(&packet))
	{
		value["kind"].String() = "BattleSetActiveStack";
		value["battleID"].Integer() = active->battleID.getNum();
		value["stack"].Integer() = active->stack;
		value["reason"].Integer() = static_cast<int>(active->reason);
	}
	else if(const auto * end = dynamic_cast<const EndAction *>(&packet))
	{
		value["kind"].String() = "EndAction";
		value["battleID"].Integer() = end->battleID.getNum();
	}
	else if(const auto * attack = dynamic_cast<const BattleAttack *>(&packet))
	{
		value["kind"].String() = "BattleAttack";
		value["battleID"].Integer() = attack->battleID.getNum();
		value["attackerUnitID"].Integer() = attack->stackAttacking;
		value["flags"].Integer() = attack->flags;
		value["hits"] = hits(attack->bsa);
	}
	else if(const auto * damage = dynamic_cast<const StacksInjured *>(&packet))
	{
		value["kind"].String() = "StacksInjured";
		value["battleID"].Integer() = damage->battleID.getNum();
		value["hits"] = hits(damage->stacks);
	}
	else if(const auto * result = dynamic_cast<const BattleResult *>(&packet))
	{
		value["kind"].String() = "BattleResult";
		value["battleID"].Integer() = result->battleID.getNum();
		value["winner"].Integer() = static_cast<int>(result->winner);
		value["result"].Integer() = static_cast<int>(result->result);
	}
	else if(const auto * ended = dynamic_cast<const BattleEnded *>(&packet))
	{
		value["kind"].String() = "BattleEnded";
		value["battleID"].Integer() = ended->battleID.getNum();
		value["victor"].Integer() = ended->victor.getNum();
		value["loser"].Integer() = ended->loser.getNum();
	}
	else if(const auto * log = dynamic_cast<const BattleLogMessage *>(&packet))
	{
		value["kind"].String() = "BattleLogMessage";
		value["battleID"].Integer() = log->battleID.getNum();
		if(log->lines.size() > 1024) throw std::runtime_error("Battle log packet exceeds export limit");
		for(const auto & line : log->lines)
		{
			const auto text = line.toString(LIBRARY ? LIBRARY->generaltexth.get() : nullptr);
			if(text.size() > 4096) throw std::runtime_error("Battle log line exceeds export limit");
			value["lines"].Vector().emplace_back(text);
		}
	}
	else if(identifyBattle<BattleStackMoved>(packet, value, "BattleStackMoved")
		|| identifyBattle<BattleUnitsChanged>(packet, value, "BattleUnitsChanged")
		|| identifyBattle<BattleHeroOrderStateChanged>(packet, value, "BattleHeroOrderStateChanged")
		|| identifyBattle<BattleResultsApplied>(packet, value, "BattleResultsApplied"))
	{
		// Typed IDs are retained even when this tool does not summarize the effect.
	}
	else if(const auto * applied = dynamic_cast<const PackageApplied *>(&packet))
	{
		value["kind"].String() = "PackageApplied";
		value["ownership"].String() = "no_battle_identity";
		value["association"].String() = "interval only; acknowledgment carries no battle or action payload";
		value["player"].Integer() = applied->player.getNum();
		value["requestID"].Integer() = applied->requestID;
		value["requestTypeID"].Integer() = applied->packType;
		value["result"].Bool() = applied->result;
		MakeAction action;
		value["battleActionRequest"].Bool() = applied->packType == CTypeList::getInstance().getTypeID(&action);
	}
	if(value["battleID"].isNumber())
		value["ownership"].String() = "battle_identity_from_packet";
	return value;
}
}
