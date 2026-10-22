/*
 * NewHorizonsSkirmisherPreviewFixtureExportTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/entities/artifact/CArtifactInstance.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/filesystem/NhArtSha256.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/processors/TurnOrderProcessor.h"
#include "../../../server/queries/QueriesProcessor.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <string_view>

namespace
{
namespace fs = std::filesystem;
constexpr auto exportFlag = "NH_EXPORT_SKIRMISHER_PREVIEW_ORDINARY_BOOTSTRAP";
constexpr auto saveRootVariable = "NH_EXPORT_SKIRMISHER_PREVIEW_SAVE_ROOT";
constexpr auto privateRootVariable = "NH_EXPORT_SKIRMISHER_PREVIEW_PRIVATE_ROOT";
constexpr auto sourceRootVariable = "NH_EXPORT_SKIRMISHER_PREVIEW_SOURCE_ROOT";
constexpr auto pendingStem = "skirmisher-hover-authored-pending";
constexpr auto finalName = "skirmisher-hover-authored.vsgm1";

bool containsPath(const fs::path & parent, const fs::path & child)
{
	return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end()).first == parent.end();
}

fs::path existingDirectory(const char * variable)
{
	const auto * value = std::getenv(variable);
	if(!value || !*value)
		throw std::runtime_error("Authored preview export requires an explicit directory");
	const fs::path path(value);
	if(!path.is_absolute() || !fs::is_directory(path) || path.lexically_normal() != path)
		throw std::runtime_error("Authored preview export requires an existing absolute normalized directory");
	for(auto ancestor = path; !ancestor.empty(); ancestor = ancestor.parent_path())
	{
		if(fs::is_symlink(fs::symlink_status(ancestor)))
			throw std::runtime_error("Authored preview export rejects symlink ancestors");
		if(ancestor == ancestor.root_path())
			break;
	}
	return fs::canonical(path);
}

void rejectOverlap(const fs::path & privateRoot, const fs::path & other)
{
	const auto canonical = fs::weakly_canonical(other);
	if(containsPath(privateRoot, canonical) || containsPath(canonical, privateRoot))
		throw std::runtime_error("Authored preview output overlaps a protected source or resource root");
}

fs::path checkedSaveRoot()
{
	const auto privateRoot = existingDirectory(privateRootVariable);
	const auto saveRoot = existingDirectory(saveRootVariable);
	const auto sourceRoot = existingDirectory(sourceRootVariable);
	if(!fs::is_regular_file(sourceRoot / "AGENTS.md") || !fs::is_regular_file(sourceRoot / "CMakeLists.txt"))
		throw std::runtime_error("Authored preview source root lacks the expected repository markers");
	if(saveRoot == privateRoot || !containsPath(privateRoot, saveRoot)
		|| saveRoot != fs::canonical(fs::path(VCMIDirs::get().userSavePath().string())))
		throw std::runtime_error("Authored preview save root must be the isolated save mount below its private root");
	rejectOverlap(privateRoot, sourceRoot);
	rejectOverlap(privateRoot, fs::current_path());
	rejectOverlap(privateRoot, fs::path(VCMIDirs::get().binaryPath().string()));
	for(const auto & resourceRoot : VCMIDirs::get().dataPaths())
		rejectOverlap(privateRoot, fs::path(resourceRoot.string()));
	for(const auto & name : {std::string(pendingStem) + ".vsgm1", std::string(finalName)})
		if(fs::symlink_status(saveRoot / name).type() != fs::file_type::not_found)
			throw std::runtime_error("Authored preview export refuses an existing output");
	return saveRoot;
}
}

// Authored starting content only: no post-init perk changes, combat, seed
// selection or altered client content. Both players share one hotseat connection.
class NewHorizonsSkirmisherPreviewFixtureExportTest : public TinyMapGameTest
{
protected:
	RecordingGameServer server;
	std::shared_ptr<CGameHandler> handler;
	JsonNode installedHeroes;
	JsonNode authoredHeroes;
	JsonNode installedPerks;
	Services * gameServices() override { return LIBRARY; }

	void configurePlayer(PlayerSettings & player) const override
	{
		player.bonus = PlayerStartingBonus::GOLD;
		player.connectedPlayerIDs = {PlayerConnectionID::FIRST_HUMAN};
		player.compOnly = false;
	}

	void TearDown() override
	{
		handler.reset();
		server.gameState.reset();
		TinyMapGameTest::TearDown();
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		installedHeroes = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS);
		installedPerks = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
		ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(installedPerks));
		ASSERT_TRUE(newHorizonsHeroes::usesR2PerkOffers(installedPerks));
		authoredHeroes = installedHeroes;
		auto & profiles = authoredHeroes["startingSkills"]["startingDevelopmentProfiles"];
		ASSERT_NO_FATAL_FAILURE(checkOriginalProfile(profiles["core:orrin"], "new-horizons:archery.targetCaller"));
		ASSERT_NO_FATAL_FAILURE(checkOriginalProfile(profiles["core:valeska"], "new-horizons:archery.pointBlankShot"));
		profiles["core:orrin"]["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:archery.skirmisher";
		profiles["core:valeska"]["skills"].Vector()[1]["skill"].String() = "new-horizons:battlecraft";
		profiles["core:valeska"]["startingPerks"].Vector()[0]["skill"].String() = "new-horizons:battlecraft";
		profiles["core:valeska"]["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:battlecraft.overwatch";
		auto restored = authoredHeroes;
		auto & originalProfiles = restored["startingSkills"]["startingDevelopmentProfiles"];
		const auto & defaults = installedHeroes["startingSkills"]["startingDevelopmentProfiles"];
		originalProfiles["core:orrin"]["startingPerks"].Vector()[0]["perk"] = defaults["core:orrin"]["startingPerks"].Vector()[0]["perk"];
		originalProfiles["core:valeska"]["skills"].Vector()[1]["skill"] = defaults["core:valeska"]["skills"].Vector()[1]["skill"];
		originalProfiles["core:valeska"]["startingPerks"].Vector()[0]["skill"] = defaults["core:valeska"]["startingPerks"].Vector()[0]["skill"];
		originalProfiles["core:valeska"]["startingPerks"].Vector()[0]["perk"] = defaults["core:valeska"]["startingPerks"].Vector()[0]["perk"];
		ASSERT_EQ(restored, installedHeroes) << "Only four authored starting-profile leaves may differ";
		ASSERT_NO_THROW(newHorizonsHeroes::validateHeroRules(authoredHeroes, true));
		ASSERT_NO_THROW(newHorizonsHeroes::validatePerkRules(installedPerks));
		ASSERT_NO_THROW(newHorizonsHeroes::validateWeightedPerkCapture(authoredHeroes, installedPerks));
		authoredHeroes.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, authoredHeroes);
		ASSERT_EQ(loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), installedPerks);
	}

	void checkOriginalProfile(const JsonNode & profile, const char * expectedPerk)
	{
		ASSERT_EQ(profile["skills"].Vector().size(), 2u);
		ASSERT_EQ(profile["skills"].Vector()[0]["skill"].String(), "new-horizons:divineMandate");
		ASSERT_EQ(profile["skills"].Vector()[1]["skill"].String(), "new-horizons:archery");
		for(const auto & skill : profile["skills"].Vector())
			ASSERT_EQ(skill["rank"].Integer(), MasteryLevel::BASIC);
		ASSERT_EQ(profile["startingPerks"].Vector().size(), 1u);
		ASSERT_EQ(profile["startingPerks"].Vector()[0]["skill"].String(), "new-horizons:archery");
		ASSERT_EQ(profile["startingPerks"].Vector()[0]["perk"].String(), expectedPerk);
	}

	void createAuthoredWorld()
	{
		const CreatureID archer(CreatureID::decode("core:archer"));
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		ASSERT_TRUE(archer.hasValue() && pikeman.hasValue());
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NHAuthoredSkirmisherHover")
			.description("Authored starting-perk hover diagnostic; no earned-progression or AI-choice claim.")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({8, 8, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
			.town({20, 20, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({});
		for(int color = 0; color < 2; ++color)
			builder.hero({11 + color, 10, 0}, HeroTypeID(color), PlayerColor(color))
				.heroExperience(0).heroGarrison({{archer, 3}, {pikeman, 4}}).heroSpells({});
		startWithMap(std::move(builder), EMapDifficulty::NORMAL);
		server.gameState = gameState();
		handler = std::make_shared<CGameHandler>(server, gameState());
		for(const auto & player : gameState()->players)
			handler->turnOrder->addPlayer(player.first);
		ASSERT_NO_FATAL_FAILURE(checkWorld());
		handler->start(false);
		ASSERT_NO_FATAL_FAILURE(checkAdventureTurn());
	}

	void checkHero(const CGHeroInstance & hero, int color)
	{
		ASSERT_EQ(hero.getHeroTypeID(), HeroTypeID(color));
		ASSERT_EQ(hero.level, 1u);
		ASSERT_EQ(hero.exp, 0);
		ASSERT_FALSE(hero.gainsLevel());
		ASSERT_FALSE(hero.getMasteryState().pending);
		ASSERT_EQ(hero.secSkills.size(), 2u);
		const std::string parent = color == 0 ? "new-horizons:archery" : "new-horizons:battlecraft";
		const std::string perk = color == 0 ? "new-horizons:archery.skirmisher" : "new-horizons:battlecraft.overwatch";
		ASSERT_EQ(hero.getPerkSkillRank("new-horizons:divineMandate"), MasteryLevel::BASIC);
		ASSERT_EQ(hero.getPerkSkillRank(parent), MasteryLevel::BASIC);
		ASSERT_EQ(hero.getPerkState().selected.size(), 1u);
		ASSERT_EQ(hero.getPerkState().selected.front(), (newHorizonsHeroes::PerkSelection{parent, perk}));
		ASSERT_TRUE(hero.hasActivePerk(parent, perk));
		ASSERT_NO_THROW(hero.getPerkState().validate());
		ASSERT_EQ(hero.getPerkState().rules, installedPerks);
		const auto resolved = newHorizonsHeroes::resolveHeroRules(authoredHeroes, hero.getHeroClassID());
		ASSERT_EQ(hero.getPrimaryGrowthRules(), resolved);
		const auto defaults = newHorizonsHeroes::resolveHeroRules(installedHeroes, hero.getHeroClassID());
		ASSERT_EQ(resolved["skillOfferWeights"], defaults["skillOfferWeights"]);
		const auto weights = hero.getPerkOfferWeightContext().referenceWeights;
		ASSERT_EQ(weights.size(), resolved["skillOfferWeights"].Struct().size());
		for(const auto & [skill, weight] : resolved["skillOfferWeights"].Struct())
			ASSERT_EQ(weights.at(skill), weight.Integer());
		const auto primary = newHorizonsHeroes::parsePrimaryProfile(resolved["profile"]).baseAtLevel(1);
		for(int index = 0; index < GameConstants::PRIMARY_SKILLS; ++index)
			ASSERT_EQ(hero.getPrimSkillLevel(PrimarySkill(index)), primary[index]);
		ASSERT_TRUE(hero.getSpellsInSpellbook().empty());
		ASSERT_EQ(hero.getArt(ArtifactPosition::SPELLBOOK), nullptr);
		ASSERT_EQ(hero.stacksCount(), 2u);
		for(int slot = 0; slot < 2; ++slot)
		{
			const CreatureID creature(CreatureID::decode(slot == 0 ? "core:archer" : "core:pikeman"));
			ASSERT_EQ(hero.getCreature(SlotID(slot))->getId(), creature);
			ASSERT_EQ(hero.getStackCount(SlotID(slot)), slot == 0 ? 3 : 4);
			const auto capacity = hero.getLeadershipSlotCapacity(creature);
			ASSERT_TRUE(capacity);
			ASSERT_TRUE(capacity->accepts(hero.getStackCount(SlotID(slot))));
		}
	}

	void checkWorld()
	{
		const auto & world = handler->gameState();
		ASSERT_TRUE(world.currentBattles.empty());
		ASSERT_EQ(world.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS), authoredHeroes);
		ASSERT_EQ(world.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), installedPerks);
		for(int color = 0; color < 2; ++color)
		{
			const PlayerColor player(color);
			ASSERT_TRUE(world.getStartInfo()->playerInfos.at(player).isControlledByHuman());
			ASSERT_EQ(world.getStartInfo()->playerInfos.at(player).connectedPlayerIDs, std::set<PlayerConnectionID>{PlayerConnectionID::FIRST_HUMAN});
			ASSERT_TRUE(world.players.at(player).human);
			const auto heroes = world.getHeroes(player);
			ASSERT_EQ(heroes.size(), 1u);
			ASSERT_NO_FATAL_FAILURE(checkHero(*heroes.front(), color));
			ASSERT_EQ(world.players.at(player).getTowns().size(), 1u);
			ASSERT_FALSE(handler->queries->topQuery(player));
		}
		const auto left = world.getHeroes(PlayerColor(0)).front()->visitablePos();
		const auto right = world.getHeroes(PlayerColor(1)).front()->visitablePos();
		ASSERT_EQ(right.x, left.x + 1);
		ASSERT_EQ(right.y, left.y);
		ASSERT_EQ(right.z, left.z);
	}

	void checkAdventureTurn()
	{
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(0)));
		ASSERT_FALSE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(1)));
		ASSERT_TRUE(server.attacks.empty());
		ASSERT_TRUE(server.startedActions.empty());
		ASSERT_NO_FATAL_FAILURE(checkWorld());
	}

	void saveAndReload(const fs::path & root)
	{
		const auto pending = root / (std::string(pendingStem) + ".vsgm1");
		const auto final = root / finalName;
		const std::string filename = std::string("Saves/") + pendingStem;
		const auto day = handler->gameState().day;
		handler->save(filename, PlayerColor::NEUTRAL);
		ASSERT_TRUE(fs::is_regular_file(pending));
		ASSERT_GT(fs::file_size(pending), 0u);
		ASSERT_LT(fs::file_size(pending), 64u * 1024u * 1024u);
		StartInfo loadInfo = *handler->gameState().getStartInfo();
		loadInfo.mode = EStartMode::LOAD_GAME;
		loadInfo.mapname = filename;
		handler->load(loadInfo);
		server.gameState = handler->gs;
		ASSERT_NE(handler->gs, gameState());
		ASSERT_EQ(handler->gameState().day, day);
		ASSERT_NO_FATAL_FAILURE(checkAdventureTurn());
		std::ifstream bytes(pending, std::ios::binary);
		ASSERT_TRUE(bytes.good());
		const auto size = fs::file_size(pending);
		const auto digest = nhart::hex(nhart::sha256(bytes, size));
		bytes.close();
		// Exclusive publication avoids rename's platform-specific replacement behavior.
		// A failed round-trip retains only the pending diagnostic, never this final name.
		fs::create_hard_link(pending, final);
		ASSERT_TRUE(fs::remove(pending));
		ASSERT_EQ(fs::hard_link_count(final), 1u);
		ASSERT_EQ(fs::file_size(final), size);
		RecordProperty("saveSha256", digest);
		RecordProperty("saveBytes", std::to_string(size));
		RecordProperty("saveLoadRoundtrip", "verified");
		RecordProperty("fixtureIdentity", "authored-preinit-skirmisher-overwatch");
		RecordProperty("combatActionsBeforeExport", 0);
	}
};

TEST_F(NewHorizonsSkirmisherPreviewFixtureExportTest, ExportAuthoredPreinitAdventureSave)
{
	const auto * enabled = std::getenv(exportFlag);
	if(!enabled)
		GTEST_SKIP() << "Requires the exact private authored-preview export invocation";
	ASSERT_EQ(std::string_view(enabled), "1");
	const auto root = checkedSaveRoot();
	ASSERT_NO_FATAL_FAILURE(createAuthoredWorld());
	ASSERT_NO_FATAL_FAILURE(saveAndReload(root));
}
