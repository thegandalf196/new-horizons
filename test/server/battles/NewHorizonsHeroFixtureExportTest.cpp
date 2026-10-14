/*
 * NewHorizonsHeroFixtureExportTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/VCMIDirs.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <cstdlib>
#include <zlib.h>
#include <filesystem>
#include "../../../lib/filesystem/Filesystem.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/processors/TurnOrderProcessor.h"

// Separate from BattleTestFixture::startGame: retain ordinary hero/army
// initialization and never pin/search a combat RNG stream or strip bonuses.
class NewHorizonsHeroProcFixtureExportTest : public TinyMapGameTest
{
protected:
	RecordingGameServer server;
	std::shared_ptr<CGameHandler> handler;
	Services * gameServices() override { return LIBRARY; }
	void configurePlayer(PlayerSettings & player) const override
	{
		player.bonus = PlayerStartingBonus::GOLD;
		if(player.color == PlayerColor(1))
			player.connectedPlayerIDs.clear();
	}
	void TearDown() override
	{
		handler.reset();
		server.gameState.reset();
		TinyMapGameTest::TearDown();
	}
	void chooseOffer(CGHeroInstance * hero, const char * skill, const char * perk, bool prerequisite)
	{
		const auto wanted = newHorizonsHeroes::perkDefinition(hero->getPerkState().rules, skill, perk);
		ASSERT_TRUE(wanted);
		const int tier = newHorizonsHeroes::perkRequiredRank(wanted->requiredRank);
		for(const auto & selected : hero->getPerkState().selected)
		{
			if(selected.skillId != skill)
				continue;
			const auto definition = newHorizonsHeroes::perkDefinition(hero->getPerkState().rules, skill, selected.perkId);
			if(!definition || newHorizonsHeroes::perkRequiredRank(definition->requiredRank) != tier)
				continue;
			ASSERT_TRUE(prerequisite || selected.perkId == perk) << "An occupied tier cannot be cleared for this fixture";
			return;
		}
		const auto ranks = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
		// This selects a legal offer, not a combat seed. GameRandomizer is
		// never queried/reset and no action/proc is performed before saving.
		for(uint64_t offerSeed = 0; offerSeed < 4096; ++offerSeed)
		{
			const auto offer = hero->getPerkState().prepareOffer(ranks, offerSeed, hero->getPerkOfferWeightContext());
			for(size_t choice = 0; choice < offer.size(); ++choice)
				if(offer[choice].selection.skillId == skill && offer[choice].selection.perkId == perk)
				{
					handler->levelUpHero(hero, offer, choice, offerSeed, false);
					ASSERT_TRUE(hero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No legal offer for " << perk;
	}
};

TEST_F(NewHorizonsHeroProcFixtureExportTest, ExportLegalOfferOrderProcAdventureSave)
{
	const char * familyValue = std::getenv("NH_EXPORT_HERO_PROC_FAMILY");
	const char * rootValue = std::getenv("NH_EXPORT_HERO_PROC_SAVE_ROOT");
	if(!familyValue || !rootValue)
		GTEST_SKIP() << "Explicit private family and save root are required for export";
	const std::string family(familyValue);
	ASSERT_TRUE(family == "attacker" || family == "defender");
	const std::filesystem::path root(rootValue);
	ASSERT_TRUE(root.is_absolute() && std::filesystem::is_directory(root));
	for(auto ancestor = root; !ancestor.empty(); ancestor = ancestor.parent_path())
	{
		ASSERT_FALSE(std::filesystem::is_symlink(std::filesystem::symlink_status(ancestor)));
		if(ancestor == ancestor.root_path()) break;
	}
	// Use the maintained writer in its isolated user-save mount only. The
	// explicit configured path cannot redirect output to a package/original.
	ASSERT_EQ(std::filesystem::canonical(root), std::filesystem::canonical(
		std::filesystem::path(VCMIDirs::get().userSavePath().string())));
	const auto output = root / ("hero-proc-" + family + ".vsgm1");
	ASSERT_EQ(std::filesystem::symlink_status(output).type(), std::filesystem::file_type::not_found);
	const auto luck = SecondarySkill(SecondarySkill::decode("new-horizons:luck"));
	const auto discipline = SecondarySkill(SecondarySkill::decode("new-horizons:discipline"));
	const auto divineMandate = SecondarySkill(SecondarySkill::decode("new-horizons:divineMandate"));
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID companion(CreatureID::decode("core:imp"));
	const ArtifactID pendant(ArtifactID::decode("core:pendantOfCourage"));
	ASSERT_NE(pikeman, CreatureID::NONE);
	ASSERT_NE(companion, CreatureID::NONE);
	ASSERT_NE(pendant, ArtifactID::NONE);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NHHeroProcPresentation")
		.description("Private ordinary pre-battle Hero Luck/Morale presentation fixture. No forced proc or battle-only mode.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 8, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({20, 20, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({});
	for(int color = 0; color < 2; ++color)
		builder.hero({11 + color, 10, 0}, HeroTypeID(color), PlayerColor(color))
			// Ordinary Might creation replaces the second authored skill with
			// its faction skill. Reserve that slot without sacrificing Discipline.
			.heroExperience(0).heroSecondarySkills({{SecondarySkill::LUCK, 3},
				{SecondarySkill::ARCHERY, 1}, {SecondarySkill::LEADERSHIP, 3}})
			.heroGarrison({{pikeman, 200}, {companion, 20}})
			.heroEquipped({{ArtifactPosition::NECK, pendant}}).heroSpells({});
	startWithMap(builder, EMapDifficulty::NORMAL);
	server.gameState = gameState();
	handler = std::make_shared<CGameHandler>(server, gameState());
	// The existing-state constructor does not perform CGameHandler::init's
	// ordinary turn-order registration. Register the actual initialized players.
	for(const auto & player : gameState()->players)
		handler->turnOrder->addPlayer(player.first);
	for(int color = 0; color < 2; ++color)
	{
		auto * hero = findHeroByOwner(PlayerColor(color));
		ASSERT_NE(hero, nullptr);
		// SOD stores legacy uint8 skill IDs. Ordinary creation migrates these
		// through canonical startingSkills; never patch ranks after loading.
		ASSERT_EQ(hero->getSecSkillLevel(luck), 3);
		ASSERT_EQ(hero->getSecSkillLevel(discipline), 3);
		ASSERT_EQ(hero->getSecSkillLevel(divineMandate), 1);
		ASSERT_EQ(hero->getPerkSkillRank("new-horizons:luck"), 3);
		ASSERT_EQ(hero->getPerkSkillRank("new-horizons:discipline"), 3);
		ASSERT_EQ(hero->getStackCount(SlotID(0)), 200);
		ASSERT_EQ(hero->getStackCount(SlotID(1)), 20);
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:luck", "new-horizons:luck.secondChance", true));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:luck", "new-horizons:luck.luckyRecovery", false));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:luck", "new-horizons:luck.twistOfFate", false));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:discipline", "new-horizons:discipline.steadfast", true));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:discipline", "new-horizons:discipline.holdFast", true));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:discipline", "new-horizons:discipline.heroicSpirit", false));
		ASSERT_GT(hero->luckVal(), 0);
		ASSERT_GT(hero->moraleVal(), 0);
	}
	handler->start(false);
	if(family == "defender")
	{
		ASSERT_TRUE(handler->turnOrder->onPlayerEndsTurn(PlayerColor(0)));
	}
	ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(family == "attacker" ? PlayerColor(0) : PlayerColor(1)));
	ASSERT_TRUE(gameState()->currentBattles.empty());
	const std::array<std::vector<newHorizonsHeroes::PerkSelection>, 2> selectedBeforeSave{
		findHeroByOwner(PlayerColor(0))->getPerkState().selected,
		findHeroByOwner(PlayerColor(1))->getPerkState().selected};
	const auto filename = "Saves/hero-proc-" + family;
	handler->save(filename, PlayerColor::NEUTRAL);
	ASSERT_TRUE(std::filesystem::is_regular_file(output));
	ASSERT_GT(std::filesystem::file_size(output), 0u);
	// Read the complete world and server state through the production loader.
	// Its own game state replaces gs; rebind the recording server before any
	// subsequent authoritative operation, and inspect only the loaded objects.
	StartInfo loadInfo = *handler->gameState().getStartInfo();
	loadInfo.mode = EStartMode::LOAD_GAME;
	loadInfo.mapname = filename;
	handler->load(loadInfo);
	server.gameState = handler->gs;
	ASSERT_NE(handler->gs, gameState());
	ASSERT_TRUE(handler->gameState().currentBattles.empty());
	ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(
		family == "attacker" ? PlayerColor(0) : PlayerColor(1)));
	ASSERT_FALSE(handler->turnOrder->isPlayerMakingTurn(
		family == "attacker" ? PlayerColor(1) : PlayerColor(0)));
	EXPECT_TRUE(handler->gameState().getStartInfo()->playerInfos.at(PlayerColor(0)).isControlledByHuman());
	EXPECT_FALSE(handler->gameState().getStartInfo()->playerInfos.at(PlayerColor(1)).isControlledByHuman());
	for(int color = 0; color < 2; ++color)
	{
		const auto heroes = handler->gameState().getHeroes(PlayerColor(color));
		ASSERT_EQ(heroes.size(), 1u);
		const auto * hero = heroes.front();
		ASSERT_NE(hero, nullptr);
		EXPECT_EQ(hero->getPerkState().selected, selectedBeforeSave[color]);
		EXPECT_EQ(hero->getSecSkillLevel(luck), 3);
		EXPECT_EQ(hero->getSecSkillLevel(discipline), 3);
		EXPECT_EQ(hero->getSecSkillLevel(divineMandate), 1);
		EXPECT_EQ(hero->getPerkSkillRank("new-horizons:luck"), 3);
		EXPECT_EQ(hero->getPerkSkillRank("new-horizons:discipline"), 3);
		EXPECT_TRUE(hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.luckyRecovery"));
		EXPECT_TRUE(hero->hasActivePerk("new-horizons:luck", "new-horizons:luck.twistOfFate"));
		EXPECT_TRUE(hero->hasActivePerk("new-horizons:discipline", "new-horizons:discipline.heroicSpirit"));
		EXPECT_GT(hero->luckVal(), 0);
		EXPECT_GT(hero->moraleVal(), 0);
		EXPECT_EQ(hero->stacksCount(), 2u);
		EXPECT_EQ(hero->getStackCount(SlotID(0)), 200);
		EXPECT_EQ(hero->getStackCount(SlotID(1)), 20);
	}
	if(HasFailure())
		return;
	RecordProperty("saveLoadRoundtrip", "verified");
	RecordProperty("family", family);
	RecordProperty("savePath", output.string());
	RecordProperty("mapIdentity", "NHHeroProcPresentation");
	RecordProperty("combatActionsBeforeExport", 0);
}

// Separate from the presentation exporter above: no exported presentation
// hero, army, spell restriction or selected perk is changed by this case.
TEST_F(NewHorizonsHeroProcFixtureExportTest, PermanentHeroProcStateSurvivesWorldSaveReloadAndValidatedEndTurn)
{
	const char * rootValue = std::getenv("NH_EXPORT_HERO_PROC_SAVE_ROOT");
	if(!rootValue)
		GTEST_SKIP() << "Explicit isolated save root is required for world save test";
	const std::filesystem::path root(rootValue);
	ASSERT_TRUE(root.is_absolute() && std::filesystem::is_directory(root));
	for(auto ancestor = root; !ancestor.empty(); ancestor = ancestor.parent_path())
	{
		ASSERT_FALSE(std::filesystem::is_symlink(std::filesystem::symlink_status(ancestor)));
		if(ancestor == ancestor.root_path()) break;
	}
	ASSERT_EQ(std::filesystem::canonical(root), std::filesystem::canonical(
		std::filesystem::path(VCMIDirs::get().userSavePath().string())));
	const auto output = root / "hero-proc-permanent-continuation.vsgm1";
	ASSERT_EQ(std::filesystem::symlink_status(output).type(), std::filesystem::file_type::not_found);
	const auto luck = SecondarySkill(SecondarySkill::decode("new-horizons:luck"));
	const auto discipline = SecondarySkill(SecondarySkill::decode("new-horizons:discipline"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name("NHHeroProcPermanentContinuation")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 8, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({20, 20, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({});
	for(int color = 0; color < 2; ++color)
		builder.hero({11 + color, 10, 0}, HeroTypeID(color), PlayerColor(color))
			.heroExperience(0).heroSecondarySkills({{SecondarySkill::LUCK, 3},
				{SecondarySkill::ARCHERY, 1}, {SecondarySkill::LEADERSHIP, 3}})
			.heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 20}}).heroSpells({});
	startWithMap(builder, EMapDifficulty::NORMAL);
	server.gameState = gameState();
	handler = std::make_shared<CGameHandler>(server, gameState());
	for(const auto & player : gameState()->players)
		handler->turnOrder->addPlayer(player.first);
	const SpellID learned(SpellID::MAGIC_ARROW);
	ASSERT_NE(learned.toSpell(), nullptr);
	struct SavedHero
	{
		ObjectInstanceID id;
		newHorizonsHeroes::PerkState perks;
		int32_t normal;
		int32_t buffer;
		std::set<SpellID> spellbook;
		int32_t luck;
		int32_t morale;
	};
	std::vector<SavedHero> heroesBefore;
	for(int color = 0; color < 2; ++color)
	{
		auto * hero = findHeroByOwner(PlayerColor(color));
		ASSERT_NE(hero, nullptr);
		ASSERT_EQ(hero->getSecSkillLevel(luck), 3);
		ASSERT_EQ(hero->getSecSkillLevel(discipline), 3);
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:luck", "new-horizons:luck.secondChance", true));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:luck", color == 0
			? "new-horizons:luck.luckyRecovery" : "new-horizons:luck.gambler", false));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:luck", "new-horizons:luck.twistOfFate", false));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:discipline", "new-horizons:discipline.steadfast", true));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:discipline", "new-horizons:discipline.holdFast", true));
		ASSERT_NO_FATAL_FAILURE(chooseOffer(hero, "new-horizons:discipline", "new-horizons:discipline.heroicSpirit", false));
		// Legitimate test setup through public permanent hero-state APIs, not
		// battle temporary buffers, proc forcing, raw fields or a paid-cast claim.
		hero->addSpellToSpellbook(learned);
		ASSERT_TRUE(hero->grantBufferSpellPoints(17));
		ASSERT_TRUE(hero->areSpellPointsInitialized());
		ASSERT_GT(hero->getNormalSpellPoints(), 0);
		ASSERT_GT(hero->getBufferSpellPoints(), 0);
		ASSERT_TRUE(hero->spellbookContainsSpell(learned));
	}
	handler->start(false);
	ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(0)));
	ASSERT_TRUE(handler->gameState().currentBattles.empty());
	for(int color = 0; color < 2; ++color)
	{
		const auto * hero = findHeroByOwner(PlayerColor(color));
		ASSERT_NE(hero, nullptr);
		ASSERT_GT(hero->getNormalSpellPoints(), 0);
		ASSERT_GT(hero->getBufferSpellPoints(), 0);
		ASSERT_TRUE(hero->spellbookContainsSpell(learned));
		heroesBefore.push_back({hero->id, hero->getPerkState(), hero->getNormalSpellPoints(),
			hero->getBufferSpellPoints(), hero->getSpellsInSpellbook(), hero->luckVal(), hero->moraleVal()});
	}
	const auto rulesBefore = handler->gameState().getMagicRules();
	ASSERT_TRUE(rulesBefore["heroLuck"].isStruct());
	ASSERT_TRUE(rulesBefore["heroMorale"].isStruct());
	const auto randomizerBytes = [](GameRandomizer & randomizer)
	{
		CMemorySerializer wire;
		wire.oser & randomizer;
		return wire.extractBuffer();
	};
	// Public service draws populate both heroes' dedicated streams for this
	// deterministic native save test. These are not paid action/proc presentation
	// evidence; no seed, attribute or chance is overridden to obtain an outcome.
	for(const auto & saved : heroesBefore)
	{
		ASSERT_GT(saved.luck, 0);
		ASSERT_GT(saved.morale, 0);
		ASSERT_TRUE(handler->randomizer->rollHeroLuck(saved.id, saved.luck));
		ASSERT_TRUE(handler->randomizer->rollHeroMorale(saved.id, saved.morale));
	}
	const auto beforeSaveRandomizer = randomizerBytes(*handler->randomizer);
	CMemorySerializer expectedWire(beforeSaveRandomizer);
	GameRandomizer expected(handler->gameState());
	expectedWire.iser & expected;
	ASSERT_EQ(randomizerBytes(expected), beforeSaveRandomizer);
	const auto filename = "Saves/hero-proc-permanent-continuation";
	handler->save(filename, PlayerColor::NEUTRAL);
	ASSERT_TRUE(std::filesystem::is_regular_file(output));
	ASSERT_GT(std::filesystem::file_size(output), 0u);
	StartInfo loadInfo = *handler->gameState().getStartInfo();
	loadInfo.mode = EStartMode::LOAD_GAME;
	loadInfo.mapname = filename;
	handler->load(loadInfo);
	server.gameState = handler->gs;
	ASSERT_NE(handler->gs, gameState());
	ASSERT_TRUE(handler->gameState().currentBattles.empty());
	EXPECT_EQ(handler->gameState().getMagicRules(), rulesBefore);
	ASSERT_EQ(randomizerBytes(*handler->randomizer), beforeSaveRandomizer);
	const auto expectPermanent = [&]()
	{
		for(const auto & saved : heroesBefore)
		{
			const auto * hero = handler->gameState().getHero(saved.id);
			ASSERT_NE(hero, nullptr);
			EXPECT_EQ(hero->getPerkState().rules, saved.perks.rules);
			EXPECT_EQ(hero->getPerkState().selected, saved.perks.selected);
			EXPECT_EQ(hero->getPerkSkillRank("new-horizons:luck"), 3);
			EXPECT_EQ(hero->getPerkSkillRank("new-horizons:discipline"), 3);
			EXPECT_TRUE(hero->areSpellPointsInitialized());
			EXPECT_EQ(hero->getNormalSpellPoints(), saved.normal);
			EXPECT_EQ(hero->getBufferSpellPoints(), saved.buffer);
			EXPECT_EQ(hero->getSpellsInSpellbook(), saved.spellbook);
			EXPECT_EQ(hero->luckVal(), saved.luck);
			EXPECT_EQ(hero->moraleVal(), saved.morale);
		}
	};
	ASSERT_NO_FATAL_FAILURE(expectPermanent());
	for(const auto & saved : heroesBefore)
	{
		const auto * restoredHero = handler->gameState().getHero(saved.id);
		ASSERT_NE(restoredHero, nullptr);
		const auto expectedLuck = expected.rollHeroLuck(saved.id, saved.luck);
		const auto actualLuck = handler->randomizer->rollHeroLuck(saved.id, restoredHero->luckVal());
		ASSERT_TRUE(expectedLuck);
		ASSERT_TRUE(actualLuck);
		EXPECT_EQ(actualLuck, expectedLuck);
		const auto expectedMorale = expected.rollHeroMorale(saved.id, saved.morale);
		const auto actualMorale = handler->randomizer->rollHeroMorale(saved.id, restoredHero->moraleVal());
		ASSERT_TRUE(expectedMorale);
		ASSERT_TRUE(actualMorale);
		EXPECT_EQ(actualMorale, expectedMorale);
	}
	EXPECT_EQ(handler->randomizer->getDefault().nextInt(), expected.getDefault().nextInt());
	EXPECT_EQ(randomizerBytes(*handler->randomizer), randomizerBytes(expected));
	ASSERT_TRUE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(0)));
	ASSERT_FALSE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(1)));
	// The ordinary request validates connection/player, active status and
	// pending queries before delegating to the authoritative turn processor.
	// Player0→Player1 does not advance the day or regenerate either mana pool.
	EndTurn endTurn;
	endTurn.player = PlayerColor(0);
	handler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, endTurn);
	EXPECT_FALSE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(0)));
	EXPECT_TRUE(handler->turnOrder->isPlayerMakingTurn(PlayerColor(1)));
	EXPECT_TRUE(handler->gameState().currentBattles.empty());
	EXPECT_EQ(handler->gameState().getMagicRules(), rulesBefore);
	ASSERT_NO_FATAL_FAILURE(expectPermanent());
	if(HasFailure())
		return;
	RecordProperty("worldSaveReloadPermanentHeroPolicy", "verified");
	RecordProperty("worldSaveReloadDedicatedHeroRngContinuation", "verified");
	RecordProperty("validatedPostLoadEndTurn", "verified");
}

class NewHorizonsHeroFixtureExportTest : public BattleTestFixture
{
protected:
	void configurePlayer(PlayerSettings & settings) const override
	{
		BattleTestFixture::configurePlayer(settings);
		if(settings.color == PlayerColor(1))
			settings.connectedPlayerIDs.clear();
	}
};

TEST_F(NewHorizonsHeroFixtureExportTest, ExportOrdinaryOvercapacityArmyAndTrainedBallista)
{
	const auto * enabled = std::getenv("NH_EXPORT_CAPABILITY_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_CAPABILITY_FIXTURES=1";
	ASSERT_TRUE(newHorizonsHeroes::usesRules(LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES)))
		<< "Explicit export requires actual private capability activation, never a fixture rule override";
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCapabilities"));
	const std::string name = "NHCapabilitiesArmySiege";
	const auto pikeman = creatureByName("core:pikeman");
	const auto archer = creatureByName("core:archer");
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Capability diagnostic, not a balanced scenario. Red human, Blue computer, Gold bonuses. "
			"Orrin has1000 Pikemen, Basic Pathfinding, Expert Artillery, no Leadership, and an equipped Ballista. "
			"Initial capacity1000/750; movement LIMIT factor75%, not a current-point refund. Save first. "
			"Use the empty Red town at8,10 to transfer500 Pikemen into its garrison and back: all troops remain, "
			"capacity changes but remaining movement is not awarded. End turn to refresh movement normally. "
			"Twelve hostile Archers at15,10 offer a weak battle: pursue if they flee. Defend with Pikemen "
			"to allow manual Ballista shots. Expert Artillery gives base damage range x4, not final damage x4. "
			"Save/restart and inspect actual army, capacity, skills and equipment. Original growth maps remain separate.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({11, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0)
		.heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}, {SecondarySkill::ARTILLERY, 3}})
		.heroGarrison({{pikeman, 1000}}).heroSpells({})
		.heroEquipped({{ArtifactPosition::MACH1, ArtifactID::BALLISTA}})
		.monster({15, 10, 0}, archer, 12, 4);
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	ASSERT_EQ(header->width, 36);
	ASSERT_EQ(header->height, 36);
	// No mapLoaded override, authored primary override or saved-rule injection.
	startWithMap(builder);
	ASSERT_EQ(gameState()->getHeroCapabilityRules(), canonical);
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(hero->getPrimaryGrowthView());
	EXPECT_EQ(hero->getPrimaryGrowthView()->base, (std::array<int, 4>{30, 45, 10, 15}));
	ASSERT_TRUE(hero->getLeadershipCapacity());
	EXPECT_EQ(hero->getLeadershipCapacity()->capacity, 750);
	EXPECT_EQ(hero->getLeadershipCapacity()->used, 1000);
	EXPECT_EQ(hero->getLeadershipCapacity()->movementPercent, 75);
	EXPECT_TRUE(hero->getLeadershipCapacity()->overCapacity());
	EXPECT_EQ(hero->getLowestCreatureSpeed(), 4);
	EXPECT_EQ(hero->movementPointsLimit(), 200); // New Horizons uses a fixed base, independent of creature speed/capacity.
	EXPECT_EQ(hero->movementPointsRemaining(), 200);
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1000);
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::LEADERSHIP), 0);
	ASSERT_TRUE(hero->getSiegeCapabilities());
	EXPECT_EQ(hero->getSiegeCapabilities()->artilleryRank, 3);
	EXPECT_EQ(hero->getSiegeCapabilities()->ballistaDamageMultiplier, 4);
	EXPECT_EQ(hero->getSiegeCapabilities()->ballistaControlChance, 100);
	EXPECT_TRUE(hero->hasArt(ArtifactID::BALLISTA, true));
	EXPECT_FALSE(hero->hasSpellbook());
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	const auto * neutral = findObjectAt({15, 10, 0});
	ASSERT_NE(neutral, nullptr);
	EXPECT_EQ(neutral->ID, Obj::MONSTER);
	EXPECT_EQ(neutral->subID, archer.getNum());
	ASSERT_EQ(builder.buildAndDump(name), expected);
	const auto path = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(path.string().c_str(), "rb"), &gzclose);
	ASSERT_NE(input, nullptr);
	std::vector<uint8_t> actual;
	std::array<uint8_t, 4096> chunk;
	int count = 0;
	while((count = gzread(input.get(), chunk.data(), chunk.size())) > 0)
		actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
	ASSERT_EQ(count, 0);
	ASSERT_TRUE(gzeof(input.get()));
	ASSERT_EQ(gzclose(input.release()), Z_OK);
	ASSERT_EQ(actual, expected);
	MapServiceTinyH3M exported(actual, nullptr);
	ASSERT_NE(exported.loadMap(ResourcePath(name), gameState().get()), nullptr);
}

TEST_F(NewHorizonsHeroFixtureExportTest, ExportOrdinaryCapabilityOnlyIdentityControl)
{
	const auto * enabled = std::getenv("NH_EXPORT_CAPABILITY_ONLY_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Separate diagnostic preset requires NH_EXPORT_CAPABILITY_ONLY_FIXTURES=1";
	ASSERT_TRUE(newHorizonsHeroes::usesRules(LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES)));
	ASSERT_FALSE(newHorizonsHeroes::usesRules(LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS)))
		<< "This control requires a separately identified capability-only preset, not the all-features candidate";
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsCapabilities"));
	const std::string name = "NHCapabilitiesOnlyControl";
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Nondefault capability-only saved-identity diagnostic. Use its separately audited preset, "
			"Red human/Blue computer and Gold bonuses. Primary ratings2/2/3/10 are authored map values, "
			"not a growth profile. Inspect leadership and siege without a primary growth section. "
			"Save normally, quit, then load that save in the separately audited all-features candidate: "
			"absence of primary growth must persist. Do not edit the save or frontend state. "
			"This is not evidence that the default preset starts capability-only heroes.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({11, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0).heroPrimary(2, 2, 3, 10)
		.heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}, {SecondarySkill::ARTILLERY, 3}})
		.heroGarrison({{creatureByName("core:pikeman"), 1000}}).heroSpells({})
		.heroEquipped({{ArtifactPosition::MACH1, ArtifactID::BALLISTA}});
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	ASSERT_NE(parser.loadMapHeader(ResourcePath(name)), nullptr);
	// Actual preset initialization only. No mapLoaded, rule injection or save editing.
	startWithMap(builder);
	ASSERT_EQ(gameState()->getHeroCapabilityRules(), canonical);
	ASSERT_FALSE(newHorizonsHeroes::usesRules(gameState()->getHeroDevelopmentRules()));
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_FALSE(hero->getPrimaryGrowthView());
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::ATTACK), 2);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::DEFENSE), 2);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::SPELL_POWER), 3);
	EXPECT_EQ(hero->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 10);
	EXPECT_EQ(hero->getManaAvailable(), 100);
	EXPECT_EQ(hero->manaLimit(), 100);
	ASSERT_TRUE(hero->getLeadershipCapacity());
	EXPECT_EQ(hero->getLeadershipCapacity()->capacity, 750);
	EXPECT_EQ(hero->getLeadershipCapacity()->used, 1000);
	EXPECT_EQ(hero->getLeadershipCapacity()->movementPercent, 75);
	EXPECT_EQ(hero->movementPointsRemaining(), 200);
	ASSERT_TRUE(hero->getSiegeCapabilities());
	EXPECT_EQ(hero->getSiegeCapabilities()->ballistaDamageMultiplier, 4);
	EXPECT_TRUE(hero->hasArt(ArtifactID::BALLISTA, true));
	ASSERT_EQ(builder.buildAndDump(name), expected);
	const auto path = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(path.string().c_str(), "rb"), &gzclose);
	ASSERT_NE(input, nullptr);
	std::vector<uint8_t> actual;
	std::array<uint8_t, 4096> chunk;
	int count = 0;
	while((count = gzread(input.get(), chunk.data(), chunk.size())) > 0)
		actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
	ASSERT_EQ(count, 0);
	ASSERT_TRUE(gzeof(input.get()));
	ASSERT_EQ(gzclose(input.release()), Z_OK);
	ASSERT_EQ(actual, expected);
	MapServiceTinyH3M exported(actual, nullptr);
	ASSERT_NE(exported.loadMap(ResourcePath(name), gameState().get()), nullptr);
}

TEST_F(NewHorizonsHeroFixtureExportTest, ExportOrdinaryKnightWithNearbyExperienceQuest)
{
	const auto * enabled = std::getenv("NH_EXPORT_HERO_FIXTURES");
	if(!enabled || std::string(enabled) != "1")
		GTEST_SKIP() << "Build-owned opt-in export requires NH_EXPORT_HERO_FIXTURES=1";
	if(!newHorizonsHeroes::usesRules(LIBRARY->engineSettings()->getValue(EGameSettings::HEROES_NEW_HORIZONS)))
		GTEST_SKIP() << "Requires the activated New Horizons preset; no fixture rule overrides";
	const JsonNode canonical(JsonPath::builtin("config/newHorizonsHeroes"));
	const std::string name = "NHHeroGrowthXP";
	const ArtifactID axe(ArtifactID::decode("core:centaurAxe"));
	using Builder = TinyH3M::TinyH3MBuilder;
	Builder builder(EMapFormat::SOD);
	builder.size(36, false).name(name)
		.description("Hero growth diagnostic, not a balanced scenario. Red human, Blue computer, Gold bonuses. "
			"Orrin starts at level1 with the New Horizons Knight profile (30/45/10/15) and Pathfinding only; "
			"no authored primary overrides. "
			"Open hero development and save. Pick up the Centaur's Axe at18,12 (+2 Attack): inspect auto-equip, "
			"move it to backpack and re-equip; Base must not change. Visit the nearby Seer Hut east at21,10: its Orrin-only quest "
			"awards1000XP through normal dialogue, without taking troops. Complete the level-up choice, "
			"inspect actual gains and mana limit, save/reload. First growth should be6/7/2/3: "
			"no starting skill grants extra primary rolls. Do not compare class starts to old-save stats; "
			"old saves retain their captured legacy rules.")
		.playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.town({8, 10, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({})
		.town({30, 30, 0}, FactionID::CASTLE, PlayerColor(1)).townGarrison({})
		.hero({17, 10, 0}, HeroTypeID(0), PlayerColor(0))
		.heroExperience(0).heroSecondarySkills({{SecondarySkill::PATHFINDING, 1}})
		.heroGarrison({{creatureByName("dendroidGuard"), 150}, {creatureByName("grandElf"), 20}})
		.heroSpells({SpellID::HASTE, SpellID::MAGIC_ARROW})
		.heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}})
		.seerHut({21, 10, 0}, Builder::missionHero(HeroTypeID(0)), Builder::rewardExperience(1000))
		.artifact({18, 12, 0}, axe);
	const auto expected = builder.build();
	MapServiceTinyH3M parser(expected, nullptr);
	const auto header = parser.loadMapHeader(ResourcePath(name));
	ASSERT_NE(header, nullptr);
	ASSERT_EQ(header->version, EMapFormat::SOD);
	// Ordinary initialization: deliberately no mapLoaded/settings/rules override.
	startWithMap(builder);
	ASSERT_EQ(gameState()->getHeroDevelopmentRules(), canonical);
	const auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto view = hero->getPrimaryGrowthView();
	ASSERT_TRUE(view);
	EXPECT_EQ(hero->level, 1);
	EXPECT_EQ(view->base, (std::array<int, 4>{30, 45, 10, 15}));
	EXPECT_EQ(view->modified, view->base);
	EXPECT_EQ(view->profile.progressionVersion, newHorizonsHeroes::PRIMARY_PROFILE_VERSION_STARTING_AND_GROWTH);
	EXPECT_EQ(view->profile.growth, (std::array<int, 4>{6, 7, 2, 3}));
	EXPECT_TRUE(view->extraGrowth.empty());
	EXPECT_EQ(hero->getManaAvailable(), 15);
	EXPECT_EQ(hero->manaLimit(), 15);
	ASSERT_TRUE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(1))->isHuman());
	ASSERT_NE(findObjectAt({21, 10, 0}), nullptr);
	const auto * pickup = findObjectAt({18, 12, 0});
	ASSERT_NE(pickup, nullptr);
	EXPECT_EQ(pickup->ID, Obj::ARTIFACT);
	EXPECT_EQ(pickup->subID, axe.getNum());
	ASSERT_EQ(builder.buildAndDump(name), expected);
	const auto path = VCMIDirs::get().userCachePath() / "testMaps" / (name + ".h3m");
	std::unique_ptr<gzFile_s, decltype(&gzclose)> input(gzopen(path.string().c_str(), "rb"), &gzclose);
	ASSERT_NE(input, nullptr);
	std::vector<uint8_t> actual;
	std::array<uint8_t, 4096> chunk;
	int count = 0;
	while((count = gzread(input.get(), chunk.data(), chunk.size())) > 0)
		actual.insert(actual.end(), chunk.begin(), chunk.begin() + count);
	ASSERT_EQ(count, 0);
	ASSERT_TRUE(gzeof(input.get()));
	ASSERT_EQ(gzclose(input.release()), Z_OK);
	ASSERT_EQ(actual, expected);
	MapServiceTinyH3M exported(actual, nullptr);
	ASSERT_NE(exported.loadMap(ResourcePath(name), gameState().get()), nullptr);
}
