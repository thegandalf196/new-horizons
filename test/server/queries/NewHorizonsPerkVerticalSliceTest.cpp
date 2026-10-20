/*
 * NewHorizonsPerkVerticalSliceTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later
 */
#include "StdInc.h"

#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsPerkRules.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/CTypeList.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../server/queries/MapQueries.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

namespace
{
constexpr auto rampartHeroId = HeroTypeID(16); // Mephala, a Rampart Ranger.
constexpr auto christianHeroId = HeroTypeID(6); // Christian, a Castle Knight.
constexpr auto sylvanLuckId = "new-horizons:sylvanLuck";
constexpr auto sorceryMagicId = "new-horizons:sorceryMagic";
constexpr auto overchargerId = "new-horizons:sorceryMagic.overcharger";
constexpr auto disciplineId = "new-horizons:discipline";
constexpr auto inspirationalLeaderId = "new-horizons:discipline.inspirationalLeader";
constexpr auto havocMagicId = "new-horizons:havocMagic";
constexpr auto stormcallerId = "new-horizons:havocMagic.stormcaller";
constexpr auto controlledBlastId = "new-horizons:havocMagic.controlledBlast";

class PendingPerkCommandServer final : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;
	std::vector<PackageReceived> received;
	std::vector<PackageApplied> responses;
	std::vector<std::string> events;

	void sendPack(CPackForClient & pack, GameConnectionID connection) override
	{
		EXPECT_EQ(connection, GameConnectionID::FIRST_CONNECTION);
		if(const auto * receipt = dynamic_cast<const PackageReceived *>(&pack))
			received.push_back(*receipt);
		if(const auto * response = dynamic_cast<const PackageApplied *>(&pack))
		{
			responses.push_back(*response);
			events.push_back(response->result ? "accepted" : "rejected");
		}
	}

	void applyPack(CPackForClient & pack) override
	{
		if(dynamic_cast<const HeroPerkChosen *>(&pack))
			events.push_back("perk");
		if(dynamic_cast<const QueryResolved *>(&pack))
			events.push_back("resolved");
		GameHandlerTestServer::applyPack(pack);
	}
};

class NewHorizonsPerkVerticalSliceTest : public TinyMapGameTest
{
protected:
	bool activateControlledBlastOfferFixture = false;

	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
		loaded->overrideGameSetting(EGameSettings::COMBAT_HERO_COMMANDS,
			combat["combat"]["heroCommands"]);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(activateControlledBlastOfferFixture)
		{
			bool found = false;
			for(auto & [skillId, skill] : perkRules["skills"].Struct())
			{
				(void)skillId;
				for(auto & perk : skill["perks"].Vector())
					if(perk["id"].String() == controlledBlastId)
					{
						perk["effect"]["status"].String() = "active";
						found = true;
					}
			}
			EXPECT_TRUE(found) << "Controlled Blast must remain locally offerable without changing the global planned registry";
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void startGame(HeroTypeID heroType = rampartHeroId)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, heroType, PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));
	}

	static SecondarySkill skill(const char * id)
	{
		const int decoded = SecondarySkill::decode(id);
		EXPECT_GE(decoded, 0) << "unknown secondary skill " << id;
		return SecondarySkill(decoded);
	}

	static bool offerContains(const std::vector<newHorizonsHeroes::PerkOfferCandidate> & offer,
		const char * perkId)
	{
		return std::any_of(offer.begin(), offer.end(), [perkId](const auto & candidate)
		{
			return candidate.selection.perkId == perkId;
		});
	}
};
}

TEST_F(NewHorizonsPerkVerticalSliceTest, PendingStrictPerkRejectsSaveCommandUntilAuthoritativeReply)
{
	startGame(christianHeroId);
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto player = hero->getOwner();
	ASSERT_TRUE(newHorizonsHeroes::usesStrictPerkProgression(hero->getPerkState().rules));
	PendingPerkCommandServer server(gameState(), player);
	CGameHandler handler(server, gameState());
	handler.onAdvInterfaceReady(player);
	hero->setExperience(LIBRARY->heroh->reqExp(hero->level + 1), ChangeValueMode::ABSOLUTE);
	handler.levelUpHero(hero);
	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(handler.queries->topQuery(player));
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(query->prompted);
	ASSERT_FALSE(query->hlu.perks.empty());
	const int choice = static_cast<int>(query->hlu.skills.size());
	ASSERT_TRUE(query->isValidReply(choice));
	const auto selection = query->hlu.perks.front().selection;
	const auto tier = query->hlu.perks.front().requiredRank;
	const auto selectedBefore = hero->getPerkState().selected;
	const auto levelBefore = hero->level;
	const auto experienceBefore = hero->exp;
	const auto snapshot = [](auto & value)
	{
		CMemorySerializer memory;
		memory.oser & value;
		return memory.extractBuffer();
	};
	const auto worldBefore = gameState()->saveToMemory();
	const auto rngBefore = snapshot(*handler.randomizer);
	const auto offerBefore = snapshot(query->hlu);
	const auto qidBefore = handler.QID;
	server.events.clear();

	// Exercise the actual command boundary. SaveGame's visitor reports success
	// even when disk saving throws, so a false ACK proves it was not dispatched.
	SaveGame save("Saves/PendingStrictPerkMustReject", false);
	save.player = player;
	save.requestID = 17;
	handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, save);
	ASSERT_EQ(server.received.size(), 1u);
	ASSERT_EQ(server.responses.size(), 1u);
	EXPECT_EQ(server.received.front().requestID, save.requestID);
	EXPECT_EQ(server.received.front().player, player);
	EXPECT_EQ(server.received.front().packType, CTypeList::getInstance().getTypeID(&save));
	EXPECT_EQ(server.responses.front().requestID, save.requestID);
	EXPECT_EQ(server.responses.front().player, player);
	EXPECT_EQ(server.responses.front().packType, server.received.front().packType);
	EXPECT_FALSE(server.responses.front().result);
	EXPECT_EQ(server.events, (std::vector<std::string>{"rejected"}));
	EXPECT_EQ(handler.queries->topQuery(player), query);
	EXPECT_EQ(handler.queries->countQuery(query), 1);
	EXPECT_TRUE(query->prompted);
	EXPECT_EQ(snapshot(query->hlu), offerBefore);
	EXPECT_EQ(handler.QID, qidBefore);
	EXPECT_EQ(gameState()->saveToMemory(), worldBefore);
	EXPECT_EQ(snapshot(*handler.randomizer), rngBefore);
	ASSERT_TRUE(query->isValidReply(choice));

	QueryReply reply(query->queryID, choice);
	reply.player = player;
	reply.requestID = 18;
	handler.handleReceivedPack(GameConnectionID::FIRST_CONNECTION, reply);
	ASSERT_EQ(server.received.size(), 2u);
	ASSERT_EQ(server.responses.size(), 2u);
	EXPECT_EQ(server.received.back().requestID, reply.requestID);
	EXPECT_EQ(server.received.back().packType, CTypeList::getInstance().getTypeID(&reply));
	EXPECT_EQ(server.responses.back().requestID, reply.requestID);
	EXPECT_EQ(server.responses.back().player, player);
	EXPECT_EQ(server.responses.back().packType, server.received.back().packType);
	ASSERT_TRUE(server.responses.back().result);
	EXPECT_EQ(server.events, (std::vector<std::string>{"rejected", "perk", "resolved", "accepted"}));
	EXPECT_FALSE(handler.queries->topQuery(player));
	EXPECT_EQ(handler.queries->countQuery(query), 0);
	const auto & state = hero->getPerkState();
	ASSERT_EQ(state.selected.size(), selectedBefore.size() + 1);
	EXPECT_TRUE(std::equal(selectedBefore.begin(), selectedBefore.end(), state.selected.begin()));
	EXPECT_EQ(state.selected.back(), selection);
	size_t selectedInTier = 0;
	for(const auto & selected : state.selected)
		if(selected.skillId == selection.skillId)
		{
			const auto definition = newHorizonsHeroes::perkDefinition(state.rules, selected.skillId, selected.perkId);
			ASSERT_TRUE(definition.has_value());
			selectedInTier += newHorizonsHeroes::perkRequiredRank(definition->requiredRank) == tier;
		}
	EXPECT_EQ(selectedInTier, 1u);
	ASSERT_NO_THROW(state.validate());
	EXPECT_EQ(hero->level, levelBefore);
	EXPECT_EQ(hero->exp, experienceBefore);
	EXPECT_EQ(snapshot(*handler.randomizer), rngBefore);
	// This checks only removal of the query admission blocker. The ordinary disk
	// save/Quit/reload route is separate; do not dispatch another save in this test.
	EXPECT_FALSE(handler.isBlockedByQueries(&save, player));
}


TEST_F(NewHorizonsPerkVerticalSliceTest, R2PendingQueryRefreshAndReplyStayDeterministicAndDoNotDrawWorldRng)
{
	startGame(christianHeroId);
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_TRUE(newHorizonsHeroes::usesR2PerkOffers(hero->getPerkState().rules));
	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameHandler.onAdvInterfaceReady(hero->getOwner());
	hero->setExperience(LIBRARY->heroh->reqExp(hero->level + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(query, nullptr);
	ASSERT_FALSE(query->hlu.perks.empty());
	const auto offered = query->hlu.perks;
	const auto selected = offered.front().selection;
	const auto beforePerks = hero->getPerkState().toJson();
	const auto rng = [&]()
	{
		CMemorySerializer snapshot;
		snapshot.oser & *gameHandler.randomizer;
		return snapshot.extractBuffer();
	};
	const auto beforeRng = rng();
	const int choice = static_cast<int>(query->hlu.skills.size());
	for(int attempt = 0; attempt < 4; ++attempt)
	{
		query->onExposure(nullptr); // Actual query lifecycle, not a fabricated offer.
		EXPECT_EQ(query->hlu.perks, offered);
		EXPECT_TRUE(query->isValidReply(choice));
	}
	query->hlu.perks.front().description += " forged";
	EXPECT_FALSE(query->isValidReply(choice));
	query->hlu.perks = offered;
	EXPECT_EQ(hero->getPerkState().toJson(), beforePerks);
	EXPECT_EQ(rng(), beforeRng);
	const auto selectedBefore = hero->getPerkState().selected.size();
	ASSERT_TRUE(gameHandler.queryReply(query->queryID, choice, hero->getOwner()));
	EXPECT_TRUE(hero->getPerkState().hasSelection(selected.skillId, selected.perkId));
	EXPECT_EQ(hero->getPerkState().selected.size(), selectedBefore + 1);
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ControlledBlastIsAcceptedThroughAdvancedHavocOfferAndSurvivesSaveLoad)
{
	activateControlledBlastOfferFixture = true;
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto havoc = skill(havocMagicId);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameHandler.changeSecSkill(hero, havoc, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->getSecSkillLevel(havoc), MasteryLevel::BASIC);
	EXPECT_FALSE(hero->getPerkState().canAdvanceSkillNormally(havocMagicId, MasteryLevel::BASIC));
	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};

	// On its first level-up this hero consumes a per-hero skill RNG seed before
	// the server-authored offer seed. That per-hero stream remains initialized,
	// so later level-ups consume only the offer seed from the reset global RNG.
	auto findRootSeedForOffer = [&](const char * perkId, bool heroSkillSeedInitialized)
	{
		for(int candidateSeed = 1; candidateSeed < 10000; ++candidateSeed)
		{
			CRandomGenerator probe(candidateSeed);
			if(!heroSkillSeedInitialized)
				probe.nextInt();
			const auto offerSeed = static_cast<uint64_t>(static_cast<uint32_t>(probe.nextInt()));
			if(offerContains(hero->getPerkState().prepareOffer(rankLookup, offerSeed, hero->getPerkOfferWeightContext()), perkId))
				return candidateSeed;
		}
		return 0;
	};

	const int stormcallerRootSeed = findRootSeedForOffer(stormcallerId, false);
	ASSERT_NE(stormcallerRootSeed, 0);
	gameHandler.randomizer->setSeed(stormcallerRootSeed);
	gameHandler.onAdvInterfaceReady(hero->getOwner());

	const auto firstLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(firstLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	auto firstQuery = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(firstQuery, nullptr);
	EXPECT_TRUE(std::none_of(firstQuery->hlu.perks.begin(), firstQuery->hlu.perks.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == controlledBlastId;
	})) << "Controlled Blast is not a legal offer before Havoc reaches Advanced";
	const auto stormcaller = std::find_if(firstQuery->hlu.perks.begin(), firstQuery->hlu.perks.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == stormcallerId;
	});
	ASSERT_NE(stormcaller, firstQuery->hlu.perks.end());
	const auto stormcallerChoice = static_cast<int>(firstQuery->hlu.skills.size()
		+ std::distance(firstQuery->hlu.perks.begin(), stormcaller));
	ASSERT_TRUE(firstQuery->isValidReply(stormcallerChoice));
	ASSERT_TRUE(gameHandler.queryReply(firstQuery->queryID, stormcallerChoice, hero->getOwner()));
	EXPECT_TRUE(hero->getPerkState().hasSelection(havocMagicId, stormcallerId));
	EXPECT_TRUE(hero->hasActivePerk(havocMagicId, stormcallerId));
	EXPECT_TRUE(hero->getPerkState().canAdvanceSkillNormally(havocMagicId, MasteryLevel::BASIC));

	// Exercise the normal server-validated rank advancement after Stormcaller,
	// rather than manufacturing an Advanced rank in the saved hero.
	gameHandler.levelUpHero(hero, havoc, false);
	ASSERT_EQ(hero->getSecSkillLevel(havoc), MasteryLevel::ADVANCED);

	const int controlledBlastRootSeed = findRootSeedForOffer(controlledBlastId, true);
	ASSERT_NE(controlledBlastRootSeed, 0);
	gameHandler.randomizer->setSeed(controlledBlastRootSeed);
	const auto secondLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(secondLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	auto secondQuery = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(secondQuery, nullptr);
	const auto controlledBlast = std::find_if(secondQuery->hlu.perks.begin(), secondQuery->hlu.perks.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == controlledBlastId;
	});
	ASSERT_NE(controlledBlast, secondQuery->hlu.perks.end());
	const auto controlledBlastChoice = static_cast<int>(secondQuery->hlu.skills.size()
		+ std::distance(secondQuery->hlu.perks.begin(), controlledBlast));
	ASSERT_TRUE(secondQuery->isValidReply(controlledBlastChoice));
	ASSERT_TRUE(gameHandler.queryReply(secondQuery->queryID, controlledBlastChoice, hero->getOwner()));

	EXPECT_EQ(hero->level, secondLevel + 1);
	EXPECT_TRUE(hero->getPerkState().hasSelection(havocMagicId, controlledBlastId));
	EXPECT_TRUE(hero->hasActivePerk(havocMagicId, controlledBlastId));
	const auto definition = newHorizonsHeroes::perkDefinition(hero->getPerkState().rules,
		havocMagicId, controlledBlastId);
	ASSERT_TRUE(definition.has_value());
	EXPECT_EQ(definition->effect["status"].String(), "active");

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getSecSkillLevel(havoc), MasteryLevel::ADVANCED);
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(havocMagicId, stormcallerId));
	EXPECT_TRUE(restoredHero->hasActivePerk(havocMagicId, stormcallerId));
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(havocMagicId, controlledBlastId));
	EXPECT_TRUE(restoredHero->hasActivePerk(havocMagicId, controlledBlastId));
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ScoutingImmediatelyRevealsExpandedSightAndSurvivesSaveLoad)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	const auto logistics = skill("new-horizons:logistics");
	gameHandler.changeSecSkill(hero, logistics, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	const int originalRadius = hero->getSightRadius();
	const auto expandedTile = hero->getSightCenter() + int3(originalRadius + 2, 0, 0);
	ASSERT_FALSE(gameState()->isVisibleFor(expandedTile, hero->getOwner()));
	const auto ranks = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
	bool selected = false;
	for(uint64_t seed = 0; seed < 1000 && !selected; ++seed)
	{
		const auto offer = hero->getPerkState().prepareOffer(ranks, seed, hero->getPerkOfferWeightContext());
		for(size_t index = 0; index < offer.size(); ++index)
		{
			if(offer[index].selection.perkId != "new-horizons:logistics.scouting")
				continue;
			gameHandler.levelUpHero(hero, offer, index, seed, false);
			selected = true;
			break;
		}
	}
	ASSERT_TRUE(selected);
	EXPECT_EQ(hero->getSightRadius(), originalRadius + 5);
	EXPECT_TRUE(gameState()->isVisibleFor(expandedTile, hero->getOwner()));
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	EXPECT_EQ(restored.getHero(hero->id)->getSightRadius(), originalRadius + 5);
	EXPECT_TRUE(restored.isVisibleFor(expandedTile, hero->getOwner()));
	gameHandler.changeSecSkill(hero, logistics, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSightRadius(), originalRadius);
	// Losing sight range does not erase terrain already explored.
	EXPECT_TRUE(gameState()->isVisibleFor(expandedTile, hero->getOwner()));
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ExternalRankAdvancementKeepsRankAndOffersMissingPerkTiersInOrder)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto sylvanLuck = skill(sylvanLuckId);
	ASSERT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	HeroLevelUp ordinaryRankOffer;
	ordinaryRankOffer.skills.push_back(sylvanLuck);
	CHeroLevelUpDialogQuery ordinaryQuery(&gameHandler, ordinaryRankOffer, hero);
	EXPECT_FALSE(ordinaryQuery.isValidReply(0));
	EXPECT_THROW(gameHandler.levelUpHero(hero, sylvanLuck, false), std::runtime_error);
	EXPECT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);

	gameHandler.changeSecSkill(hero, sylvanLuck, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::EXPERT);
	EXPECT_FALSE(hero->getPerkState().canAdvanceSkillNormally(sylvanLuckId, MasteryLevel::EXPERT));

	const auto rankLookup = [hero](const std::string & id) { return hero->getPerkSkillRank(id); };
	std::vector<newHorizonsHeroes::PerkOfferCandidate> offer;
	for(uint64_t seed = 0; seed < 1000; ++seed)
	{
		offer = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
		if(offerContains(offer, "new-horizons:sylvanLuck.elvenPrecision"))
			break;
	}
	ASSERT_TRUE(offerContains(offer, "new-horizons:sylvanLuck.elvenPrecision"));
	for(const auto & candidate : offer)
	{
		if(candidate.selection.skillId == sylvanLuckId)
		{
			EXPECT_EQ(candidate.requiredRank, MasteryLevel::BASIC);
		}
	}
}

TEST_F(NewHorizonsPerkVerticalSliceTest, BlockedSkillRanksNeverEnterTheWeightedOfferDraw)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto sylvanLuck = skill(sylvanLuckId);
	ASSERT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);
	ASSERT_FALSE(hero->getPerkState().canAdvanceSkillNormally(sylvanLuckId, MasteryLevel::BASIC));

	bool sawLegalOffer = false;
	for(int seed = 1; seed <= 100; ++seed)
	{
		GameRandomizer randomizer(*gameState());
		randomizer.setSeed(seed);
		const auto offers = randomizer.rollSecondarySkills(hero);
		EXPECT_EQ(std::find(offers.begin(), offers.end(), sylvanLuck), offers.end());
		sawLegalOffer = sawLegalOffer || !offers.empty();
	}
	EXPECT_TRUE(sawLegalOffer);
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ActiveBasicArcheryPerkUnlocksAdvancedRankAndAdvancedPerks)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	const auto archery = skill("new-horizons:archery");
	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameHandler.changeSecSkill(hero, archery, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	hero->applyPerkSelection({"new-horizons:archery", "new-horizons:archery.pointBlankShot"});
	ASSERT_TRUE(hero->hasActivePerk("new-horizons:archery", "new-horizons:archery.pointBlankShot"));
	EXPECT_TRUE(hero->getPerkState().canAdvanceSkillNormally("new-horizons:archery", MasteryLevel::BASIC));

	gameHandler.changeSecSkill(hero, archery, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	const auto rankLookup = [hero](const std::string & skillId) { return hero->getPerkSkillRank(skillId); };
	std::vector<newHorizonsHeroes::PerkOfferCandidate> offer;
	for(uint64_t seed = 0; seed < 1000; ++seed)
	{
		offer = hero->getPerkState().prepareOffer(rankLookup, seed, hero->getPerkOfferWeightContext());
		if(offerContains(offer, "new-horizons:archery.armorPiercingShot"))
			break;
	}
	EXPECT_TRUE(offerContains(offer, "new-horizons:archery.armorPiercingShot"));
	for(const auto & candidate : offer)
	{
		if(candidate.selection.skillId == "new-horizons:archery")
		{
			EXPECT_EQ(candidate.requiredRank, MasteryLevel::ADVANCED);
		}
	}
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ExperienceOfferChoiceActivatesEffectAndSurvivesSaveLoad)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	EXPECT_EQ(hero->getFactionID(), FactionID::RAMPART);

	const auto sylvanLuck = skill(sylvanLuckId);
	const auto sorceryMagic = skill(sorceryMagicId);
	ASSERT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);
	EXPECT_EQ(hero->getSecSkillLevel(SecondarySkill::WISDOM), MasteryLevel::NONE);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	gameHandler.changeSecSkill(hero, sorceryMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	// CGameHandler consumes one global value for the hero-specific skill RNG and
	// the next one for the server-authored offer seed. Find a reproducible seed
	// whose real offer includes the active Overcharger candidate.
	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};
	int rootSeed = 0;
	for(int candidateSeed = 1; candidateSeed < 10000; ++candidateSeed)
	{
		CRandomGenerator probe(candidateSeed);
		probe.nextInt();
		const auto offerSeed = static_cast<uint64_t>(static_cast<uint32_t>(probe.nextInt()));
		if(offerContains(hero->getPerkState().prepareOffer(rankLookup, offerSeed, hero->getPerkOfferWeightContext()), overchargerId))
		{
			rootSeed = candidateSeed;
			break;
		}
	}
	ASSERT_NE(rootSeed, 0);
	gameHandler.randomizer->setSeed(rootSeed);
	gameHandler.onAdvInterfaceReady(hero->getOwner());

	const auto previousLevel = hero->level;
	const auto nextLevelExperience = LIBRARY->heroh->reqExp(previousLevel + 1);
	gameHandler.giveExperience(hero, nextLevelExperience - hero->exp);

	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(query, nullptr);
	const auto perk = std::find_if(query->hlu.perks.begin(), query->hlu.perks.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == overchargerId;
	});
	ASSERT_NE(perk, query->hlu.perks.end());
	const auto perkChoice = static_cast<int>(query->hlu.skills.size()
		+ std::distance(query->hlu.perks.begin(), perk));
	ASSERT_TRUE(query->isValidReply(perkChoice));
	ASSERT_TRUE(gameHandler.queryReply(query->queryID, perkChoice, hero->getOwner()));

	EXPECT_EQ(hero->level, previousLevel + 1);
	EXPECT_TRUE(hero->getPerkState().hasSelection(sorceryMagicId, overchargerId));
	EXPECT_TRUE(hero->hasActivePerk(sorceryMagicId, overchargerId));
	const auto definition = newHorizonsHeroes::perkDefinition(hero->getPerkState().rules,
		sorceryMagicId, overchargerId);
	ASSERT_TRUE(definition.has_value());
	EXPECT_EQ(definition->effect["status"].String(), "active");

	const auto modifiers = newHorizonsMagic::magicArrowOverchargeModifiers(hero);
	EXPECT_EQ(modifiers.maximumBonus, 1);
	EXPECT_EQ(modifiers.damagePercentTenths, 175);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(gameState()->getMagicRules(),
		SpellID(SpellID::MAGIC_ARROW), 150, modifiers), 6);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getFactionID(), FactionID::RAMPART);
	EXPECT_EQ(restoredHero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);
	EXPECT_EQ(restoredHero->getSecSkillLevel(sorceryMagic), MasteryLevel::BASIC);
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(sorceryMagicId, overchargerId));
	EXPECT_TRUE(restoredHero->hasActivePerk(sorceryMagicId, overchargerId));

	const auto restoredModifiers = newHorizonsMagic::magicArrowOverchargeModifiers(restoredHero);
	EXPECT_EQ(restoredModifiers.maximumBonus, 1);
	EXPECT_EQ(restoredModifiers.damagePercentTenths, 175);
	EXPECT_EQ(newHorizonsMagic::magicArrowMaxOvercharge(restored.getMagicRules(),
		SpellID(SpellID::MAGIC_ARROW), 150, restoredModifiers), 6);
}

TEST_F(NewHorizonsPerkVerticalSliceTest, RampartFactionSkillProgressionAndPerkChoiceSurviveSaveLoad)
{
	startGame();
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->getFactionID(), FactionID::RAMPART);

	const auto sylvanLuck = skill(sylvanLuckId);
	ASSERT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);

	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());

	// Keep the level-up offer focused on the already-owned faction skill and on
	// the one active Sylvan Luck perk. This still goes through the normal
	// randomizer/query path; the other skills are only closed out so a test
	// cannot accidentally depend on a random unrelated skill or perk.
	for(int index = 0; index < LIBRARY->skillh->size(); ++index)
	{
		const SecondarySkill candidate(index);
		// Leave the two skills that can open their separate mastery dialog out of
		// this focused perk journey.
		if(candidate == SecondarySkill::ARTILLERY || candidate == SecondarySkill::LOGISTICS)
			continue;
		hero->setSecSkillLevel(candidate, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	}
	hero->setSecSkillLevel(sylvanLuck, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};
	int rootSeed = 0;
	for(int candidateSeed = 1; candidateSeed < 10000; ++candidateSeed)
	{
		CRandomGenerator probe(candidateSeed);
		probe.nextInt(); // hero-specific secondary/primary skill stream seed
		const auto offerSeed = static_cast<uint64_t>(static_cast<uint32_t>(probe.nextInt()));
		if(offerContains(hero->getPerkState().prepareOffer(rankLookup, offerSeed, hero->getPerkOfferWeightContext()),
			"new-horizons:sylvanLuck.elvenPrecision"))
		{
			rootSeed = candidateSeed;
			break;
		}
	}
	ASSERT_NE(rootSeed, 0);
	gameHandler.randomizer->setSeed(rootSeed);
	gameHandler.onAdvInterfaceReady(hero->getOwner());

	// The first real level-up presents the active faction perk. Select it via
	// CHeroLevelUpDialogQuery so validation, HeroPerkChosen replication, and the
	// query-resolution ordering are all exercised.
	const auto firstLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(firstLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	auto firstQuery = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(firstQuery, nullptr);
	const auto firstPerk = std::find_if(firstQuery->hlu.perks.begin(), firstQuery->hlu.perks.end(), [](const auto & candidate)
	{
		return candidate.selection.perkId == "new-horizons:sylvanLuck.elvenPrecision";
	});
	ASSERT_NE(firstPerk, firstQuery->hlu.perks.end());
	const auto firstPerkChoice = static_cast<int>(firstQuery->hlu.skills.size()
		+ std::distance(firstQuery->hlu.perks.begin(), firstPerk));
	ASSERT_TRUE(firstQuery->isValidReply(firstPerkChoice));
	ASSERT_TRUE(gameHandler.queryReply(firstQuery->queryID, firstPerkChoice, hero->getOwner()));

	EXPECT_EQ(hero->level, firstLevel + 1);
	EXPECT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::BASIC);
	EXPECT_TRUE(hero->hasActivePerk(sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));

	// On the following level-up the faction skill is offered as an upgrade even
	// though its legacy gain chance is zero. Selecting it proves progression is
	// tied to the owning faction rather than to a legacy class table entry.
	const auto secondLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(secondLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	const auto topAfterSecondLevel = gameHandler.queries->topQuery(hero->getOwner());
	ASSERT_NE(topAfterSecondLevel, nullptr) << "hero level=" << hero->level << " exp=" << hero->exp;
	auto secondQuery = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(topAfterSecondLevel);
	ASSERT_NE(secondQuery, nullptr);
	const auto factionSkillChoice = std::find(secondQuery->hlu.skills.begin(), secondQuery->hlu.skills.end(), sylvanLuck);
	ASSERT_NE(factionSkillChoice, secondQuery->hlu.skills.end());
	const auto factionSkillIndex = static_cast<int>(std::distance(secondQuery->hlu.skills.begin(), factionSkillChoice));
	ASSERT_TRUE(secondQuery->isValidReply(factionSkillIndex));
	ASSERT_TRUE(gameHandler.queryReply(secondQuery->queryID, factionSkillIndex, hero->getOwner()));

	EXPECT_EQ(hero->level, secondLevel + 1);
	EXPECT_EQ(hero->getSecSkillLevel(sylvanLuck), MasteryLevel::ADVANCED);
	EXPECT_TRUE(hero->hasActivePerk(sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getFactionID(), FactionID::RAMPART);
	EXPECT_EQ(restoredHero->getSecSkillLevel(sylvanLuck), MasteryLevel::ADVANCED);
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(
		sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));
	EXPECT_TRUE(restoredHero->hasActivePerk(
		sylvanLuckId, "new-horizons:sylvanLuck.elvenPrecision"));
}

TEST_F(NewHorizonsPerkVerticalSliceTest, ChristianCanChooseInspirationalLeaderAndKeepDisciplineMorale)
{
	startGame(christianHeroId);
	auto * hero = findHeroByOwner(PlayerColor(0));
	ASSERT_NE(hero, nullptr);
	ASSERT_EQ(hero->getFactionID(), FactionID::CASTLE);

	const auto discipline = skill(disciplineId);
	GameHandlerTestServer server(gameState());
	CGameHandler gameHandler(server, gameState());
	// Christian's authored start is War Machines, not Discipline. Assemble the
	// learned-Skill fixture through the simulation; the perk is still chosen
	// through the real ordinary level-up query below.
	ASSERT_TRUE(hero->canLearnSkill(discipline));
	gameHandler.changeSecSkill(hero, discipline, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(hero->getSecSkillLevel(discipline), MasteryLevel::BASIC);
	const auto moraleWithDiscipline = hero->moraleVal();
	EXPECT_GE(moraleWithDiscipline, 1);

	const auto rankLookup = [hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	};

	// Find a deterministic ordinary level-up seed whose server-authored
	// candidate list contains the one active Discipline perk.
	int rootSeed = 0;
	for(int candidateSeed = 1; candidateSeed < 10000; ++candidateSeed)
	{
		CRandomGenerator probe(candidateSeed);
		probe.nextInt();
		const auto offerSeed = static_cast<uint64_t>(static_cast<uint32_t>(probe.nextInt()));
		if(offerContains(hero->getPerkState().prepareOffer(rankLookup, offerSeed, hero->getPerkOfferWeightContext()),
			inspirationalLeaderId))
		{
			rootSeed = candidateSeed;
			break;
		}
	}
	ASSERT_NE(rootSeed, 0);
	gameHandler.randomizer->setSeed(rootSeed);
	gameHandler.onAdvInterfaceReady(hero->getOwner());

	const auto previousLevel = hero->level;
	hero->setExperience(LIBRARY->heroh->reqExp(previousLevel + 1), ChangeValueMode::ABSOLUTE);
	gameHandler.levelUpHero(hero);
	auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
		gameHandler.queries->topQuery(hero->getOwner()));
	ASSERT_NE(query, nullptr);
	const auto perk = std::find_if(query->hlu.perks.begin(), query->hlu.perks.end(),
		[](const auto & candidate)
	{
		return candidate.selection.perkId == inspirationalLeaderId;
	});
	ASSERT_NE(perk, query->hlu.perks.end());
	const auto perkChoice = static_cast<int>(query->hlu.skills.size()
		+ std::distance(query->hlu.perks.begin(), perk));
	ASSERT_TRUE(query->isValidReply(perkChoice));
	ASSERT_TRUE(gameHandler.queryReply(query->queryID, perkChoice, hero->getOwner()));

	EXPECT_EQ(hero->level, previousLevel + 1);
	EXPECT_TRUE(hero->getPerkState().hasSelection(disciplineId, inspirationalLeaderId));
	EXPECT_TRUE(hero->hasActivePerk(disciplineId, inspirationalLeaderId));
	EXPECT_EQ(hero->moraleVal(), moraleWithDiscipline);

	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	const auto * restoredHero = restored.getHero(hero->id);
	ASSERT_NE(restoredHero, nullptr);
	EXPECT_EQ(restoredHero->getSecSkillLevel(discipline), MasteryLevel::BASIC);
	EXPECT_TRUE(restoredHero->getPerkState().hasSelection(disciplineId, inspirationalLeaderId));
	EXPECT_TRUE(restoredHero->hasActivePerk(disciplineId, inspirationalLeaderId));
	EXPECT_EQ(restoredHero->moraleVal(), moraleWithDiscipline);
}
