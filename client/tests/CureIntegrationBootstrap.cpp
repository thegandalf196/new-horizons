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
#include "../../lib/CStack.h"
#include "../../lib/battle/CBattleInfoCallback.h"
#include "../../lib/battle/IBattleState.h"
#include "../../lib/battle/NewHorizonsFrozen.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/StartInfo.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/NhArtSha256.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapInfo.h"

#include <iostream>
#include <cstdlib>

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

CureIntegrationBootstrap::CureIntegrationBootstrap(const char * optIn)
	: state(optIn), fixture(std::make_shared<cureFixture::Context>(
		std::getenv("NH_CURE_FROZEN_FIXTURE"), optIn, std::getenv("NH_CURE_CALLBACK_CASE")))
{
	cureFixture::registerContext(fixture);
}

void CureIntegrationBootstrap::verifyFixtureTarget(const CStack & target, const CBattleInfoCallback & battle) const
{
	if(!fixture->enabled())
		return;
	cureBootstrap::require(fixture->isReceipt(battle.getBattle()->getBattleID().getNum(), target.unitId(),
		battle.battleGetRound()) && target.unitSlot() == SlotID(0) && target.unitSide() == BattleSide::ATTACKER
		&& target.creatureId() == CreatureID(CreatureID::decode("core:crusader"))
		&& target.alive() && target.getCount() == 300 && target.getFirstHPleft() == target.getMaxHealth()
		&& target.getAvailableHealth() == static_cast<int64_t>(300) * target.getMaxHealth()
		&& newHorizonsFrozen::isFrozen(target) && target.frozenLastAppliedRound() == 1
		&& newHorizonsMagic::cureAfflictions(battle.getBattle()->getMagicRules(), &target).empty(),
		"Fixture replica requires exact authoritative receipt/fullHP Frozen-only Crusader300");
}

void CureIntegrationBootstrap::verifyReplicatedFixture(const CBattleInfoCallback & battle)
{
	if(!fixture->enabled() || replicaChecked)
		return;
	const auto * active = battle.battleGetStackByID(battle.getBattle()->getActiveStackID());
	cureBootstrap::require(active && active->unitSlot() == SlotID(1) && active->unitSide() == BattleSide::ATTACKER
		&& active->creatureId() == CreatureID(CreatureID::decode("core:waterElemental"))
		&& active->getCount() == 200 && battle.battleGetRound() == 1, "Fixture replica requires first Water control");
	const CStack * target = nullptr;
	for(const auto * stack : battle.battleGetAllStacks())
		if(stack->unitSide() == BattleSide::ATTACKER && stack->unitSlot() == SlotID(0))
			target = stack;
	cureBootstrap::require(target != nullptr, "Missing fixture replica target");
	verifyFixtureTarget(*target, battle);
	replicaChecked = true;
	std::cout << "Cure deterministic fixture replica READY: battle=" << battle.getBattle()->getBattleID().getNum()
		<< " round=1 target=" << target->unitId() << " active=" << active->unitId()
		<< " Frozen-only Crusader300 fullHP" << std::endl;
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
		fixture->authenticate(cureBootstrap::DIGEST); // fixedMap authenticated actual bytes/header first.
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
