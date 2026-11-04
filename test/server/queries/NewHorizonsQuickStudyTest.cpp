/*
 * NewHorizonsQuickStudyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include <gtest/gtest.h>

#include "../battles/FullGameSnapshotTypes.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/mapObjects/NewHorizonsLevelUpOffer.h"
#include "../../../lib/entities/hero/NewHorizonsLearning.h"
#include "../../../lib/entities/hero/SkillChoiceEligibility.h"
#include "../../../lib/entities/hero/NewHorizonsGenieSage.h"
#include "../../../server/queries/HeroLevelUpOffer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"

namespace
{
constexpr auto LEARNING_SKILL = "new-horizons:learning";
constexpr auto MENTOR = "new-horizons:learning.mentor";
constexpr auto QUICK_STUDY = "new-horizons:learning.connoisseur";

class OfferRecordingServer : public GameHandlerTestServer
{
public:
	using GameHandlerTestServer::GameHandlerTestServer;
	std::vector<HeroLevelUp> offers;
	std::vector<bool> acknowledgements;
	std::vector<std::string> events;
	void sendPack(CPackForClient & pack, GameConnectionID connection) override
	{
		if(const auto * ack = dynamic_cast<const PackageApplied *>(&pack))
		{
			acknowledgements.push_back(ack->result);
			events.push_back(ack->result ? "accepted" : "rejected");
		}
		GameHandlerTestServer::sendPack(pack, connection);
	}
	void applyPack(CPackForClient & pack) override
	{
		if(const auto * offer = dynamic_cast<const HeroLevelUp *>(&pack))
			{ offers.push_back(*offer); events.push_back("offer"); }
		if(dynamic_cast<const QueryResolved *>(&pack))
			events.push_back("resolved");
		GameHandlerTestServer::applyPack(pack);
	}
};

struct PrefixProbe
{
	using Version = ESerializationVersion;
	bool saving = true;
	size_t fields = 0;
	bool hasFeature(Version feature) const { return feature <= Version::NEW_HORIZONS_OPTIONAL_QUICK_STUDY; }
	template<class T> PrefixProbe & operator&(T &) { ++fields; return *this; }
};

struct ExpectedOffer
{
	std::vector<SecondarySkill> skills;
	std::vector<newHorizonsHeroes::PerkOfferCandidate> perks;
	int64_t perkOfferSeed = 0;
};

ExpectedOffer prepareExpectedOffer(GameRandomizer & randomizer, const CGHeroInstance * hero)
{
	ExpectedOffer result;
	result.skills = randomizer.rollSecondarySkills(hero);
	const auto & perkState = hero->getPerkState();
	if(!newHorizonsHeroes::usesPerkRules(perkState.rules))
		return result;

	std::erase_if(result.skills, [hero, &perkState](SecondarySkill skill)
	{
		const auto * definition = LIBRARY->skillh->getById(skill);
		return !definition || !perkState.canAdvanceSkillNormally(definition->getJsonKey(),
			hero->getSecSkillLevel(skill));
	});
	const size_t maxSkillChoices = static_cast<size_t>(perkState.rules["maxSkillChoices"].Integer());
	if(result.skills.size() > maxSkillChoices)
		result.skills.resize(maxSkillChoices);

	result.perkOfferSeed = static_cast<uint32_t>(randomizer.getDefault().nextInt());
	result.perks = perkState.prepareOffer([hero](const std::string & skillId)
	{
		return hero->getPerkSkillRank(skillId);
	}, static_cast<uint64_t>(result.perkOfferSeed), hero->getPerkOfferWeightContext());
	return result;
}

class NewHorizonsQuickStudyTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto perkRules = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
		// Historical controls capture absence even after installed defaults opt in.
		perkRules.Struct().erase("connoisseurPolicy");
		for(auto & perk : perkRules["skills"][LEARNING_SKILL]["perks"].Vector())
		{
			if(perk["id"].String() == QUICK_STUDY || perk["id"].String() == MENTOR)
				perk["effect"]["status"].String() = "active";
		}
		perkRules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perkRules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_MASTERIES, JsonNode());
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(0), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}});
		startWithMap(std::move(builder));

		hero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(hero, nullptr);
		server = std::make_unique<OfferRecordingServer>(gameState(), PlayerColor(0));
		gameHandler = std::make_unique<CGameHandler>(*server, gameState());
		gameHandler->randomizer->setSeed(1337);
	}

	void setLearningRank(int rank)
	{
		const int skillId = SecondarySkill::decode(LEARNING_SKILL);
		ASSERT_GE(skillId, 0);
		hero->setSecSkillLevel(SecondarySkill(skillId), rank, ChangeValueMode::ABSOLUTE);
	}

	void selectQuickStudy()
	{
		setLearningRank(MasteryLevel::BASIC);
		hero->applyPerkSelection({LEARNING_SKILL, MENTOR});
		setLearningRank(MasteryLevel::ADVANCED);
		hero->applyPerkSelection({LEARNING_SKILL, QUICK_STUDY});
	}

	void markQuickStudyPlanned(bool historicalSnapshot = false)
	{
		auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
		// This historical saved selection predates strict active-prefix validation.
		if(historicalSnapshot)
			state.rules.Struct().erase("perkProgressionPolicyVersion");
		for(auto & perk : state.rules["skills"][LEARNING_SKILL]["perks"].Vector())
			if(perk["id"].String() == QUICK_STUDY)
				perk["effect"]["status"].String() = "planned";
	}

	void expectLevelUp(int reachedLevel, bool expectReroll)
	{
		hero->level = reachedLevel - 1;
		hero->setExperience(LIBRARY->heroh->reqExp(reachedLevel), ChangeValueMode::ABSOLUTE);
		const auto owner = hero->getOwner();
		gameHandler->onAdvInterfaceReady(owner);

		CMemorySerializer randomizerSnapshot;
		randomizerSnapshot.oser & *gameHandler->randomizer;
		GameRandomizer expectedRandomizer(*gameState());
		randomizerSnapshot.iser & expectedRandomizer;
		const auto expectedPrimaryGains = expectedRandomizer.rollPrimarySkillsForLevelup(hero);
		auto expectedOffer = prepareExpectedOffer(expectedRandomizer, hero);
		(void)expectReroll; // Connoisseur never redraws automatically.

		gameHandler->levelUpHero(hero);
		const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(
			gameHandler->queries->topQuery(owner));
		ASSERT_NE(query, nullptr);
		EXPECT_EQ(query->hlu.primaryGains, expectedPrimaryGains);
		EXPECT_EQ(query->hlu.skills, expectedOffer.skills);
		EXPECT_EQ(query->hlu.perks, expectedOffer.perks);
		EXPECT_EQ(query->hlu.perkOfferSeed, expectedOffer.perkOfferSeed);
		EXPECT_EQ(hero->level, reachedLevel);

		// Re-exposure must not spend another draw. The same authoritative offer is
		// passed through the level-up callback used by both human UI and AI.
		query->onExposure(query);
		EXPECT_EQ(gameHandler->randomizer->rollSecondarySkills(hero),
			expectedRandomizer.rollSecondarySkills(hero));
		EXPECT_EQ(gameHandler->randomizer->getDefault().nextInt(),
			expectedRandomizer.getDefault().nextInt());

		ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, owner));
	}

	void enableOptionalPolicy(int = 1)
	{
		auto & rules = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).rules;
		rules["connoisseurPolicy"]["version"].Integer() = 1;
	}

	std::shared_ptr<CHeroLevelUpDialogQuery> openOptionalOffer(int level)
	{
		hero->level = level - 1;
		hero->setExperience(LIBRARY->heroh->reqExp(level), ChangeValueMode::ABSOLUTE);
		gameHandler->onAdvInterfaceReady(hero->getOwner());
		gameHandler->levelUpHero(hero);
		return std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(hero->getOwner()));
	}

	bool acquireProdigyFromCurrentOffer()
	{
		const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(hero->getOwner()));
		if(!query)
			return false;
		for(uint64_t seed = 0; seed < 10000; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer([this](const std::string & skill)
				{ return hero->getPerkSkillRank(skill); }, seed, hero->getPerkOfferWeightContext());
			for(size_t i = 0; i < offer.size(); ++i)
				if(offer[i].selection.perkId == "new-horizons:learning.prodigy")
				{
					query->hlu.perks = offer;
					query->hlu.perkOfferSeed = seed;
					return gameHandler->queryReply(query->queryID, static_cast<int32_t>(query->hlu.skills.size() + i), hero->getOwner());
				}
		}
		return false;
	}

	int finishNormalChoices()
	{
		int count = 0;
		while(const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(hero->getOwner())))
		{
			if(++count > 12 || !gameHandler->queryReply(query->queryID, 0, hero->getOwner()))
				return -1;
		}
		return count;
	}

	std::unique_ptr<GameRandomizer> cloneRandomizer()
	{
		CMemorySerializer snapshot;
		snapshot.oser & *gameHandler->randomizer;
		auto result = std::make_unique<GameRandomizer>(*gameState());
		snapshot.iser & *result;
		return result;
	}

	std::vector<std::byte> randomizerBytes()
	{
		CMemorySerializer snapshot;
		snapshot.oser & *gameHandler->randomizer;
		return snapshot.extractBuffer();
	}

	CGHeroInstance * hero = nullptr;
	std::unique_ptr<OfferRecordingServer> server;
	std::unique_ptr<CGameHandler> gameHandler;
};
}

TEST_F(NewHorizonsQuickStudyTest, NeverRedrawsInitialOfferAutomatically)
{
	startGame();
	setLearningRank(MasteryLevel::ADVANCED);
	selectQuickStudy();
	ASSERT_TRUE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));

	expectLevelUp(5, true);
	expectLevelUp(10, true);
	expectLevelUp(13, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollWithoutThePerkSelected)
{
	startGame();
	setLearningRank(MasteryLevel::ADVANCED);
	EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));
	expectLevelUp(5, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollBelowThePerkRequiredRank)
{
	startGame();
	selectQuickStudy();
	setLearningRank(MasteryLevel::BASIC);
	EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));
	expectLevelUp(5, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollWhenTheSavedPerkDefinitionIsPlanned)
{
	startGame();
	selectQuickStudy();
	markQuickStudyPlanned(true);
	EXPECT_FALSE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));
	expectLevelUp(5, false);
}

TEST_F(NewHorizonsQuickStudyTest, DoesNotRerollAtANonFifthLevelWithThePerkActive)
{
	startGame();
	setLearningRank(MasteryLevel::ADVANCED);
	selectQuickStudy();
	ASSERT_TRUE(hero->hasActivePerk(LEARNING_SKILL, QUICK_STUDY));

	expectLevelUp(4, false);
}

TEST_F(NewHorizonsQuickStudyTest, OptionalPolicyShowsInitialOfferAndDeclineConsumesNoRerollDraw)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	hero->level = 2;
	auto expected = cloneRandomizer();
	expected->rollPrimarySkillsForLevelup(hero);
	const auto initial = prepareExpectedOffer(*expected, hero);
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	EXPECT_TRUE(query->hlu.optionalRerollAvailable);
	EXPECT_EQ(query->hlu.skills, initial.skills);
	EXPECT_EQ(query->hlu.perks, initial.perks);
	EXPECT_EQ(query->hlu.perkOfferSeed, initial.perkOfferSeed);
	const auto beforeExposure = randomizerBytes();
	query->onExposure(query);
	EXPECT_EQ(randomizerBytes(), beforeExposure);
	CMemorySerializer expectedSnapshot;
	expectedSnapshot.oser & *expected;
	EXPECT_EQ(randomizerBytes(), expectedSnapshot.extractBuffer());
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	EXPECT_EQ(hero->level, 3);
}

TEST_F(NewHorizonsQuickStudyTest, OptionalRerollReplacesOnlySelectedSkillOnceWithoutAnotherLevel)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(6);
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(query->hlu.optionalRerollAvailable);
	const auto oldID = query->queryID;
	const auto oldPrimary = query->hlu.primaryGains;
	const auto selections = hero->getPerkState().selected;
	auto expected = cloneRandomizer();
	const auto held = query->hlu;
	const auto candidates = query->rerollCandidates();
	ASSERT_FALSE(candidates.empty());
	const auto expectedCard = expected->rollSecondarySkillForLevelup(hero, candidates);
	ASSERT_TRUE(gameHandler->queryReply(oldID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_EQ(gameHandler->queries->topQuery(hero->getOwner()), query);
	EXPECT_NE(query->queryID, oldID);
	EXPECT_EQ(query->hlu.replacesQueryID, QueryID::NONE);
	ASSERT_GE(server->offers.size(), 2u);
	EXPECT_EQ(server->offers.back().replacesQueryID, oldID);
	EXPECT_FALSE(query->hlu.optionalRerollAvailable);
	ASSERT_EQ(query->hlu.skills.size(), held.skills.size());
	EXPECT_NE(query->hlu.skills.front(), held.skills.front());
	EXPECT_EQ(query->hlu.skills.front(), expectedCard);
	for(size_t i = 1; i < held.skills.size(); ++i)
		EXPECT_EQ(query->hlu.skills[i], held.skills[i]);
	EXPECT_EQ(query->hlu.perks, held.perks);
	EXPECT_EQ(query->hlu.perkOfferSeed, held.perkOfferSeed);
	EXPECT_EQ(query->hlu.primaryGains, oldPrimary);
	EXPECT_EQ(hero->level, 6);
	EXPECT_EQ(hero->getPerkState().selected, selections);
	const auto afterReroll = randomizerBytes();
	PrefixProbe pendingSave;
	EXPECT_THROW(gameHandler->serialize(pendingSave), std::runtime_error);
	EXPECT_EQ(pendingSave.fields, 0u);
	EXPECT_EQ(randomizerBytes(), afterReroll);
	EXPECT_FALSE(query->hlu.optionalRerollAvailable);
	EXPECT_FALSE(gameHandler->queryReply(oldID, 0, hero->getOwner()));
	EXPECT_FALSE(gameHandler->queryReply(oldID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	query->onExposure(query);
	EXPECT_EQ(randomizerBytes(), afterReroll);
	CMemorySerializer expectedSnapshot;
	expectedSnapshot.oser & *expected;
	EXPECT_EQ(randomizerBytes(), expectedSnapshot.extractBuffer());
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	EXPECT_EQ(hero->level, 6);
}

TEST_F(NewHorizonsQuickStudyTest, OptionalCadenceAndEligibilityAreAuthoritative)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	for(int level : {3, 4, 5, 6, 9})
	{
		const auto query = openOptionalOffer(level);
		ASSERT_NE(query, nullptr);
		EXPECT_TRUE(query->hlu.optionalRerollAvailable);
		ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	}
}

TEST_F(NewHorizonsQuickStudyTest, RerollRejectsLostRankPlannedPerkAndWrongPlayerWithoutDraws)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	const auto before = randomizerBytes();
	EXPECT_FALSE(query->requestReroll(PlayerColor(1)));
	setLearningRank(MasteryLevel::BASIC);
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	setLearningRank(MasteryLevel::ADVANCED);
	markQuickStudyPlanned();
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_EQ(randomizerBytes(), before);
}

TEST_F(NewHorizonsQuickStudyTest, RerollRequiresTopQueryAndRejectsStaleOffer)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	const auto before = randomizerBytes();
	auto blocking = std::make_shared<CGenericQuery>(gameHandler.get(), hero->getOwner(), [](auto) {});
	gameHandler->queries->addQuery(blocking);
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	gameHandler->queries->popQuery(blocking);
	query->hlu.perkOfferSeed = -1;
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_EQ(randomizerBytes(), before);
}

TEST_F(NewHorizonsQuickStudyTest, PendingOptionalOfferRejectsSaveBeforeAnySerializedPrefix)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	PrefixProbe probe;
	EXPECT_THROW(gameHandler->serialize(probe), std::runtime_error);
	EXPECT_EQ(probe.fields, 0u);
	EXPECT_THROW(gameHandler->save("Saves/PendingOptionalQuickStudyMustReject", hero->getOwner()), std::runtime_error);
	EXPECT_EQ(gameHandler->queries->topQuery(hero->getOwner()), query);
}

TEST_F(NewHorizonsQuickStudyTest, CompletedChoiceRoundTripsCapturedPolicyAtCurrentFormat)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	EXPECT_NO_THROW(newHorizonsLevelUp::validateNoPendingOptionalOffer(*gameHandler));
	CMemorySerializer save;
	ASSERT_EQ(save.oser.version, ESerializationVersion::CURRENT);
	ASSERT_NO_THROW(save.oser & *gameState());
	CGameState restored;
	restored.preInit(LIBRARY);
	save.iser.version = ESerializationVersion::CURRENT;
	save.iser.loadingGamestate = true;
	save.iser.cb = &restored;
	ASSERT_NO_THROW(save.iser & restored);
	const auto * loaded = restored.getHero(hero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->level, 3);
	EXPECT_EQ(loaded->exp, hero->exp);
	EXPECT_EQ(loaded->getPerkState().selected, hero->getPerkState().selected);
	EXPECT_EQ(loaded->getPerkState().rules, hero->getPerkState().rules);
	EXPECT_EQ(newHorizonsHeroes::quickStudyRerollInterval(loaded->getPerkState().rules), 1);
}

TEST_F(NewHorizonsQuickStudyTest, CapturedPolicyIsConfigurableAndRejectsMalformedPresence)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy(4);
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	EXPECT_TRUE(query->hlu.optionalRerollAvailable);
	auto rules = hero->getPerkState().rules;
	EXPECT_THROW(newHorizonsHeroes::validateWeightedPerkSerialization(rules, true, true, true, false, true), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsHeroes::validateWeightedPerkSerialization(rules, true, true, true, true, true));
	rules["connoisseurPolicy"]["interval"].Integer() = 3;
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error);
	rules["connoisseurPolicy"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error);
}

TEST_F(NewHorizonsQuickStudyTest, ReplacementEnvelopeRoundTripsAndRejectsOldWireBeforePrefix)
{
	HeroLevelUp offer;
	offer.reachedLevel = 6;
	offer.queryID = QueryID(9);
	offer.replacesQueryID = QueryID(8);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	EXPECT_THROW(offer.serialize(old.oser), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY;
	current.iser.version = ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY;
	offer.serialize(current.oser);
	HeroLevelUp restored;
	restored.serialize(current.iser);
	EXPECT_EQ(restored.reachedLevel, 6);
	EXPECT_EQ(restored.queryID, QueryID(9));
	EXPECT_EQ(restored.replacesQueryID, QueryID(8));
	EXPECT_FALSE(restored.optionalRerollAvailable);
	CMemorySerializer defaultWire;
	ASSERT_EQ(defaultWire.oser.version, ESerializationVersion::CURRENT);
	ASSERT_GE(defaultWire.oser.version, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	ASSERT_NO_THROW(offer.serialize(defaultWire.oser));
	HeroLevelUp defaultRestored;
	ASSERT_NO_THROW(defaultRestored.serialize(defaultWire.iser));
	EXPECT_EQ(defaultRestored.reachedLevel, offer.reachedLevel);
	EXPECT_EQ(defaultRestored.queryID, offer.queryID);
	EXPECT_EQ(defaultRestored.replacesQueryID, offer.replacesQueryID);
	EXPECT_EQ(defaultRestored.optionalRerollAvailable, offer.optionalRerollAvailable);
}

TEST_F(NewHorizonsQuickStudyTest, ExistingReplyCommandAcknowledgesRerollAfterOfferWithoutResolving)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	const auto oldID = query->queryID;
	server->events.clear();
	QueryReply request(oldID, CHeroLevelUpDialogQuery::REROLL_REPLY);
	request.player = hero->getOwner();
	request.requestID = 17;
	gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
	ASSERT_EQ(server->acknowledgements.size(), 1u);
	EXPECT_TRUE(server->acknowledgements.back());
	EXPECT_EQ(server->events, (std::vector<std::string>{"offer", "accepted"}));
	EXPECT_EQ(gameHandler->queries->topQuery(hero->getOwner()), query);
	EXPECT_EQ(hero->level, 3);
	server->events.clear();
	request.requestID = 18;
	gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
	EXPECT_FALSE(server->acknowledgements.back());
	EXPECT_EQ(server->events, (std::vector<std::string>{"rejected"}));
}

TEST_F(NewHorizonsQuickStudyTest, LegacyCapturedAbsenceRoundTripsWithoutOptionalMigration)
{
	startGame();
	selectQuickStudy();
	CMemorySerializer save;
	auto original = hero->getPerkState();
	ASSERT_NO_THROW(save.oser & original);
	newHorizonsHeroes::PerkState restored;
	ASSERT_NO_THROW(save.iser & restored);
	EXPECT_FALSE(newHorizonsHeroes::quickStudyRerollInterval(restored.rules));
	const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()) = restored;
	expectLevelUp(5, true);
}

TEST_F(NewHorizonsQuickStudyTest, DirectHeroSerializationUsesCurrentPolicyAndRetainsIdentityBooksAndPerks)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto random = randomizerBytes();
	CMemorySerializer current;
	ASSERT_EQ(current.oser.version, ESerializationVersion::CURRENT);
	ASSERT_NO_THROW(current.oser & *hero);
	const auto payload = current.extractBuffer();
	ASSERT_FALSE(payload.empty());
	CMemorySerializer reader(payload);
	reader.iser.version = ESerializationVersion::CURRENT;
	reader.iser.cb = gameState().get();
	CGHeroInstance restored(gameState().get());
	ASSERT_NO_THROW(reader.iser & restored);
	EXPECT_EQ(restored.id, hero->id);
	EXPECT_EQ(restored.getHeroTypeID(), hero->getHeroTypeID());
	EXPECT_EQ(restored.getSpellsInSpellbook(), hero->getSpellsInSpellbook());
	EXPECT_EQ(restored.level, hero->level);
	EXPECT_EQ(restored.exp, hero->exp);
	EXPECT_EQ(restored.getPerkState().selected, hero->getPerkState().selected);
	EXPECT_EQ(restored.getPerkState().rules, hero->getPerkState().rules);
	// Current malformed policy still rejects before any Hero bytes are written.
	auto & rules = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).rules;
	const auto captured = rules;
	rules["connoisseurPolicy"]["interval"].Integer() = 3;
	CMemorySerializer invalid;
	EXPECT_THROW(invalid.oser & *hero, std::runtime_error);
	EXPECT_TRUE(invalid.extractBuffer().empty());
	rules = captured;
	EXPECT_EQ(randomizerBytes(), random);
}

TEST_F(NewHorizonsQuickStudyTest, RejectedRerollConfirmsHeldOfferBeforeAckWithoutGrowthOrDraws)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	query->hlu.optionalRerollAvailable = false;
	const auto held = query->hlu;
	const auto idCounter = gameHandler->QID;
	query->hlu.optionalRerollAvailable = false;
	const auto before = randomizerBytes();
	CMemorySerializer globalBefore;
	CRandomGenerator::getDefault().serialize(globalBefore.oser);
	server->events.clear();
	QueryReply request(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY);
	request.player = hero->getOwner();
	request.requestID = 21;
	gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request);
	EXPECT_EQ(server->events, (std::vector<std::string>{"offer", "rejected"}));
	const auto confirmed = server->offers.back();
	EXPECT_EQ(confirmed.queryID, held.queryID);
	EXPECT_EQ(confirmed.replacesQueryID, held.queryID);
	EXPECT_FALSE(confirmed.optionalRerollAvailable);
	EXPECT_EQ(confirmed.skills, held.skills);
	EXPECT_EQ(confirmed.perks, held.perks);
	EXPECT_EQ(confirmed.perkOfferSeed, held.perkOfferSeed);
	EXPECT_EQ(confirmed.primaryGains, held.primaryGains);
	EXPECT_EQ(query->hlu.replacesQueryID, QueryID::NONE);
	EXPECT_EQ(query->hlu.optionalRerollAvailable, held.optionalRerollAvailable);
	EXPECT_EQ(gameHandler->QID, idCounter);
	EXPECT_EQ(hero->level, 4);
	EXPECT_EQ(randomizerBytes(), before);
	EXPECT_NO_THROW(newHorizonsHeroes::applyLevelUpOffer(hero, confirmed));
	EXPECT_NO_THROW(newHorizonsHeroes::applyLevelUpOffer(hero, confirmed));
	EXPECT_EQ(hero->level, 4);
	EXPECT_EQ(randomizerBytes(), before);
	CMemorySerializer globalAfter;
	CRandomGenerator::getDefault().serialize(globalAfter.oser);
	EXPECT_EQ(globalAfter.extractBuffer(), globalBefore.extractBuffer());
	server->events.clear();
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	EXPECT_EQ(server->events, (std::vector<std::string>{"resolved"}));
}

TEST_F(NewHorizonsQuickStudyTest, RerollConfirmationRequiresCurrentOwnerAndQuery)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	query->hlu.optionalRerollAvailable = false;
	const auto before = randomizerBytes();
	const auto count = server->offers.size();
	EXPECT_FALSE(query->requestReroll(PlayerColor(1)));
	EXPECT_FALSE(gameHandler->queryReply(QueryID(query->queryID.getNum() + 1), CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	auto blocking = std::make_shared<CGenericQuery>(gameHandler.get(), hero->getOwner(), [](auto) {});
	gameHandler->queries->addQuery(blocking);
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	gameHandler->queries->popQuery(blocking);
	EXPECT_EQ(server->offers.size(), count);
	const auto idCounter = gameHandler->QID;
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_EQ(server->offers.back().queryID, query->queryID);
	EXPECT_EQ(server->offers.back().replacesQueryID, query->queryID);
	EXPECT_FALSE(server->offers.back().optionalRerollAvailable);
	EXPECT_EQ(query->hlu.replacesQueryID, QueryID::NONE);
	EXPECT_EQ(gameHandler->QID, idCounter);
	EXPECT_EQ(randomizerBytes(), before);
	query->hlu.perkOfferSeed = -1;
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_EQ(server->offers.size(), count + 1);
	EXPECT_EQ(randomizerBytes(), before);
}

TEST_F(NewHorizonsQuickStudyTest, SameIdConfirmationRejectsWrongHeroOwnerAndLevelWithoutApplyingGains)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	const auto before = randomizerBytes();
	auto confirmation = query->hlu;
	confirmation.replacesQueryID = confirmation.queryID;
	ASSERT_TRUE(confirmation.optionalRerollAvailable);
	CMemorySerializer wire;
	wire.oser & confirmation;
	HeroLevelUp decoded;
	wire.iser & decoded;
	EXPECT_TRUE(decoded.optionalRerollAvailable);
	EXPECT_EQ(decoded.queryID, decoded.replacesQueryID);
	EXPECT_NO_THROW(newHorizonsHeroes::applyLevelUpOffer(hero, decoded));
	EXPECT_TRUE(query->hlu.optionalRerollAvailable);
	confirmation.player = PlayerColor(1);
	EXPECT_THROW(newHorizonsHeroes::applyLevelUpOffer(hero, confirmation), std::runtime_error);
	confirmation.player = hero->getOwner();
	confirmation.heroId = ObjectInstanceID::NONE;
	EXPECT_THROW(newHorizonsHeroes::applyLevelUpOffer(hero, confirmation), std::runtime_error);
	confirmation.heroId = hero->id;
	++confirmation.reachedLevel;
	EXPECT_THROW(newHorizonsHeroes::applyLevelUpOffer(hero, confirmation), std::runtime_error);
	EXPECT_EQ(hero->level, 3);
	EXPECT_EQ(randomizerBytes(), before);
	const auto originalID = query->queryID;
	ASSERT_TRUE(gameHandler->queryReply(originalID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_NE(query->queryID, originalID);
	EXPECT_FALSE(query->hlu.optionalRerollAvailable);
	EXPECT_EQ(hero->level, 3);
}

TEST_F(NewHorizonsQuickStudyTest, RejectedRerollDoesNotPublishConfirmationForAnInvalidCurrentRegistry)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	auto & rules = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).rules;
	rules["schemaVersion"].Integer() = 99;
	const auto before = randomizerBytes();
	const auto offers = server->offers.size();
	server->events.clear();
	QueryReply request(query->queryID, CHeroLevelUpDialogQuery::REROLL_REPLY);
	request.player = hero->getOwner();
	request.requestID = 22;
	EXPECT_NO_THROW(gameHandler->handleReceivedPack(GameConnectionID::FIRST_CONNECTION, request));
	EXPECT_EQ(server->events, (std::vector<std::string>{"rejected"}));
	EXPECT_EQ(server->offers.size(), offers);
	EXPECT_EQ(gameHandler->queries->topQuery(hero->getOwner()), query);
	EXPECT_EQ(hero->level, 4);
	EXPECT_EQ(randomizerBytes(), before);
}

TEST_F(NewHorizonsQuickStudyTest, ConnoisseurRejectsInvalidIndexAndNoAlternativeWithoutDrawOrUse)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	const auto before = randomizerBytes();
	const auto original = query->hlu;
	EXPECT_FALSE(query->requestReroll(hero->getOwner(), original.skills.size()));
	EXPECT_EQ(randomizerBytes(), before);
	EXPECT_TRUE(query->hlu.optionalRerollAvailable);
	auto & weights = const_cast<JsonNode &>(hero->getPrimaryGrowthRules())["skillOfferWeights"];
	for(auto & [name, value] : weights.Struct())
		if(std::none_of(original.skills.begin(), original.skills.end(), [&name](SecondarySkill skill)
			{ return name == SecondarySkill::encode(skill.getNum()); }))
			value.Integer() = 0;
	query->hlu.perks = hero->getPerkState().prepareOffer([this](const std::string & skill)
		{ return hero->getPerkSkillRank(skill); }, query->hlu.perkOfferSeed, hero->getPerkOfferWeightContext());
	const auto heldPerks = query->hlu.perks;
	EXPECT_TRUE(query->rerollCandidates().empty());
	EXPECT_FALSE(query->requestReroll(hero->getOwner(), 0));
	EXPECT_EQ(randomizerBytes(), before);
	EXPECT_EQ(query->hlu.skills, original.skills);
	EXPECT_EQ(query->hlu.perks, heldPerks);
	EXPECT_TRUE(query->hlu.optionalRerollAvailable);
}

TEST_F(NewHorizonsQuickStudyTest, ConnoisseurCanReplaceSecondCardWithoutChangingFirstOrPerks)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	ASSERT_GE(query->hlu.skills.size(), 2u);
	const auto held = query->hlu;
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::rerollReply(1), hero->getOwner()));
	EXPECT_EQ(query->hlu.skills[0], held.skills[0]);
	EXPECT_NE(query->hlu.skills[1], held.skills[1]);
	EXPECT_NE(query->hlu.skills[1], held.skills[0]);
	EXPECT_EQ(query->hlu.perks, held.perks);
	EXPECT_EQ(query->hlu.perkOfferSeed, held.perkOfferSeed);
}

TEST_F(NewHorizonsQuickStudyTest, CurrentLearningRewardsUseEnemyTotalExperienceAdditiveExperienceAndPermanentPrimaries)
{
	startGame();
	selectQuickStudy();
	setLearningRank(MasteryLevel::EXPERT);
	hero->applyPerkSelection({LEARNING_SKILL, "new-horizons:learning.philosopher"});
	hero->level = 1;
	hero->setExperience(600, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(hero->calculateXp(1000), 1550);
	EXPECT_EQ(hero->calculateXp(1000, 25), 1800);
	// Independent helper checks use isolated selected-perk projections below.
	auto & state = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState());
	state.selected.push_back({LEARNING_SKILL, "new-horizons:learning.duelLessons"});
	state.selected.push_back({LEARNING_SKILL, "new-horizons:learning.seasonedAdventurer"});
	EXPECT_EQ(newHorizonsLearning::duelLessonsExperience(hero, 20000), 2000);
	EXPECT_EQ(newHorizonsLearning::duelLessonsExperience(hero, 0), 0);
	EXPECT_EQ(newHorizonsLearning::duelLessonsExperience(hero, 20009), 2000);
	EXPECT_EQ(newHorizonsLearning::duelLessonsExperience(hero, -1), 0);
	EXPECT_EQ(newHorizonsLearning::combatExperience(hero, 1000, 20000), 3550);
	EXPECT_EQ(newHorizonsLearning::combatExperience(hero, std::numeric_limits<int64_t>::max(), 20000), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(newHorizonsLearning::trainingPrimaryReward(hero, Obj::LIBRARY_OF_ENLIGHTENMENT, 4), 5);
	EXPECT_EQ(newHorizonsLearning::trainingPrimaryReward(hero, Obj::GARDEN_OF_REVELATION, 1), 1);
	EXPECT_EQ(newHorizonsLearning::trainingPrimaryReward(hero, Obj::SCHOOL_OF_WAR, -4), -4);
	EXPECT_EQ(newHorizonsLearning::trainingPrimaryReward(hero, Obj::SWAN_POND, 4), 4);
	EXPECT_EQ(newHorizonsLearning::trainingPrimaryReward(hero, std::nullopt, 4), 4);
}

TEST_F(NewHorizonsQuickStudyTest, ProdigyDefersUntilCurrentChoiceThenGrantsExactlyThreeNormalLevelsPreservingRawProgress)
{
	startGame();
	hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:thane")));
	selectQuickStudy();
	enableOptionalPolicy();
	const_cast<JsonNode &>(hero->getPrimaryGrowthRules())["extraGrowth"].Vector().clear();
	setLearningRank(MasteryLevel::EXPERT);
	const auto query = openOptionalOffer(6);
	ASSERT_NE(query, nullptr);
	hero->setExperience(LIBRARY->heroh->reqExp(6) + 123, ChangeValueMode::ABSOLUTE);
	const auto before = hero->getPrimaryGrowthView()->base;
	const auto growth = hero->getPrimaryGrowthView()->profile.growth;
	ASSERT_TRUE(newHorizonsHeroes::hasSageOfTheAges(hero));
	ASSERT_TRUE(acquireProdigyFromCurrentOffer());
	EXPECT_EQ(hero->level, 7); // First new normal choice is already waiting.
	EXPECT_EQ(finishNormalChoices(), 3);
	EXPECT_EQ(hero->level, 9);
	EXPECT_EQ(hero->exp, LIBRARY->heroh->reqExp(9) + 123);
	EXPECT_TRUE(gameHandler->pendingProdigyLevels.empty());
	for(size_t i = 0; i < before.size(); ++i)
		EXPECT_EQ(hero->getPrimaryGrowthView()->base[i] - before[i], 3 * (growth[i] + (i >= 2 ? 1 : 0)));
	const auto experience = hero->exp;
	gameHandler->expGiven(hero);
	EXPECT_EQ(hero->level, 9);
	EXPECT_EQ(hero->exp, experience);
	EXPECT_FALSE(gameHandler->queries->topQuery(hero->getOwner()));
	CMemorySerializer save;
	ASSERT_NO_THROW(save.oser & *gameState());
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	save.iser.loadingGamestate = true;
	save.iser.cb = restored.get();
	ASSERT_NO_THROW(save.iser & *restored);
	const auto * loaded = restored->getHero(hero->id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->level, 9);
	EXPECT_EQ(loaded->exp, experience);
	EXPECT_TRUE(loaded->hasActivePerk(LEARNING_SKILL, "new-horizons:learning.prodigy"));
	GameHandlerTestServer loadedServer(restored, hero->getOwner());
	CGameHandler loadedHandler(loadedServer, restored);
	loadedHandler.expGiven(loaded);
	EXPECT_EQ(loaded->level, 9);
	EXPECT_FALSE(loadedHandler.queries->topQuery(loaded->getOwner()));
}

TEST_F(NewHorizonsQuickStudyTest, ProdigyDrainsAlreadyEarnedNaturalLevelsBeforeThreeBonusLevels)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	setLearningRank(MasteryLevel::EXPERT);
	const auto query = openOptionalOffer(6);
	ASSERT_NE(query, nullptr);
	hero->setExperience(LIBRARY->heroh->reqExp(8) + 75, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(acquireProdigyFromCurrentOffer());
	EXPECT_EQ(finishNormalChoices(), 5); // Two already earned, then exactly three Prodigy levels.
	EXPECT_EQ(hero->level, 11);
	EXPECT_EQ(hero->exp, LIBRARY->heroh->reqExp(11) + 75);
	EXPECT_TRUE(gameHandler->pendingProdigyLevels.empty());
}

TEST_F(NewHorizonsQuickStudyTest, ProdigyHonorsMapCapAndThaneReceivesEveryNormalSpecialtyGrowthEvent)
{
	startGame();
	hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:thane")));
	selectQuickStudy();
	enableOptionalPolicy();
	setLearningRank(MasteryLevel::EXPERT);
	const auto query = openOptionalOffer(6);
	ASSERT_NE(query, nullptr);
	gameState()->getMap().levelLimit = 8;
	const auto power = hero->getBasePrimarySkillValue(PrimarySkill::SPELL_POWER);
	const auto knowledge = hero->getBasePrimarySkillValue(PrimarySkill::KNOWLEDGE);
	ASSERT_TRUE(acquireProdigyFromCurrentOffer());
	EXPECT_EQ(finishNormalChoices(), 2);
	EXPECT_EQ(hero->level, 8);
	EXPECT_GE(hero->getBasePrimarySkillValue(PrimarySkill::SPELL_POWER) - power, 2);
	EXPECT_GE(hero->getBasePrimarySkillValue(PrimarySkill::KNOWLEDGE) - knowledge, 2);
	EXPECT_TRUE(gameHandler->pendingProdigyLevels.empty());
}

TEST_F(NewHorizonsQuickStudyTest, ConnoisseurPublishesNoAlternativeAsUnavailableWithoutDrawsOrQueryMutation)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	hero->level = 3;
	hero->setExperience(LIBRARY->heroh->reqExp(4), ChangeValueMode::ABSOLUTE);
	gameHandler->levelUpHero(hero); // Wait for the ordinary client-ready prompt boundary.
	const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(hero->getOwner()));
	ASSERT_NE(query, nullptr);
	ASSERT_FALSE(query->prompted);
	ASSERT_FALSE(query->hlu.skills.empty());
	const auto skills = query->hlu.skills;
	auto & weights = const_cast<JsonNode &>(hero->getPrimaryGrowthRules())["skillOfferWeights"];
	for(auto & [name, value] : weights.Struct())
		if(std::none_of(skills.begin(), skills.end(), [&name](SecondarySkill skill)
			{ return name == SecondarySkill::encode(skill.getNum()); }))
			value.Integer() = 0;
	ASSERT_TRUE(query->rerollCandidates().empty());
	const auto beforePrompt = randomizerBytes();
	const auto selections = hero->getPerkState().selected;
	gameHandler->onAdvInterfaceReady(hero->getOwner());
	ASSERT_TRUE(query->prompted);
	ASSERT_FALSE(server->offers.empty());
	EXPECT_FALSE(server->offers.back().optionalRerollAvailable);
	EXPECT_FALSE(query->hlu.optionalRerollAvailable);
	EXPECT_EQ(randomizerBytes(), beforePrompt);
	EXPECT_EQ(query->hlu.skills, skills);
	EXPECT_EQ(hero->level, 4);
	const auto idCounter = gameHandler->QID;
	CMemorySerializer held;
	held.oser & query->hlu;
	const auto heldBytes = held.extractBuffer();
	for(size_t index = 0; index < skills.size(); ++index)
	{
		EXPECT_FALSE(gameHandler->queryReply(query->queryID, CHeroLevelUpDialogQuery::rerollReply(index), hero->getOwner()));
		EXPECT_FALSE(server->offers.back().optionalRerollAvailable);
		EXPECT_EQ(server->offers.back().queryID, query->queryID);
		EXPECT_EQ(server->offers.back().replacesQueryID, query->queryID);
		EXPECT_EQ(gameHandler->queries->topQuery(hero->getOwner()), query);
		EXPECT_EQ(gameHandler->QID, idCounter);
		EXPECT_EQ(randomizerBytes(), beforePrompt);
		EXPECT_EQ(hero->getPerkState().selected, selections);
		EXPECT_EQ(hero->level, 4);
		CMemorySerializer unchanged;
		unchanged.oser & query->hlu;
		EXPECT_EQ(unchanged.extractBuffer(), heldBytes);
	}
}

TEST_F(NewHorizonsQuickStudyTest, SharedConnoisseurPoolExcludesBothCardsAndConsumesNoRng)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	ASSERT_EQ(query->hlu.skills.size(), 2u);
	const auto before = randomizerBytes();
	const auto pool = newHorizonsHeroes::connoisseurSkillCandidates(*gameState(), *hero, query->hlu.skills);
	EXPECT_EQ(pool, query->rerollCandidates());
	for(const auto skill : query->hlu.skills)
		EXPECT_FALSE(pool.contains(skill));
	auto reversed = query->hlu.skills;
	std::reverse(reversed.begin(), reversed.end());
	EXPECT_EQ(pool, newHorizonsHeroes::connoisseurSkillCandidates(*gameState(), *hero, reversed));
	EXPECT_EQ(randomizerBytes(), before);
}

TEST_F(NewHorizonsQuickStudyTest, SharedSkillEligibilityKeepsRankPerkAndSlotLimits)
{
	startGame();
	const SecondarySkill learning(SecondarySkill::decode(LEARNING_SKILL));
	EXPECT_FALSE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), nullptr, learning));
	EXPECT_FALSE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, SecondarySkill::NONE));
	ASSERT_EQ(hero->getSecSkillLevel(learning), MasteryLevel::NONE);
	EXPECT_TRUE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, learning));
	setLearningRank(MasteryLevel::BASIC);
	EXPECT_FALSE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, learning));
	hero->applyPerkSelection({LEARNING_SKILL, MENTOR});
	EXPECT_TRUE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, learning));
	setLearningRank(MasteryLevel::EXPERT);
	EXPECT_FALSE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, learning));
	setLearningRank(MasteryLevel::NONE);
	JsonNode oneSlot;
	oneSlot.Integer() = 1;
	gameState()->getMap().overrideGameSetting(EGameSettings::HEROES_SKILL_PER_HERO, oneSlot);
	EXPECT_FALSE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, learning));
}

TEST_F(NewHorizonsQuickStudyTest, SharedSkillEligibilityKeepsOwnFactionMapGateAndRejectsForeignFaction)
{
	startGame();
	const auto & rules = hero->getPrimaryGrowthRules();
	const auto own = newHorizonsHeroes::factionSkill(rules, hero->getFactionID());
	const auto foreign = newHorizonsHeroes::factionSkill(rules, FactionID(FactionID::decode("core:tower")));
	ASSERT_TRUE(own);
	ASSERT_TRUE(foreign);
	ASSERT_NE(*own, *foreign);
	EXPECT_FALSE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, *foreign));
	hero->setSecSkillLevel(*own, MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(gameState()->isAllowed(*own));
	EXPECT_TRUE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, *own));
	gameState()->getMap().allowedAbilities.erase(*own);
	EXPECT_FALSE(newHorizonsHeroes::isValidLevelUpSkillChoice(*gameState(), hero, *own));
}

TEST_F(NewHorizonsQuickStudyTest, SharedConnoisseurPoolKeepsMapClassWeightAndCapturedExclusions)
{
	startGame();
	const SecondarySkill learning(SecondarySkill::decode(LEARNING_SKILL));
	const std::vector<SecondarySkill> offered;
	const auto pool = [&]() { return newHorizonsHeroes::connoisseurSkillCandidates(*gameState(), *hero, offered); };
	ASSERT_TRUE(pool().contains(learning));
	const auto before = randomizerBytes();
	gameState()->getMap().allowedAbilities.erase(learning);
	EXPECT_FALSE(pool().contains(learning));
	gameState()->getMap().allowedAbilities.insert(learning);
	auto & rules = const_cast<JsonNode &>(hero->getPrimaryGrowthRules());
	auto & weight = rules["skillOfferWeights"][LEARNING_SKILL];
	const auto positiveWeight = weight;
	weight.Integer() = 0;
	EXPECT_FALSE(pool().contains(learning));
	weight = positiveWeight;
	JsonNode excluded;
	excluded.String() = LEARNING_SKILL;
	rules["excludedSkills"].Vector().push_back(excluded);
	EXPECT_FALSE(pool().contains(learning));
	EXPECT_EQ(randomizerBytes(), before);
}
