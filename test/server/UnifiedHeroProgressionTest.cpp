/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../mock/TinyMapGameTest.h"
#include "../mock/GameHandlerTestServer.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/entities/hero/CHeroHandler.h"
#include "../../lib/bonuses/NativeBonusGraphAccess.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/serializer/CLoadFile.h"
#include "../../lib/serializer/CSaveFile.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../server/CGameHandler.h"
#include "../../server/TurnTimerHandler.h"
#include "../../server/battles/BattleProcessor.h"
#include "../../server/processors/HeroPoolProcessor.h"
#include "../../server/processors/PlayerMessageProcessor.h"
#include "../../server/processors/TurnOrderProcessor.h"
#include "../../server/queries/MapQueries.h"
#include "../../server/queries/QueriesProcessor.h"
#include <algorithm>
#include <boost/filesystem/operations.hpp>

namespace
{
constexpr PlayerColor HUMAN(0);
constexpr PlayerColor AI_PLAYER(1);
constexpr auto AUTHORITY = "new-horizons:authority";

// Same bounded native-envelope fixture pattern as SaveParityTest. The guarded
// test driver binds the process temporary directory to private disposable data.
struct PendingProgressionSave
{
	boost::filesystem::path directory = boost::filesystem::temp_directory_path()
		/ boost::filesystem::unique_path("nh-pending-perk-%%%%%%%%-%%%%%%%%");
	boost::filesystem::path path = directory / "pending-perk.vsgm1";
	PendingProgressionSave() { boost::filesystem::create_directory(directory); }
	~PendingProgressionSave() { boost::filesystem::remove_all(directory); }
};

class UnifiedHeroProgressionTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	void configurePlayer(PlayerSettings & player) const override
	{
		if(player.color == AI_PLAYER) player.connectedPlayerIDs.clear();
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(bool secondHumanHero = false, uint32_t startingExperience = 0)
	{
		TinyH3M::TinyH3MBuilder map(EMapFormat::SOD);
		map.size(36, false).playerActive(HUMAN).playerActive(AI_PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), HUMAN)
			.heroGarrison({{CreatureID(0), 1}}).heroExperience(startingExperience)
			.hero({8, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), AI_PLAYER)
			.heroGarrison({{CreatureID(0), 1}});
		if(secondHumanHero)
			map.hero({6, 7, 0}, HeroTypeID(HeroTypeID::decode("core:christian")), HUMAN)
				.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(map));
		hero = findHeroByOwner(HUMAN);
		ASSERT_NE(hero, nullptr);
		ASSERT_TRUE(newHorizonsHeroes::usesUnifiedSkillProgression(hero->getPerkState().rules));
		server = std::make_unique<GameHandlerTestServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->randomizer->setSeed(1337);
		handler->onAdvInterfaceReady(HUMAN);
		ASSERT_NO_FATAL_FAILURE(drain(HUMAN));
	}
	void TearDown() override
	{
		handler.reset();
		server.reset();
		TinyMapGameTest::TearDown();
	}
	std::shared_ptr<CHeroLevelUpDialogQuery> current(PlayerColor player = HUMAN) const
	{
		return std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler->queries->topQuery(player));
	}
	void reply(size_t choice = 0, PlayerColor player = HUMAN)
	{
		const auto query = current(player);
		ASSERT_TRUE(query);
		ASSERT_TRUE(query->isValidReply(static_cast<int32_t>(choice)));
		ASSERT_TRUE(handler->queryReply(query->queryID, static_cast<int32_t>(choice), player));
	}
	void drain(PlayerColor player)
	{
		for(size_t count = 0; count < 128; ++count)
		{
			const auto query = current(player);
			if(!query) return;
			size_t choice = 0;
			while(choice < query->hlu.perks.size()
				&& query->hlu.perks[choice].selection.perkId == "new-horizons:learning.prodigy") ++choice;
			ASSERT_NO_FATAL_FAILURE(reply(choice, player));
		}
		FAIL() << "Progression did not reach an ordinary action boundary";
	}
	SecondarySkill authority() const { return SecondarySkill(SecondarySkill::decode(AUTHORITY)); }
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
	CGHeroInstance * hero = nullptr;
};
}

TEST_F(UnifiedHeroProgressionTest, ExternalMultiRankGrantOffersOneCompleteTierAtATime)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(hero->getSecSkillLevel(authority()), 0);
	handler->changeSecSkill(hero, authority(), 3, ChangeValueMode::ABSOLUTE);
	for(const auto [rank, size] : {std::pair{1, 4u}, std::pair{2, 4u}, std::pair{3, 2u}})
	{
		const auto query = current();
		ASSERT_TRUE(query);
		EXPECT_TRUE(query->hlu.perkOnly);
		ASSERT_EQ(query->hlu.perks.size(), size);
		EXPECT_EQ(query->hlu.perks.front().requiredRank, rank);
		EXPECT_EQ(handler->pendingHeroLevelUps.size(), 1u);
		EXPECT_FALSE(query->isValidReply(-1));
		EXPECT_FALSE(query->isValidReply(static_cast<int32_t>(size)));
		ASSERT_NO_FATAL_FAILURE(reply());
	}
	EXPECT_FALSE(current());
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
	const auto choices = hero->getPerkState().selected;
	handler->changeSecSkill(hero, authority(), 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getPerkState().selected, choices);
	EXPECT_FALSE(current());
}

TEST_F(UnifiedHeroProgressionTest, AuthoredStartingExperienceResolvesAllLevelsAndStartingTierDebt)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false, 4600));
	EXPECT_EQ(hero->exp, 4600);
	EXPECT_EQ(hero->level, 5);
	EXPECT_FALSE(current());
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
	EXPECT_TRUE(hero->getPerkState().nextRequiredPerkOffer([&](const std::string & skill)
		{ return hero->getPerkSkillRank(skill); }).empty());
	handler->resumeMasteryQueries(HUMAN);
	EXPECT_EQ(hero->level, 5);
	EXPECT_EQ(hero->exp, 4600);
}

TEST_F(UnifiedHeroProgressionTest, OrdinarySkillThenPerkIsOneLevelAndStaleReplyCannotGrantAgain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto oldLevel = hero->level;
	handler->giveExperience(hero, LIBRARY->heroh->reqExp(oldLevel + 1) - hero->exp);
	const auto skill = current();
	ASSERT_TRUE(skill);
	ASSERT_FALSE(skill->hlu.skills.empty());
	EXPECT_FALSE(skill->hlu.perkOnly);
	EXPECT_TRUE(skill->hlu.perks.empty());
	EXPECT_EQ(hero->level, oldLevel + 1);
	EXPECT_TRUE(handler->pendingHeroLevelUps.at(hero->id).restoredOffer);
	EXPECT_NO_THROW(handler->validatePendingHeroProgression());
	ASSERT_NO_FATAL_FAILURE(reply());
	const auto perk = current();
	ASSERT_TRUE(perk);
	EXPECT_TRUE(perk->hlu.perkOnly);
	EXPECT_EQ(hero->level, oldLevel + 1);
	const auto before = hero->getPerkState().selected;
	EXPECT_FALSE(handler->queryReply(skill->queryID, 0, HUMAN));
	EXPECT_EQ(hero->getPerkState().selected, before);
	ASSERT_NO_FATAL_FAILURE(reply());
	EXPECT_EQ(hero->level, oldLevel + 1);
	EXPECT_EQ(hero->getPerkState().selected.size(), before.size() + 1);
}

TEST_F(UnifiedHeroProgressionTest, PendingOrdinaryOfferRoundtripResumesWithoutRepeatingLevelGrowth)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	handler->giveExperience(hero, LIBRARY->heroh->reqExp(hero->level + 1) - hero->exp);
	ASSERT_TRUE(current());
	const auto expected = current()->hlu;
	const auto level = hero->level;
	const auto qid = handler->QID;
	CMemorySerializer memory;
	memory.oser & handler->pendingHeroLevelUps;
	std::map<ObjectInstanceID, HeroLevelUp> restored;
	memory.iser & restored;
	handler.reset();
	handler = std::make_unique<CGameHandler>(*server, gameState());
	handler->QID = qid;
	handler->pendingHeroLevelUps = std::move(restored);
	handler->onAdvInterfaceReady(HUMAN);
	ASSERT_TRUE(current());
	EXPECT_EQ(hero->level, level);
	EXPECT_EQ(current()->hlu.skills, expected.skills);
	EXPECT_EQ(current()->hlu.optionalRerollAvailable, expected.optionalRerollAvailable);
	EXPECT_NE(current()->queryID, expected.queryID);
	EXPECT_TRUE(current()->hlu.restoredOffer);
	ASSERT_NO_FATAL_FAILURE(drain(HUMAN));
	EXPECT_EQ(hero->level, level);
}

TEST_F(UnifiedHeroProgressionTest, PendingPerkRoundtripRetainsExactTierAndPermitsSafeSaveOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	handler->changeSecSkill(hero, authority(), 2, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(reply()); // Basic selected; Advanced is pending.
	ASSERT_TRUE(current());
	const auto expected = current()->hlu.perks;
	CMemorySerializer memory;
	memory.oser & handler->pendingHeroLevelUps;
	std::map<ObjectInstanceID, HeroLevelUp> restored;
	memory.iser & restored;
	handler.reset();
	handler = std::make_unique<CGameHandler>(*server, gameState());
	handler->pendingHeroLevelUps = std::move(restored);
	handler->onAdvInterfaceReady(HUMAN);
	ASSERT_TRUE(current());
	EXPECT_EQ(current()->hlu.perks, expected);
	SaveGame save;
	EXPECT_FALSE(current()->blocksPack(&save));
	EXPECT_NO_THROW(newHorizonsLevelUp::validateNoPendingOptionalOffer(*handler));
	const auto other = std::make_shared<CGenericQuery>(handler.get(), AI_PLAYER,
		[](std::optional<int32_t>) {});
	handler->queries->addQuery(other);
	EXPECT_TRUE(current()->blocksPack(&save));
	EXPECT_THROW(newHorizonsLevelUp::validateNoPendingOptionalOffer(*handler), std::runtime_error);
}

TEST_F(UnifiedHeroProgressionTest, FullNativeSaveLoadsPendingPerkIntoNewWorldAndGrantsExactlyOnce)
{
	NativeBonusGraphAccess graphAccess;
	ASSERT_NO_FATAL_FAILURE(prepare());
	handler->changeSecSkill(hero, authority(), 2, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(reply()); // Preserve Basic; Advanced remains mandatory.
	const auto pending = current();
	ASSERT_TRUE(pending);
	ASSERT_TRUE(pending->hlu.perkOnly);
	ASSERT_EQ(pending->hlu.perks.front().requiredRank, 2);
	const auto offer = pending->hlu;
	const auto selected = hero->getPerkState().selected;
	const auto level = hero->level;
	std::array<int, GameConstants::PRIMARY_SKILLS> primary{};
	for(size_t index = 0; index < primary.size(); ++index)
		primary[index] = hero->getBasePrimarySkillValue(PrimarySkill(index));
	map()->registerMapStrings();
	PendingProgressionSave save;
	{
		CSaveFile file;
		gameState()->saveGame(file);
		file.save(*handler);
		ASSERT_LT(file.currentContent().size(), 64u * 1024u * 1024u);
		file.write(save.path);
	}
	ASSERT_TRUE(boost::filesystem::is_regular_file(save.path));
	ASSERT_GT(boost::filesystem::file_size(save.path), 0u);
	ASSERT_LT(boost::filesystem::file_size(save.path), 64u * 1024u * 1024u);
	auto loadedWorld = std::make_shared<CGameState>();
	loadedWorld->preInit(LIBRARY);
	GameHandlerTestServer loadedServer(loadedWorld, HUMAN);
	auto loadedHandler = std::make_unique<CGameHandler>(loadedServer, loadedWorld);
	{
		CLoadFile file(save.path, loadedWorld.get());
		ASSERT_EQ(file.formatVersion(), ESerializationVersion::CURRENT);
		loadedWorld->loadGame(file); // Includes ordinary map-script load hooks.
		file.load(*loadedHandler);
		ASSERT_TRUE(file.atEnd());
	}
	const StartInfo resumedOptions = *loadedWorld->getStartInfo();
	loadedWorld->updateOnLoad(resumedOptions);
	auto * loadedHero = loadedWorld->getHero(hero->id);
	ASSERT_NE(loadedHero, nullptr);
	ASSERT_NE(loadedWorld.get(), gameState().get());
	ASSERT_NE(loadedHero, hero);
	ASSERT_NE(loadedHandler.get(), handler.get());
	EXPECT_EQ(loadedHero->getPerkState().selected, selected);
	EXPECT_EQ(loadedHero->getCapabilityRules(), hero->getCapabilityRules());
	EXPECT_EQ(loadedHero->getPerkState().rules, hero->getPerkState().rules);
	ASSERT_EQ(loadedHandler->pendingHeroLevelUps.at(hero->id).perks, offer.perks);
	EXPECT_FALSE(loadedHandler->queries->topQuery(HUMAN));
	loadedHandler->onAdvInterfaceReady(HUMAN);
	const auto restored = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(loadedHandler->queries->topQuery(HUMAN));
	ASSERT_TRUE(restored);
	EXPECT_EQ(restored->hero, loadedHero);
	EXPECT_EQ(restored->hlu.perks, offer.perks);
	EXPECT_NE(restored->queryID, offer.queryID);
	EXPECT_EQ(loadedHero->level, level);
	EXPECT_FALSE(loadedHandler->queryReply(offer.queryID, 0, HUMAN));
	EXPECT_EQ(loadedHero->getPerkState().selected, selected);
	ASSERT_TRUE(loadedHandler->queryReply(restored->queryID, 0, HUMAN));
	EXPECT_TRUE(loadedHero->getPerkState().hasSelection(AUTHORITY, offer.perks.front().selection.perkId));
	EXPECT_EQ(loadedHero->getPerkState().selected.size(), selected.size() + 1);
	EXPECT_TRUE(loadedHandler->pendingHeroLevelUps.empty());
	EXPECT_FALSE(loadedHandler->queries->topQuery(HUMAN));
	EXPECT_FALSE(loadedHandler->queryReply(restored->queryID, 0, HUMAN));
	loadedHandler->resumeMasteryQueries(HUMAN);
	EXPECT_EQ(loadedHero->getPerkState().selected.size(), selected.size() + 1);
	EXPECT_EQ(loadedHero->level, level);
	for(size_t index = 0; index < primary.size(); ++index)
		EXPECT_EQ(loadedHero->getBasePrimarySkillValue(PrimarySkill(index)), primary[index]);
	EXPECT_EQ(hero->getPerkState().selected, selected); // Original world is isolated.
	EXPECT_EQ(handler->pendingHeroLevelUps.at(hero->id).perks, offer.perks);
}

TEST_F(UnifiedHeroProgressionTest, ExternalGrantDefersPromptUntilObjectContinuationCompletes)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	bool completed = false;
	auto visit = std::make_shared<CGenericQuery>(handler.get(), HUMAN,
		[&](std::optional<int32_t>) { completed = true; });
	handler->queries->addQuery(visit);
	handler->changeSecSkill(hero, authority(), 1, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(handler->queries->topQuery(HUMAN), visit);
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
	ASSERT_TRUE(handler->queryReply(visit->queryID, 0, HUMAN));
	EXPECT_TRUE(completed);
	handler->resumeMasteryQueries(HUMAN);
	ASSERT_TRUE(current());
	EXPECT_TRUE(current()->hlu.perkOnly);
	ASSERT_NO_FATAL_FAILURE(reply());
}

TEST_F(UnifiedHeroProgressionTest, AIStartingSkillsUseRealCallbackQueryWithoutHumanReadiness)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto * aiHero = findHeroByOwner(AI_PLAYER);
	ASSERT_NE(aiHero, nullptr);
	ASSERT_FALSE(gameState()->getPlayerState(AI_PLAYER)->isHuman());
	handler->resumeMasteryQueries(AI_PLAYER);
	const auto query = current(AI_PLAYER);
	ASSERT_TRUE(query);
	EXPECT_TRUE(query->prompted);
	EXPECT_TRUE(query->hlu.perkOnly);
	EXPECT_FALSE(handler->uiReadyForDialogs.contains(AI_PLAYER));
	ASSERT_NO_FATAL_FAILURE(drain(AI_PLAYER));
	EXPECT_TRUE(aiHero->getPerkState().nextRequiredPerkOffer([&](const std::string & skill)
		{ return aiHero->getPerkSkillRank(skill); }).empty());
}

TEST_F(UnifiedHeroProgressionTest, NativeExperienceCurveRetainsOriginalThresholds)
{
	for(const auto [level, experience] : {std::pair{2u, 1000u}, std::pair{5u, 4600u},
		std::pair{10u, 14700u}, std::pair{15u, 34140u}, std::pair{16u, 40567u},
		std::pair{20u, 81961u}, std::pair{30u, 496963u}})
		EXPECT_EQ(LIBRARY->heroh->reqExp(level), experience);
}

TEST_F(UnifiedHeroProgressionTest, EmptyOrdinaryOfferStillPublishesExactlyOneLevel)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto level = hero->level;
	HeroLevelUp offer;
	offer.heroId = hero->id;
	offer.player = HUMAN;
	offer.reachedLevel = level + 1;
	handler->sendAndApply(offer);
	EXPECT_EQ(hero->level, level + 1);
	EXPECT_THROW(handler->sendAndApply(offer), std::runtime_error);
	EXPECT_EQ(hero->level, level + 1);
}

TEST_F(UnifiedHeroProgressionTest, ProdigyCounterAndPendingChoiceRoundtripCompleteThreeLevelsOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SecondarySkill learning(SecondarySkill::decode("new-horizons:learning"));
	ASSERT_EQ(hero->getSecSkillLevel(learning), 0);
	handler->changeSecSkill(hero, learning, 3, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(reply()); // Basic Learning perk.
	ASSERT_NO_FATAL_FAILURE(reply()); // Advanced Learning perk.
	ASSERT_TRUE(current());
	const auto & expert = current()->hlu.perks;
	const auto prodigy = std::find_if(expert.begin(), expert.end(), [](const auto & candidate)
		{ return candidate.selection.perkId == "new-horizons:learning.prodigy"; });
	ASSERT_NE(prodigy, expert.end());
	const auto level = hero->level;
	ASSERT_NO_FATAL_FAILURE(reply(std::distance(expert.begin(), prodigy)));
	ASSERT_TRUE(current());
	EXPECT_EQ(hero->level, level + 1);
	ASSERT_EQ(handler->pendingProdigyLevels.at(hero->id), 2);
	CMemorySerializer memory;
	memory.oser & handler->pendingHeroLevelUps;
	memory.oser & handler->pendingProdigyLevels;
	std::map<ObjectInstanceID, HeroLevelUp> restoredOffers;
	std::map<ObjectInstanceID, uint8_t> restoredProdigy;
	memory.iser & restoredOffers;
	memory.iser & restoredProdigy;
	handler.reset();
	handler = std::make_unique<CGameHandler>(*server, gameState());
	handler->pendingHeroLevelUps = std::move(restoredOffers);
	handler->pendingProdigyLevels = std::move(restoredProdigy);
	handler->validatePendingProdigyLevels();
	handler->onAdvInterfaceReady(HUMAN);
	ASSERT_NO_FATAL_FAILURE(drain(HUMAN));
	EXPECT_EQ(hero->level, level + 3);
	EXPECT_TRUE(handler->pendingProdigyLevels.empty());
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
	handler->resumeMasteryQueries(HUMAN);
	EXPECT_EQ(hero->level, level + 3);
}

TEST_F(UnifiedHeroProgressionTest, TwoHeroesRetainSeparateMandatoryTierDebtWithoutOverwritingOffers)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto * other = findHeroAt({6, 7, 0});
	ASSERT_NE(other, nullptr);
	ASSERT_EQ(hero->getSecSkillLevel(authority()), 0);
	ASSERT_EQ(other->getSecSkillLevel(authority()), 0);
	handler->changeSecSkill(hero, authority(), 1, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(current());
	const auto first = current()->queryID;
	handler->changeSecSkill(other, authority(), 1, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(current()->queryID, first);
	ASSERT_NO_FATAL_FAILURE(reply());
	ASSERT_TRUE(current());
	EXPECT_EQ(current()->hero->id, other->id);
	EXPECT_EQ(handler->pendingHeroLevelUps.size(), 1u);
	ASSERT_NO_FATAL_FAILURE(reply());
	EXPECT_FALSE(current());
	EXPECT_TRUE(hero->getPerkState().hasPerkForTier(AUTHORITY, 1));
	EXPECT_TRUE(other->getPerkState().hasPerkForTier(AUTHORITY, 1));
}

TEST_F(UnifiedHeroProgressionTest, ScriptedTransferPreservesExactOrdinaryOfferAndAppliedLevel)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	handler->resumeMasteryQueries(AI_PLAYER);
	ASSERT_NO_FATAL_FAILURE(drain(AI_PLAYER));
	handler->giveExperience(hero, LIBRARY->heroh->reqExp(hero->level + 1) - hero->exp);
	const auto previous = current();
	ASSERT_TRUE(previous);
	const auto expected = previous->hlu;
	const auto level = hero->level;
	handler->setOwner(hero, AI_PLAYER);
	EXPECT_FALSE(current());
	EXPECT_EQ(previous->hero, nullptr);
	EXPECT_FALSE(previous->isValidReply(0));
	ASSERT_TRUE(current(AI_PLAYER));
	EXPECT_EQ(current(AI_PLAYER)->hero, hero);
	EXPECT_EQ(current(AI_PLAYER)->hlu.skills, expected.skills);
	EXPECT_EQ(current(AI_PLAYER)->hlu.primaryGains, expected.primaryGains);
	EXPECT_EQ(current(AI_PLAYER)->hlu.optionalRerollAvailable, expected.optionalRerollAvailable);
	EXPECT_NE(current(AI_PLAYER)->queryID, expected.queryID);
	EXPECT_FALSE(handler->queryReply(expected.queryID, 0, HUMAN));
	EXPECT_EQ(hero->level, level);
	ASSERT_NO_FATAL_FAILURE(drain(AI_PLAYER));
	EXPECT_EQ(hero->level, level);
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
}

TEST_F(UnifiedHeroProgressionTest, CoveredMandatoryQueryTransfersWithoutExecutingUnrelatedContinuation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	handler->resumeMasteryQueries(AI_PLAYER);
	ASSERT_NO_FATAL_FAILURE(drain(AI_PLAYER));
	handler->changeSecSkill(hero, authority(), 1, ChangeValueMode::ABSOLUTE);
	const auto previous = current();
	ASSERT_TRUE(previous);
	const auto pool = previous->hlu.perks;
	bool continued = false;
	const auto cover = std::make_shared<CGenericQuery>(handler.get(), HUMAN,
		[&](std::optional<int32_t>) { continued = true; });
	handler->queries->addQuery(cover);
	handler->setOwner(hero, AI_PLAYER);
	EXPECT_EQ(handler->queries->topQuery(HUMAN), cover);
	EXPECT_FALSE(continued);
	EXPECT_EQ(previous->hero, nullptr);
	EXPECT_FALSE(handler->queries->getQuery(previous->queryID));
	ASSERT_TRUE(current(AI_PLAYER));
	EXPECT_EQ(current(AI_PLAYER)->hlu.perks, pool);
	ASSERT_NO_FATAL_FAILURE(reply(0, AI_PLAYER));
	EXPECT_TRUE(hero->getPerkState().hasPerkForTier(AUTHORITY, 1));
	ASSERT_TRUE(handler->queryReply(cover->queryID, 0, HUMAN));
	EXPECT_TRUE(continued);
	EXPECT_FALSE(current());
}

TEST_F(UnifiedHeroProgressionTest, ScriptedRemovalErasesOnlyRemovedHeroAndResumesOtherMandatoryChoice)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	const auto * other = findHeroAt({6, 7, 0});
	ASSERT_NE(other, nullptr);
	handler->changeSecSkill(hero, authority(), 1, ChangeValueMode::ABSOLUTE);
	const auto previous = current();
	ASSERT_TRUE(previous);
	const auto removedId = hero->id;
	handler->changeSecSkill(other, authority(), 1, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(handler->removeObject(hero, HUMAN));
	hero = nullptr;
	EXPECT_FALSE(gameState()->getHero(removedId));
	EXPECT_EQ(previous->hero, nullptr);
	EXPECT_FALSE(previous->isValidReply(0));
	EXPECT_FALSE(handler->pendingHeroLevelUps.contains(removedId));
	ASSERT_TRUE(current());
	EXPECT_EQ(current()->hero, other);
	EXPECT_FALSE(handler->queryReply(previous->queryID, 0, HUMAN));
	ASSERT_NO_FATAL_FAILURE(reply());
	EXPECT_TRUE(other->getPerkState().hasPerkForTier(AUTHORITY, 1));
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
}

TEST_F(UnifiedHeroProgressionTest, TransferToNeutralCompletesPendingChoiceWithoutRepeatingLevelGrowth)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	handler->giveExperience(hero, LIBRARY->heroh->reqExp(hero->level + 1) - hero->exp);
	const auto previous = current();
	ASSERT_TRUE(previous);
	const auto level = hero->level;
	handler->setOwner(hero, PlayerColor::NEUTRAL);
	EXPECT_EQ(hero->level, level);
	EXPECT_FALSE(current());
	EXPECT_EQ(previous->hero, nullptr);
	EXPECT_TRUE(handler->pendingHeroLevelUps.empty());
	EXPECT_TRUE(hero->getPerkState().nextRequiredPerkOffer([&](const std::string & skill)
		{ return hero->getPerkSkillRank(skill); }).empty());
}
