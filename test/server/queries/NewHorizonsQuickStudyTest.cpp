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
constexpr auto QUICK_STUDY = "new-horizons:learning.quickStudy";

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
		perkRules.Struct().erase("quickStudyPolicy");
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
		if(expectReroll)
			expectedOffer = prepareExpectedOffer(expectedRandomizer, hero);

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

	void enableOptionalPolicy(int interval = 3)
	{
		auto & rules = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).rules;
		rules["quickStudyPolicy"]["version"].Integer() = 1;
		rules["quickStudyPolicy"]["interval"].Integer() = interval;
	}

	std::shared_ptr<CHeroLevelUpDialogQuery> openOptionalOffer(int level)
	{
		hero->level = level - 1;
		hero->setExperience(LIBRARY->heroh->reqExp(level), ChangeValueMode::ABSOLUTE);
		gameHandler->onAdvInterfaceReady(hero->getOwner());
		gameHandler->levelUpHero(hero);
		return std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(hero->getOwner()));
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

TEST_F(NewHorizonsQuickStudyTest, RerollsTheWholeOfferOnceAtEveryFifthReachedLevel)
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

TEST_F(NewHorizonsQuickStudyTest, OptionalRerollReplacesWholeOfferOnceWithoutAnotherLevel)
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
	const auto replacement = prepareExpectedOffer(*expected, hero);
	ASSERT_TRUE(gameHandler->queryReply(oldID, CHeroLevelUpDialogQuery::REROLL_REPLY, hero->getOwner()));
	EXPECT_EQ(gameHandler->queries->topQuery(hero->getOwner()), query);
	EXPECT_NE(query->queryID, oldID);
	EXPECT_EQ(query->hlu.replacesQueryID, QueryID::NONE);
	ASSERT_GE(server->offers.size(), 2u);
	EXPECT_EQ(server->offers.back().replacesQueryID, oldID);
	EXPECT_FALSE(query->hlu.optionalRerollAvailable);
	EXPECT_EQ(query->hlu.skills, replacement.skills);
	EXPECT_EQ(query->hlu.perks, replacement.perks);
	EXPECT_EQ(query->hlu.perkOfferSeed, replacement.perkOfferSeed);
	EXPECT_EQ(query->hlu.primaryGains, oldPrimary);
	EXPECT_EQ(hero->level, 6);
	EXPECT_EQ(hero->getPerkState().selected, selections);
	const auto afterReroll = randomizerBytes();
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
		EXPECT_EQ(query->hlu.optionalRerollAvailable, level % 3 == 0);
		if(level % 3)
		{
			const auto before = randomizerBytes();
			EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
			EXPECT_EQ(randomizerBytes(), before);
		}
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
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
	setLearningRank(MasteryLevel::ADVANCED);
	markQuickStudyPlanned();
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
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
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	gameHandler->queries->popQuery(blocking);
	query->hlu.perkOfferSeed = -1;
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
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

TEST_F(NewHorizonsQuickStudyTest, CompletedChoiceRoundTripsCapturedPolicyAtItsExplicitFormat)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(3);
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
	ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, hero->getOwner()));
	EXPECT_NO_THROW(newHorizonsLevelUp::validateNoPendingOptionalOffer(*gameHandler));
	ASSERT_GE(ESerializationVersion::CURRENT, ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY);
	for(const bool explicitFormat : {true, false})
	{
		CMemorySerializer save;
		if(explicitFormat) save.oser.version = ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY;
		else EXPECT_EQ(save.oser.version, ESerializationVersion::CURRENT);
		ASSERT_NO_THROW(save.oser & *gameState());
		CGameState restored;
		restored.preInit(LIBRARY);
		save.iser.version = save.oser.version;
		save.iser.loadingGamestate = true;
		save.iser.cb = &restored;
		ASSERT_NO_THROW(save.iser & restored);
		const auto * loaded = restored.getHero(hero->id);
		ASSERT_NE(loaded, nullptr);
		EXPECT_EQ(loaded->level, 3);
		EXPECT_EQ(loaded->getPerkState().selected, hero->getPerkState().selected);
		EXPECT_EQ(newHorizonsHeroes::quickStudyRerollInterval(loaded->getPerkState().rules), 3);
	}
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
	EXPECT_THROW(newHorizonsHeroes::validateWeightedPerkSerialization(rules, true, true, true), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsHeroes::validateWeightedPerkSerialization(rules, true, true, true, true));
	rules["quickStudyPolicy"]["interval"].Integer() = 0;
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(rules), std::runtime_error);
	rules["quickStudyPolicy"] = JsonNode();
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

TEST_F(NewHorizonsQuickStudyTest, DirectHeroSerializationUsesTheDeclaredOptionalPolicyCapabilityBeforePrefix)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	CMemorySerializer supported;
	supported.oser.version = ESerializationVersion::NEW_HORIZONS_OPTIONAL_QUICK_STUDY;
	EXPECT_NO_THROW(supported.oser & *hero);
	EXPECT_FALSE(supported.extractBuffer().empty());

	CMemorySerializer unsupported;
	unsupported.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	EXPECT_THROW(unsupported.oser & *hero, std::runtime_error);
	EXPECT_TRUE(unsupported.extractBuffer().empty());

	auto & rules = const_cast<newHorizonsHeroes::PerkState &>(hero->getPerkState()).rules;
	rules.Struct().erase("quickStudyPolicy");
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_EXTENDED_ORDINARY_SPECIALTIES;
	EXPECT_NO_THROW(legacy.oser & *hero);
	EXPECT_FALSE(legacy.extractBuffer().empty());
}

TEST_F(NewHorizonsQuickStudyTest, RejectedRerollConfirmsHeldOfferBeforeAckWithoutGrowthOrDraws)
{
	startGame();
	selectQuickStudy();
	enableOptionalPolicy();
	const auto query = openOptionalOffer(4);
	ASSERT_NE(query, nullptr);
	const auto held = query->hlu;
	const auto idCounter = gameHandler->QID;
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
	const auto before = randomizerBytes();
	const auto count = server->offers.size();
	EXPECT_FALSE(query->requestReroll(PlayerColor(1)));
	EXPECT_FALSE(gameHandler->queryReply(QueryID(query->queryID.getNum() + 1), -1, hero->getOwner()));
	auto blocking = std::make_shared<CGenericQuery>(gameHandler.get(), hero->getOwner(), [](auto) {});
	gameHandler->queries->addQuery(blocking);
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
	gameHandler->queries->popQuery(blocking);
	EXPECT_EQ(server->offers.size(), count);
	const auto idCounter = gameHandler->QID;
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
	EXPECT_EQ(server->offers.back().queryID, query->queryID);
	EXPECT_EQ(server->offers.back().replacesQueryID, query->queryID);
	EXPECT_FALSE(server->offers.back().optionalRerollAvailable);
	EXPECT_EQ(query->hlu.replacesQueryID, QueryID::NONE);
	EXPECT_EQ(gameHandler->QID, idCounter);
	EXPECT_EQ(randomizerBytes(), before);
	query->hlu.perkOfferSeed = -1;
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, -1, hero->getOwner()));
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
	ASSERT_TRUE(gameHandler->queryReply(originalID, -1, hero->getOwner()));
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
