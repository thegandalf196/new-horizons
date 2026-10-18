/*
 * CureIntegrationBootstrap.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "CureIntegrationBootstrap.h"
#include "../Client.h"
#include "../CServerHandler.h"
#include "../GameEngine.h"
#include "../GameInstance.h"
#include "../mainmenu/CMainMenu.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/StartInfo.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/NhArtSha256.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapInfo.h"

#include <iostream>

namespace
{
void validateHeader(const CMapHeader & map)
{
	cureBootstrap::Header header;
	cureBootstrap::require(map.players.size() == header.human.size(), "Unexpected player slot count");
	for(std::size_t index = 0; index < header.human.size(); ++index)
	{
		header.human[index] = map.players[index].canHumanPlay;
		header.computer[index] = map.players[index].canComputerPlay;
	}
	header.width = map.width;
	header.height = map.height;
	header.normal = map.difficulty == EMapDifficulty::NORMAL;
	header.ordinary = !map.battleOnly && map.levels() == 1;
	cureBootstrap::validateHeader(header);
}

std::shared_ptr<CMapInfo> fixedMap()
{
	const ResourcePath resource(std::string(cureBootstrap::MAP), EResType::MAP);
	auto * loader = CResourceHandler::get();
	cureBootstrap::require(loader->existsResource(resource), "Missing fixed Cure bootstrap map");
	const auto physical = loader->getResourceName(resource);
	const auto expected = VCMIDirs::get().userDataPath() / "Maps"
		/ "ordinary48-normal-garrison-ice200-three-human-neutral-owner.vmap";
	cureBootstrap::require(physical && expected.is_absolute() && expected == boost::filesystem::canonical(expected)
		&& *physical == expected, "Cure bootstrap map must resolve to managed profile, without aliases");
	auto stream = loader->load(resource);
	cureBootstrap::require(stream->getSize() == 1581, "Unexpected Cure bootstrap map size");
	auto [bytes, size] = stream->readAll();
	const auto digest = nhart::hex(nhart::sha256(std::span<const ui8>(bytes.get(), size)));
	cureBootstrap::validateResource(true, size, digest);
	auto info = std::make_shared<CMapInfo>();
	info->mapInit(std::string(cureBootstrap::MAP));
	cureBootstrap::require(info->mapHeader && !info->isRandomMap, "Missing authored map header");
	validateHeader(*info->mapHeader);
	return info;
}

bool threeRedStacks(const CGHeroInstance & hero)
{
	if(hero.Slots().size() != 3)
		return false;
	const std::array<std::string, 3> creatures{"core:crusader", "core:waterElemental", "core:waterElemental"};
	const std::array<int, 3> counts{300, 200, 200};
	for(int index = 0; index < 3; ++index)
	{
		const auto slot = hero.Slots().find(SlotID(index));
		if(slot == hero.Slots().end() || slot->second->getCreatureID() != CreatureID(CreatureID::decode(creatures[index]))
			|| slot->second->getCount() != counts[index])
			return false;
	}
	return true;
}

void validateLoaded()
{
	const auto & game = GAME->server().client->gameState();
	const auto * start = game.getInitialStartInfo();
	cureBootstrap::require(start, "Missing loaded initial StartInfo");
	cureBootstrap::Loaded loaded;
	loaded.exactMap = ResourcePath(start->mapname, EResType::MAP)
		== ResourcePath(std::string(cureBootstrap::MAP), EResType::MAP);
	loaded.newGame = start->mode == EStartMode::NEW_GAME;
	loaded.normal = start->difficulty == 1;
	for(const auto & [color, player] : start->playerInfos)
	{
		cureBootstrap::require(color.isValidPlayer(), "Invalid loaded player color");
		loaded.human[color.getNum()] = player.isControlledByHuman();
		loaded.computer[color.getNum()] = player.isControlledByAI();
	}
	const auto & map = game.getMap();
	validateHeader(map);
	for(const auto * hero : map.getObjects<CGHeroInstance>())
	{
		if(hero->instanceName == "hero_0")
		{
			loaded.redTyris = hero->getOwner() == PlayerColor(0)
				&& hero->getHeroTypeID() == HeroTypeID(HeroTypeID::decode("core:tyris"));
			loaded.threeRedStacks = threeRedStacks(*hero);
		}
	}
	for(const auto * army : map.getObjects<CArmedInstance>())
	{
		if(army->instanceName == "ordinary48_normal_garrison_ice200" && army->Slots().size() == 1)
		{
			const auto & stack = *army->Slots().begin()->second;
			loaded.neutralIce200 = army->getOwner() == PlayerColor::NEUTRAL
				&& stack.getCreatureID() == CreatureID(CreatureID::decode("core:iceElemental"))
				&& stack.getCount() == 200;
		}
	}
	cureBootstrap::validateLoaded(loaded);
}
}

void CureIntegrationBootstrap::update()
{
	cureBootstrap::require(ENGINE->amIGuiThread(), "Cure bootstrap must run on GUI thread");
	if(state.consume())
	{
		const auto & session = settings["session"];
		cureBootstrap::require(!session["headless"].Bool() && !session["onlyai"].Bool()
			&& !session["spectate"].Bool() && session["testmap"].String().empty()
			&& session["testsave"].String().empty(), "Conflicting Cure bootstrap session");
		auto info = fixedMap();
		CMainMenu::openLobby(ESelectionScreen::newGame, true, {}, ELoadMode::NONE, false, false);
		GAME->server().startMapAfterConnection(info);
		std::cout << "Cure bootstrap ordinary44b24 requested once; initial menus omitted\n";
	}
	if(state.awaitingLoaded() && GAME->server().getState() == EClientState::GAMEPLAY)
	{
		cureBootstrap::require(GAME->server().client != nullptr, "Missing actual loaded client");
		validateLoaded();
		state.acceptLoaded();
		std::cout << "Cure bootstrap actual setup PASS: Normal red-human blue-AI three-red-stacks neutral-Ice200\n";
	}
}
