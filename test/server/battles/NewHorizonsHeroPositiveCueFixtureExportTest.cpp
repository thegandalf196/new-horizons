/*
 * NewHorizonsHeroPositiveCueFixtureExportTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/filesystem/NhArtSha256.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGCreature.h"
#include "../../../lib/mapObjects/Quest.h"
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
constexpr auto exportFlag = "NH_EXPORT_HERO_POSITIVE_CUE_ORDINARY_BOOTSTRAP";
constexpr auto saveRootVariable = "NH_EXPORT_HERO_POSITIVE_CUE_SAVE_ROOT";
constexpr auto privateRootVariable = "NH_EXPORT_HERO_POSITIVE_CUE_PRIVATE_ROOT";
constexpr auto sourceRootVariable = "NH_EXPORT_HERO_POSITIVE_CUE_SOURCE_ROOT";
constexpr auto pendingStem = "hero-positive-cue-progression-pending";
constexpr auto finalName = "hero-positive-cue-progression.vsgm1";

bool containsPath(const fs::path & parent, const fs::path & child)
{
	return std::mismatch(parent.begin(), parent.end(), child.begin(), child.end()).first == parent.end();
}

fs::path existingDirectory(const char * variable)
{
	const auto * value = std::getenv(variable);
	if(!value || !*value)
		throw std::runtime_error("Ordinary progression export requires an explicit directory");
	const fs::path path(value);
	if(!path.is_absolute() || !fs::is_directory(path) || path.lexically_normal() != path)
		throw std::runtime_error("Ordinary progression export requires an existing absolute normalized directory");
	for(auto ancestor = path; !ancestor.empty(); ancestor = ancestor.parent_path())
	{
		if(fs::is_symlink(fs::symlink_status(ancestor)))
			throw std::runtime_error("Ordinary progression export rejects symlink ancestors");
		if(ancestor == ancestor.root_path())
			break;
	}
	return fs::canonical(path);
}

void rejectOverlap(const fs::path & privateRoot, const fs::path & other)
{
	const auto canonical = fs::weakly_canonical(other);
	if(containsPath(privateRoot, canonical) || containsPath(canonical, privateRoot))
		throw std::runtime_error("Ordinary progression output overlaps a protected source or resource root");
}

fs::path checkedSaveRoot()
{
	const auto privateRoot = existingDirectory(privateRootVariable);
	const auto saveRoot = existingDirectory(saveRootVariable);
	const auto sourceRoot = existingDirectory(sourceRootVariable);
	if(!fs::is_regular_file(sourceRoot / "AGENTS.md") || !fs::is_regular_file(sourceRoot / "CMakeLists.txt"))
		throw std::runtime_error("Ordinary progression source root lacks the expected repository markers");
	if(saveRoot == privateRoot || !containsPath(privateRoot, saveRoot)
		|| saveRoot != fs::canonical(fs::path(VCMIDirs::get().userSavePath().string())))
		throw std::runtime_error("Ordinary progression save root must be the isolated save mount below its private root");
	rejectOverlap(privateRoot, sourceRoot);
	rejectOverlap(privateRoot, fs::current_path());
	rejectOverlap(privateRoot, fs::path(VCMIDirs::get().binaryPath().string()));
	for(const auto & resourceRoot : VCMIDirs::get().dataPaths())
		rejectOverlap(privateRoot, fs::path(resourceRoot.string()));
	const auto mapCache = fs::path(VCMIDirs::get().userCachePath().string()) / "testMaps";
	if(!containsPath(privateRoot, fs::weakly_canonical(mapCache)))
		throw std::runtime_error("Ordinary progression map cache must remain inside its isolated private root");
	const auto dump = mapCache / "NewHorizonsHeroPositiveCueFixtureExportTest_ExportOrdinaryProgressionAdventureSave.h3m";
	if(fs::symlink_status(dump).type() != fs::file_type::not_found)
		throw std::runtime_error("Ordinary progression export refuses an existing map cache output");
	for(const auto & name : {std::string(pendingStem) + ".vsgm1", std::string(finalName)})
		if(fs::symlink_status(saveRoot / name).type() != fs::file_type::not_found)
			throw std::runtime_error("Ordinary progression export refuses an existing output");
	return saveRoot;
}
}

// Two Red canonical starting heroes earn perks through ordinary XP dialogs.
// This exporter does not visit either quest or choose an offer before saving.
class NewHorizonsHeroPositiveCueFixtureExportTest : public TinyMapGameTest
{
protected:
	RecordingGameServer server;
	std::shared_ptr<CGameHandler> handler;
	JsonNode installedHeroes;
	JsonNode installedPerks;
	Services * gameServices() override { return LIBRARY; }

	void configurePlayer(PlayerSettings & player) const override
	{
		player.bonus = PlayerStartingBonus::GOLD;
		player.connectedPlayerIDs = player.color == PlayerColor(0)
			? std::set<PlayerConnectionID>{PlayerConnectionID::FIRST_HUMAN} : std::set<PlayerConnectionID>{};
		player.compOnly = player.color != PlayerColor(0);
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
		ASSERT_NO_THROW(newHorizonsHeroes::validateWeightedPerkCapture(installedHeroes, installedPerks));
	}

	void createOrdinaryWorld()
	{
		const HeroTypeID daremyth(HeroTypeID::decode("core:daremyth"));
		const HeroTypeID vey(HeroTypeID::decode("core:vey"));
		ASSERT_NE(daremyth, HeroTypeID::NONE);
		ASSERT_NE(vey, HeroTypeID::NONE);
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).name("NHHeroPositiveCueProgression")
			.description("Single human Red owns ordinary Daremyth and Vey; Blue is AI. Each hero-specific Seer Hut grants 10000 XP; nearby single savage Archers offer ordinary encounters. Earn Lucky Day or Heroic Spirit through actual offers; neither is guaranteed. No forced proc or reroll.")
			.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.town({8, 8, 0}, FactionID::TOWER, PlayerColor(0)).townGarrison({})
			.town({30, 30, 0}, FactionID::STRONGHOLD, PlayerColor(1)).townGarrison({})
			.hero({17, 10, 0}, daremyth, PlayerColor(0)).heroExperience(0)
			.hero({21, 10, 0}, vey, PlayerColor(0)).heroExperience(0)
			.hero({28, 28, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1))
			.monster({18, 14, 0}, CreatureID(CreatureID::decode("core:archer")), 1,
				static_cast<int8_t>(CGCreature::Character::SAVAGE))
			.monster({22, 14, 0}, CreatureID(CreatureID::decode("core:archer")), 1,
				static_cast<int8_t>(CGCreature::Character::SAVAGE))
			.seerHut({18, 12, 0}, TinyH3M::TinyH3MBuilder::missionHero(daremyth),
				TinyH3M::TinyH3MBuilder::rewardExperience(10000))
			.seerHut({22, 12, 0}, TinyH3M::TinyH3MBuilder::missionHero(vey),
				TinyH3M::TinyH3MBuilder::rewardExperience(10000));
		const auto mapBytes = builder.build();
		MapServiceTinyH3M map(mapBytes, nullptr);
		const auto header = map.loadMapHeader(ResourcePath("NHHeroPositiveCueProgression"));
		ASSERT_NE(header, nullptr);
		ASSERT_EQ(header->version, EMapFormat::SOD);
		ASSERT_EQ(header->width, 36);
		ASSERT_EQ(header->height, 36);
		for(int color = 0; color < 2; ++color)
			ASSERT_TRUE(header->players.at(color).canHumanPlay);
		startWithMap(std::move(builder), EMapDifficulty::NORMAL);
		server.gameState = gameState();
		handler = std::make_shared<CGameHandler>(server, gameState());
		for(const auto & player : gameState()->players)
			handler->turnOrder->addPlayer(player.first);
		handler->start(false);
		ASSERT_NO_FATAL_FAILURE(checkOrdinaryWorld());
	}

	void checkHero(const CGHeroInstance & hero, int subject)
	{
		const std::string identity = subject == 0 ? "core:daremyth" : "core:vey";
		ASSERT_EQ(hero.getHeroTypeID(), HeroTypeID(HeroTypeID::decode(identity)));
		ASSERT_EQ(hero.getOwner(), PlayerColor(0));
		ASSERT_EQ(hero.level, 1u);
		ASSERT_EQ(hero.exp, 0);
		ASSERT_FALSE(hero.gainsLevel());
		const auto & profile = installedHeroes["startingSkills"]["startingDevelopmentProfiles"][identity];
		ASSERT_EQ(hero.secSkills.size(), profile["skills"].Vector().size());
		for(const auto & skill : profile["skills"].Vector())
			ASSERT_EQ(hero.getPerkSkillRank(skill["skill"].String()), skill["rank"].Integer());
		ASSERT_EQ(profile["startingPerks"].Vector().size(), 1u);
		ASSERT_EQ(hero.getPerkState().selected.size(), 1u);
		const auto & expected = profile["startingPerks"].Vector().front();
		ASSERT_EQ(hero.getPerkState().selected.front(), (newHorizonsHeroes::PerkSelection{
			expected["skill"].String(), expected["perk"].String()}));
		ASSERT_TRUE(hero.hasActivePerk(expected["skill"].String(), expected["perk"].String()));
		ASSERT_FALSE(hero.hasActivePerk("new-horizons:luck", "new-horizons:luck.luckyRecovery"));
		ASSERT_FALSE(hero.hasActivePerk("new-horizons:discipline", "new-horizons:discipline.heroicSpirit"));
		ASSERT_NO_THROW(hero.getPerkState().validate());
		ASSERT_EQ(hero.getPerkState().rules, installedPerks);
		ASSERT_EQ(hero.getPrimaryGrowthRules(), newHorizonsHeroes::resolveHeroRules(installedHeroes, hero.getHeroClassID()));
		ASSERT_FALSE(hero.getMasteryState().pending);
		ASSERT_FALSE(hero.Slots().empty());
		for(const auto & [slot, stack] : hero.Slots())
		{
			const auto capacity = hero.getLeadershipSlotCapacity(stack->getCreatureID());
			ASSERT_TRUE(capacity);
			ASSERT_TRUE(capacity->accepts(stack->getCount()));
		}
		if(subject == 0)
		{
			ASSERT_GT(hero.luckVal(), 0);
		}
		else
		{
			ASSERT_GT(hero.moraleVal(), 0);
		}
	}

	void checkNeutral(const CMap & map, int x)
	{
		const auto found = std::ranges::find_if(map.objects, [&](const auto & object)
		{
			return object && object->ID == Obj::MONSTER && object->pos == int3(x, 14, 0);
		});
		ASSERT_NE(found, map.objects.end());
		const auto * neutral = dynamic_cast<const CGCreature *>(found->get());
		ASSERT_NE(neutral, nullptr);
		ASSERT_EQ(neutral->getOwner(), PlayerColor::UNFLAGGABLE);
		ASSERT_EQ(neutral->initialCharacter, CGCreature::Character::SAVAGE);
		ASSERT_EQ(neutral->agression, 10);
		ASSERT_EQ(neutral->Slots().size(), 1u);
		ASSERT_EQ(neutral->Slots().begin()->second->getCreatureID(), CreatureID(CreatureID::decode("core:archer")));
		ASSERT_EQ(neutral->getStackCount(SlotID(0)), 1);
	}

	void checkQuest(const CMap & map, int color)
	{
		const HeroTypeID identity(HeroTypeID::decode(color == 0 ? "core:daremyth" : "core:vey"));
		const int3 position(color == 0 ? 18 : 22, 12, 0);
		const auto found = std::ranges::find_if(map.objects, [&](const auto & object)
		{
			return object && object->ID == Obj::SEER_HUT && object->pos == position;
		});
		ASSERT_NE(found, map.objects.end());
		const auto * hut = dynamic_cast<const SeerHut *>(found->get());
		ASSERT_NE(hut, nullptr);
		ASSERT_EQ(hut->allQuests().size(), 1u);
		const auto & quest = hut->getQuest();
		ASSERT_EQ(quest.missionKind, EQuestMission::HERO);
		ASSERT_EQ(quest.mission.heroes, std::vector<HeroTypeID>{identity});
		ASSERT_FALSE(quest.isCompleted);
		ASSERT_FALSE(quest.repeatedQuest);
		ASSERT_TRUE(quest.reward);
		ASSERT_EQ(quest.reward->reward.heroExperience, 10000);
	}

	void checkOrdinaryWorld()
	{
		const auto & world = handler->gameState();
		ASSERT_TRUE(world.currentBattles.empty());
		ASSERT_EQ(world.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS), installedHeroes);
		ASSERT_EQ(world.getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS), installedPerks);
		for(int color = 0; color < 2; ++color)
		{
			const PlayerColor player(color);
			ASSERT_EQ(world.getStartInfo()->playerInfos.at(player).isControlledByHuman(), color == 0);
			ASSERT_EQ(world.getStartInfo()->playerInfos.at(player).connectedPlayerIDs,
				(color == 0 ? std::set<PlayerConnectionID>{PlayerConnectionID::FIRST_HUMAN}
					: std::set<PlayerConnectionID>{}));
			ASSERT_EQ(world.getStartInfo()->playerInfos.at(player).compOnly, color != 0);
			ASSERT_EQ(world.players.at(player).human, color == 0);
			const auto heroes = world.getHeroes(player);
			ASSERT_EQ(heroes.size(), color == 0 ? 2u : 1u);
			if(color == 0)
			{
				for(int subject = 0; subject < 2; ++subject)
				{
					const HeroTypeID identity(HeroTypeID::decode(subject == 0 ? "core:daremyth" : "core:vey"));
					const auto found = std::ranges::find_if(heroes, [&](const auto * hero)
						{ return hero->getHeroTypeID() == identity; });
					ASSERT_NE(found, heroes.end());
					ASSERT_NO_FATAL_FAILURE(checkHero(**found, subject));
				}
			}
			else
				ASSERT_EQ(heroes.front()->getHeroTypeID(), HeroTypeID(HeroTypeID::decode("core:orrin")));
			ASSERT_NO_FATAL_FAILURE(checkQuest(world.getMap(), color));
			ASSERT_FALSE(handler->queries->topQuery(player));
		}
		ASSERT_NO_FATAL_FAILURE(checkNeutral(world.getMap(), 18));
		ASSERT_NO_FATAL_FAILURE(checkNeutral(world.getMap(), 22));
		ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(0)));
		ASSERT_FALSE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(1)));
		ASSERT_TRUE(server.attacks.empty());
		ASSERT_TRUE(server.startedActions.empty());
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
		ASSERT_NO_FATAL_FAILURE(checkOrdinaryWorld());
		std::ifstream bytes(pending, std::ios::binary);
		ASSERT_TRUE(bytes.good());
		const auto size = fs::file_size(pending);
		const auto digest = nhart::hex(nhart::sha256(bytes, size));
		bytes.close();
		fs::create_hard_link(pending, final);
		ASSERT_TRUE(fs::remove(pending));
		ASSERT_EQ(fs::hard_link_count(final), 1u);
		ASSERT_EQ(fs::file_size(final), size);
		RecordProperty("saveSha256", digest);
		RecordProperty("saveBytes", std::to_string(size));
		RecordProperty("saveLoadRoundtrip", "verified");
		RecordProperty("fixtureIdentity", "canonical-starts-unvisited-xp-quests");
		RecordProperty("progressionChoicesBeforeExport", 0);
		RecordProperty("combatActionsBeforeExport", 0);
	}
};

TEST_F(NewHorizonsHeroPositiveCueFixtureExportTest, ExportOrdinaryProgressionAdventureSave)
{
	const auto * enabled = std::getenv(exportFlag);
	if(!enabled)
		GTEST_SKIP() << "Requires the exact private ordinary-progression export invocation";
	ASSERT_EQ(std::string_view(enabled), "1");
	const auto root = checkedSaveRoot();
	ASSERT_NO_FATAL_FAILURE(createOrdinaryWorld());
	ASSERT_NO_FATAL_FAILURE(saveAndReload(root));
}
