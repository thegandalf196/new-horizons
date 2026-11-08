/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt and AUTHORS. */
#include "StdInc.h"
#include "EventRuntime.h"
#include "../../callback/IGameEventCallback.h"
#include "../../mapObjects/CGHeroInstance.h"
#include "../../networkPacks/PacksForClient.h"
#include "../../networkPacks/StackLocation.h"

namespace scripting::nativeMap
{
std::vector<Component> Runtime::components(IGameEventCallback & server, const Continuation & context, const JsonNode & images) const
{
	std::vector<Component> result;
	for(const auto & image : images.Vector())
	{
		Component component;
		component.type = static_cast<ComponentType>(image["type"].Integer());
		component.value = static_cast<int>(expression(server, context, image["value"]));
		const int subtype = image["subtype"].Integer();
		switch(component.type)
		{
			case ComponentType::PRIM_SKILL: component.subType = PrimarySkill(subtype); break;
			case ComponentType::SEC_SKILL: component.subType = SecondarySkill(subtype); break;
			case ComponentType::RESOURCE: case ComponentType::RESOURCE_PER_DAY: component.subType = GameResID(subtype); break;
			case ComponentType::CREATURE: component.subType = CreatureID(subtype); break;
			case ComponentType::ARTIFACT: case ComponentType::SPELL_SCROLL: component.subType = ArtifactID(subtype); break;
			case ComponentType::SPELL: component.subType = SpellID(subtype); break;
			case ComponentType::HERO_PORTRAIT: component.subType = HeroTypeID(subtype); break;
			case ComponentType::FLAG: component.subType = PlayerColor(subtype); break;
			default: break;
		}
		result.push_back(std::move(component));
	}
	return result;
}
void Runtime::message(IGameEventCallback & server, PlayerColor player, const std::string & text, const std::vector<Component> & components) const
{
	InfoWindow window;
	window.player = player; window.text = MetaString::createFromTextID(text); window.components = components;
	server.showInfoDialog(&window);
}
void Runtime::dialog(IGameEventCallback & server, PlayerColor player, const std::string & text, bool yesNo, const std::vector<Component> & components) const
{
	BlockingDialog window(yesNo, false);
	window.player = player; window.text = MetaString::createFromTextID(text); window.components = components;
	server.showScriptDialog(&window);
}
void Runtime::combat(IGameEventCallback & server, Continuation & context, const JsonNode & node)
{
	std::vector<std::pair<int64_t, CreatureID>> army;
	// Evaluate the whole army table before changing any live stacks.
	for(const auto & slot : node["army"].Vector())
		army.emplace_back(static_cast<int64_t>(expression(server, context, slot["amount"])), CreatureID::decode(slot["entity"].String()));
	const auto & targetHero = hero(context);
	const auto * target = dynamic_cast<const CArmedInstance *>(&subject(context, node));
	if(!target) { logGlobal->error("Native startCombat: subject cannot hold an army"); return; }
	for(int guard = 0; guard < GameConstants::ARMY_SIZE && !target->Slots().empty(); ++guard)
		server.eraseStack(StackLocation(target->id, target->Slots().begin()->first), true);
	for(size_t i = 0; i < army.size() && i < GameConstants::ARMY_SIZE; ++i)
		if(army[i].first > 0 && army[i].second.hasValue())
			server.insertNewStack(StackLocation(target->id, SlotID(i)), army[i].second.toCreature(), army[i].first);
	server.startBattle(&targetHero, target);
}
}
