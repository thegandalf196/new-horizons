/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "OwnedPreparationBootstrap.h"
#include "../Client.h"
#include "../CServerHandler.h"
#include "../GameInstance.h"
#include "../mainmenu/CMainMenu.h"
#include "../../lib/CConfigHandler.h"
#include "../../lib/StartInfo.h"
#include "../../lib/VCMIDirs.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/filesystem/NhArtSha256.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/mapping/CMapInfo.h"
#include <charconv>
#include <cstdlib>
#include <stdexcept>
namespace ownedPreparationIntegration
{
namespace
{
constexpr auto MAP = "Maps/owned-preparation-angel2.vmap";
void require(bool value, const char * why) { if(!value) throw std::runtime_error(why); }
std::string required(const char * name)
{
	const auto * value = std::getenv(name); require(value && *value, "Missing explicit owned-preparation fixture binding"); return value;
}
}
Bootstrap::Bootstrap() : digest(required("NH_OWNED_PREPARATION_MAP_SHA256"))
{
	require(required("NH_OWNED_PREPARATION_BOOTSTRAP") == "owned-angel2", "Unsupported owned bootstrap");
	require(digest.size() == 64 && std::all_of(digest.begin(), digest.end(), [](char c)
		{ return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }), "Invalid fixture SHA256 binding");
	const auto count = required("NH_OWNED_PREPARATION_MAP_BYTES");
	const auto parsed = std::from_chars(count.data(), count.data() + count.size(), bytes);
	require(parsed.ec == std::errc{} && parsed.ptr == count.data() + count.size() && bytes > 0,
		"Invalid fixture byte-count binding");
}
void Bootstrap::update()
{
	if(pending)
	{
		pending = false; // Never rearm after failure/restart.
		const auto & session = settings["session"];
		require(!session["headless"].Bool() && !session["onlyai"].Bool() && !session["spectate"].Bool()
			&& session["testmap"].String().empty() && session["testsave"].String().empty(), "Conflicting owned bootstrap session");
		const ResourcePath resource(MAP, EResType::MAP); auto * loader = CResourceHandler::get();
		require(loader->existsResource(resource), "Missing managed owned-preparation map");
		const auto actual = loader->getResourceName(resource);
		const auto expected = VCMIDirs::get().userDataPath() / "Maps" / "owned-preparation-angel2.vmap";
		require(actual && expected.is_absolute() && expected == boost::filesystem::canonical(expected) && *actual == expected,
			"Owned fixture must resolve to the managed disposable profile without aliases");
		auto stream = loader->load(resource); require(stream->getSize() == bytes, "Owned fixture bytes changed");
		auto [data, count] = stream->readAll();
		require(nhart::hex(nhart::sha256(std::span<const ui8>(data.get(), count))) == digest, "Owned fixture hash changed");
		auto info = std::make_shared<CMapInfo>(); info->mapInit(MAP);
		require(info->mapHeader && !info->isRandomMap && !info->mapHeader->battleOnly, "Expected authored ordinary fixture");
		CMainMenu::openLobby(ESelectionScreen::newGame, true, {}, ELoadMode::NONE, false, false);
		GAME->server().startMapAfterConnection(info); requested = true;
	}
	if(requested && !validated && GAME->server().getState() == EClientState::GAMEPLAY)
	{
		require(GAME->server().client != nullptr, "Missing actual ordinary internal client");
		const auto & game = GAME->server().client->gameState(); const auto * start = game.getInitialStartInfo();
		require(start && start->mode == EStartMode::NEW_GAME && ResourcePath(start->mapname, EResType::MAP) == ResourcePath(MAP, EResType::MAP)
			&& !game.getMap().battleOnly && start->playerInfos.contains(PlayerColor(0))
			&& start->playerInfos.at(PlayerColor(0)).isControlledByHuman(), "Unexpected actual owned fixture world/P0 perspective");
		validated = true;
	}
}
}
