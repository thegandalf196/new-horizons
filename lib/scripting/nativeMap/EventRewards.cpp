/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "EventRuntime.h"
#include "EventCodes.h"
#include "../../callback/IGameEventCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../mapObjects/CGTownInstance.h"
#include "../../mapObjects/army/CStackBasicDescriptor.h"
#include "../../networkPacks/ArtifactLocation.h"
#include "../../networkPacks/PacksForClient.h"

namespace scripting::nativeMap
{
namespace
{
void takeArtifact(IGameEventCallback & server, const CGHeroInstance & hero, ArtifactID id)
{
	if(!hero.hasArt(id))
		if(const auto * assembly = hero.getCombinedArtWithPart(id))
		{
			DisassembledArtifact packet;
			packet.al = ArtifactLocation(hero.id, hero.getArtPos(assembly));
			server.sendAndApply(packet);
		}
	if(hero.hasArt(id)) server.removeArtifact(ArtifactLocation(hero.id, hero.getArtPos(id, false)));
}
void spellPoints(IGameEventCallback & server, const CGHeroInstance & hero, int amount, int mode)
{
	const int64_t magnitude = std::abs(static_cast<int64_t>(amount));
	if(mode == 0)
	{
		if(amount >= 0) server.restoreSpellPoints(hero.id, amount);
		else server.spendSpellPoints(hero.id, std::min(magnitude, hero.getManaAvailable()));
	}
	else if(mode == 1)
	{
		if(amount >= 0) server.spendSpellPoints(hero.id, std::min(magnitude, hero.getManaAvailable()));
		else server.restoreSpellPoints(hero.id, static_cast<int32_t>(std::min<int64_t>(magnitude, std::numeric_limits<int32_t>::max())));
	}
	else
	{
		const int64_t target = std::max<int64_t>(0, amount), current = hero.getManaAvailable();
		if(target < current) server.spendSpellPoints(hero.id, current - target);
		else if(target > current) server.restoreSpellPoints(hero.id, static_cast<int32_t>(std::min<int64_t>(target - current, std::numeric_limits<int32_t>::max())));
	}
}
}
void Runtime::reward(IGameEventCallback & server, Continuation & context, const JsonNode & node)
{
	const auto code = static_cast<HotaScriptActions>(node["op"].Integer());
	if(code == HotaScriptActions::RESOURCES)
	{
		std::vector<int> amounts;
		for(const auto & amount : node["amounts"].Vector()) amounts.push_back(static_cast<int>(expression(server, context, amount)));
		for(size_t i = 0; i < amounts.size(); ++i) if(amounts[i] != 0) server.giveResource(player(context), GameResID(i), amounts[i]);
		return;
	}
	if(code == HotaScriptActions::CONSTRUCT_BUILDING)
	{
		server.buildStructureForced(town(context).id, BuildingID(node["building"].Integer())); return;
	}
	if(code == HotaScriptActions::CREATURES_TO_HIRE)
	{
		const int amount = expression(server, context, node["amount"]);
		const auto & target = town(context);
		const int level = node["level"].Integer();
		if(level < 0 || level >= static_cast<int>(target.creatures.size())) { logGlobal->error("Native grantCreaturesToHire: invalid tier %d", level); return; }
		SetAvailableCreatures packet;
		packet.tid = target.id; packet.creatures = target.creatures;
		packet.creatures[level].first = std::max(0, static_cast<int>(packet.creatures[level].first) + amount);
		server.sendAndApply(packet); return;
	}
	const auto & target = hero(context);
	switch(code)
	{
		case HotaScriptActions::ARTIFACT: case HotaScriptActions::WAR_MACHINE: case HotaScriptActions::SPELLBOOK:
		{
			const ArtifactID id = code == HotaScriptActions::SPELLBOOK ? ArtifactID::SPELLBOOK
				: code == HotaScriptActions::ARTIFACT && !node["take"].Bool() && !node["scroll"].isNull() ? ArtifactID::SPELL_SCROLL
				: ArtifactID::decode(node["entity"].String());
			if(node["take"].Bool()) takeArtifact(server, target, id);
			else if(code == HotaScriptActions::ARTIFACT && !node["scroll"].isNull())
				server.giveHeroNewScroll(&target, SpellID::decode(node["scroll"].String()), ArtifactPosition::FIRST_AVAILABLE);
			else server.giveHeroNewArtifact(&target, id, ArtifactPosition::FIRST_AVAILABLE);
			break;
		}
		case HotaScriptActions::SPELL:
			server.changeSpells(&target, true, {SpellID::decode(node["entity"].String())}); break;
		case HotaScriptActions::CREATURES:
		{
			const CreatureID id = CreatureID::decode(node["entity"].String());
			const int amount = expression(server, context, node["amount"]);
			if(node["take"].Bool()) server.takeCreatures(target.id, {CStackBasicDescriptor(id, amount)});
			else { CCreatureSet army; army.addToSlot(army.getFreeSlot(), id, amount); server.giveCreatures(&target, army); }
			break;
		}
		case HotaScriptActions::EXPERIENCE:
			server.giveExperience(&target, static_cast<int64_t>(expression(server, context, node["amount"]))); break;
		case HotaScriptActions::PRIMARY_SKILL:
			server.changePrimSkill(&target, PrimarySkill(node["skill"].Integer()), static_cast<int>(expression(server, context, node["amount"])), ChangeValueMode::RELATIVE); break;
		case HotaScriptActions::SECONDARY_SKILL:
			server.changeSecSkill(&target, SecondarySkill(SecondarySkill::decode(node["entity"].String())), node["mastery"].Integer(), ChangeValueMode::ABSOLUTE); break;
		case HotaScriptActions::MORALE: case HotaScriptActions::LUCK:
		{
			Bonus bonus(BonusDuration::ONE_BATTLE, code == HotaScriptActions::MORALE ? BonusType::MORALE : BonusType::LUCK, BonusSource::OTHER, node["amount"].Integer(), BonusSourceID());
			GiveBonus packet(GiveBonus::ETarget::OBJECT, target.id, bonus); server.giveHeroBonus(&packet); break;
		}
		case HotaScriptActions::SPELL_POINTS:
			spellPoints(server, target, expression(server, context, node["amount"]), node["mode"].Integer()); break;
		case HotaScriptActions::MOVEMENT_POINTS:
		{
			const int amount = expression(server, context, node["amount"]), mode = node["mode"].Integer();
			const int current = target.movementPointsRemaining();
			server.setMovePoints(target.id, mode == 0 ? current + amount : mode == 1 ? std::max(0, current - amount) : amount); break;
		}
		default: throw std::runtime_error("Unknown native map action opcode");
	}
}
}
