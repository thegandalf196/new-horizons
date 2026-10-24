/*
 * NewHorizonsSwampSeerTest.cpp, part of VCMI / New Horizons
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../mock/TinyH3MBuilder.h"
#include "../../mock/TinyMapGameTest.h"
#include "../../mock/GameHandlerTestServer.h"
#include "../../../AI/Nullkiller2/Engine/PriorityEvaluator.h"
#include "../../../lib/entities/hero/NewHorizonsSwampSeer.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/Component.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/CQuery.h"
#include "../../../server/queries/QueriesProcessor.h"
#include <cmath>
#include <limits>
namespace
{
constexpr PlayerColor PLAYER(0);
constexpr auto LEARNING = "new-horizons:learning";
constexpr auto EAGLE = "new-horizons:learning.eagleEye";
constexpr auto HISTORIAN = "new-horizons:learning.historian";
class StudyServer : public GameHandlerTestServer
{
public:
	explicit StudyServer(std::shared_ptr<CGameState> state) : GameHandlerTestServer(std::move(state), PLAYER) {}
	int experiencePackets = 0;
	TExpType lastExperience = 0;
	void applyPack(CPackForClient & packet) override
	{
		if(const auto * experience = dynamic_cast<SetHeroExperience *>(&packet))
		{
			++experiencePackets;
			lastExperience = experience->val;
		}
		GameHandlerTestServer::applyPack(packet);
	}
};
class NewHorizonsSwampSeerTest : public TinyMapGameTest
{
protected:
	bool captured = true;
	bool historianFixture = false;
	int coefficient = 250;
	CGHeroInstance * tiva = nullptr;
	CGHeroInstance * malcom = nullptr;
	std::unique_ptr<StudyServer> server;
	std::unique_ptr<CGameHandler> handler;
	int siteX = 12;
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		rules.setOverrideFlag(true);
		rules.Struct().erase("swampSeerSpecialty");
		if(captured)
		{
			auto & row = rules["swampSeerSpecialty"];
			row["version"].Integer() = 1;
			row["learningPercentPerLevel"].Integer() = 5;
			row["learningMaximumPercent"].Integer() = 100;
			row["studyExperiencePerMagicPrimaryPoint"].Integer() = coefficient;
		}
		if(historianFixture)
		{
			// Explicit authored-map fixture, not an ordinary start/profile change.
			auto & perk = rules["startingSkills"]["startingDevelopmentProfiles"]["core:tiva"]["startingPerks"].Vector().front();
			perk["perk"].String() = HISTORIAN;
		}
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}
	void prepare(int level = 20)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PLAYER)
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:tiva")), PLAYER)
			.hero({7, 5, 0}, HeroTypeID(HeroTypeID::decode("core:malcom")), PLAYER);
		startWithMap(std::move(builder));
		tiva = findHeroAt({5, 5, 0});
		malcom = findHeroAt({7, 5, 0});
		ASSERT_NE(tiva, nullptr);
		ASSERT_NE(malcom, nullptr);
		ASSERT_TRUE(tiva->hasActivePerk(LEARNING, historianFixture ? HISTORIAN : EAGLE));
		ASSERT_EQ(tiva->hasActivePerk(LEARNING, HISTORIAN), historianFixture);
		ASSERT_TRUE(tiva->spellbookContainsSpell(SpellID(SpellID::decode("new-horizons:regeneration"))));
		for(auto * hero : {tiva, malcom})
		{
			hero->level = level;
			hero->exp = LIBRARY->heroh->reqExp(level);
		}
		server = std::make_unique<StudyServer>(gameState());
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->onAdvInterfaceReady(PLAYER);
		handler->giveResource(PLAYER, GameResID(GameResID::GOLD), 10000);
		server->experiencePackets = 0;
	}
	CRewardableObject * site(Obj type)
	{
		auto object = handler->createNewObject({siteX, 12, 0}, type, MapObjectSubID(0));
		siteX += 2;
		if(!object)
			return nullptr;
		handler->newObject(object, PLAYER);
		return dynamic_cast<CRewardableObject *>(gameState()->getObjInstance(object->id));
	}
	const Rewardable::Reward & firstReward(const CRewardableObject & object) const
	{
		return object.configuration.info.at(object.getAvailableRewards(tiva, Rewardable::EEventType::EVENT_FIRST_VISIT).front()).reward;
	}
	TExpType preview(const Rewardable::Reward & reward, Obj source) const
	{
		std::vector<Component> components;
		reward.loadComponents(components, tiva, source);
		for(const auto & component : components)
			if(component.type == ComponentType::EXPERIENCE && component.value)
				return *component.value;
		return 0;
	}
	bool finishLevelUps()
	{
		for(ui32 i = 0; i < LIBRARY->heroh->maxSupportedLevel(); ++i)
		{
			const auto query = handler->queries->topQuery(PLAYER);
			if(!query)
				return true;
			if(query->getType() != QueryType::HeroLevelUpDialog || !handler->queryReply(query->queryID, 0, PLAYER))
				return false;
		}
		return false;
	}
};
}
TEST_F(NewHorizonsSwampSeerTest, GardenAndStarAcceptedVisitsMatchProjectionAndEachUseOneExperienceGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto type : {Obj::GARDEN_OF_REVELATION, Obj::STAR_AXIS})
	{
		auto * object = site(type);
		ASSERT_NE(object, nullptr);
		const auto & reward = firstReward(*object);
		EXPECT_EQ(newHorizonsHeroes::projectedStudyPrimaryPoints(*tiva, reward, type), 1);
		EXPECT_EQ(reward.calculateHeroExperience(tiva, true, type), 300);
		EXPECT_EQ(preview(reward, type), 300);
		EXPECT_EQ(reward.getDisplayedComponent(tiva, type).type, ComponentType::PRIM_SKILL);
		const auto before = tiva->exp;
		const auto packets = server->experiencePackets;
		handler->objectVisited(object, tiva);
		EXPECT_EQ(tiva->exp - before, 300);
		EXPECT_EQ(server->experiencePackets - packets, 1);
		EXPECT_TRUE(object->wasVisited(tiva));
		handler->objectVisited(object, tiva);
		EXPECT_EQ(tiva->exp - before, 300);
		EXPECT_EQ(server->experiencePackets - packets, 1);
	}
}
TEST_F(NewHorizonsSwampSeerTest, LibraryCountsOnlyFourMagicalPointsAndRespectsOrdinaryAdmission)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	auto * library = site(Obj::LIBRARY_OF_ENLIGHTENMENT);
	ASSERT_NE(library, nullptr);
	EXPECT_TRUE(library->getAvailableRewards(tiva, Rewardable::EEventType::EVENT_FIRST_VISIT).empty());
	NK2AI::RewardEvaluator evaluator(nullptr);
	EXPECT_FLOAT_EQ(evaluator.getSkillReward(library, tiva, NK2AI::HeroRole::MAIN), 8.0f);
	tiva->level = 20;
	tiva->exp = LIBRARY->heroh->reqExp(20);
	const auto & reward = firstReward(*library);
	EXPECT_EQ(newHorizonsHeroes::projectedStudyPrimaryPoints(*tiva, reward, library->ID), 4);
	EXPECT_EQ(preview(reward, library->ID), 1200);
	const auto before = tiva->exp;
	handler->objectVisited(library, tiva);
	EXPECT_EQ(tiva->exp - before, 1200);
	EXPECT_EQ(server->experiencePackets, 1);
}
TEST_F(NewHorizonsSwampSeerTest, NearCapActualDeltaIsCarriedAfterMutationRatherThanReprojectedToZero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto maximum = tiva->getPrimaryGrowthRules()["maxPrimary"].Integer();
	tiva->setPrimarySkill(PrimarySkill::KNOWLEDGE, maximum - 1, ChangeValueMode::ABSOLUTE);
	tiva->setPrimarySkill(PrimarySkill::SPELL_POWER, maximum, ChangeValueMode::ABSOLUTE);
	tiva->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::OTHER, 500, BonusSourceID(), BonusSubtypeID(PrimarySkill::KNOWLEDGE)));
	const auto modifiedBefore = tiva->getPrimSkillLevel(PrimarySkill::KNOWLEDGE);
	auto * library = site(Obj::LIBRARY_OF_ENLIGHTENMENT);
	ASSERT_NE(library, nullptr);
	const auto reward = firstReward(*library);
	EXPECT_EQ(newHorizonsHeroes::projectedStudyPrimaryPoints(*tiva, reward, library->ID), 1);
	EXPECT_EQ(preview(reward, library->ID), 300);
	const auto before = tiva->exp;
	handler->objectVisited(library, tiva);
	EXPECT_EQ(tiva->exp - before, 300);
	EXPECT_EQ(server->experiencePackets, 1);
	EXPECT_EQ(tiva->getBasePrimarySkillValue(PrimarySkill::KNOWLEDGE), maximum);
	EXPECT_EQ(tiva->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), modifiedBefore);
	EXPECT_EQ(reward.calculateHeroExperience(tiva, true, library->ID), 0);
	EXPECT_EQ(reward.calculateHeroExperienceForAcceptedReward(tiva, library->ID, 1), 300);
	EXPECT_EQ(reward.calculateHeroExperienceForAcceptedReward(tiva, library->ID, 0), 0);
}
TEST_F(NewHorizonsSwampSeerTest, CappedAndPhysicalOrUnlistedRewardsGrantNoStudyExperience)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto maximum = tiva->getPrimaryGrowthRules()["maxPrimary"].Integer();
	tiva->setPrimarySkill(PrimarySkill::KNOWLEDGE, maximum, ChangeValueMode::ABSOLUTE);
	auto * garden = site(Obj::GARDEN_OF_REVELATION);
	ASSERT_NE(garden, nullptr);
	EXPECT_EQ(preview(firstReward(*garden), garden->ID), 0);
	const auto before = tiva->exp;
	handler->objectVisited(garden, tiva);
	EXPECT_EQ(tiva->exp, before);
	EXPECT_EQ(server->experiencePackets, 0);
	Rewardable::Reward physical;
	physical.primary[PrimarySkill::ATTACK.getNum()] = 2;
	EXPECT_EQ(physical.calculateHeroExperience(tiva, true, Obj::LIBRARY_OF_ENLIGHTENMENT), 0);
	Rewardable::Reward magical;
	magical.primary[PrimarySkill::SPELL_POWER.getNum()] = 1;
	EXPECT_EQ(magical.calculateHeroExperience(tiva, true, Obj::MERCENARY_CAMP), 0);
	EXPECT_EQ(magical.calculateHeroExperience(tiva), 0); // Source-less stays ordinary.
}
TEST_F(NewHorizonsSwampSeerTest, SchoolChoiceValuesActualLastKnowledgeOptionWithoutOverclaimingUnchosenPower)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto maximum = tiva->getPrimaryGrowthRules()["maxPrimary"].Integer();
	tiva->setPrimarySkill(PrimarySkill::KNOWLEDGE, maximum, ChangeValueMode::ABSOLUTE);
	auto * school = site(Obj::SCHOOL_OF_MAGIC);
	ASSERT_NE(school, nullptr);
	const auto available = school->getAvailableRewards(tiva, Rewardable::EEventType::EVENT_FIRST_VISIT);
	ASSERT_EQ(available.size(), 2);
	const auto & power = school->configuration.info.at(available.front()).reward;
	const auto & knowledge = school->configuration.info.at(available.back()).reward;
	EXPECT_EQ(newHorizonsHeroes::projectedSwampSeerStudyExperience(tiva, power, school->ID), 300);
	EXPECT_EQ(newHorizonsHeroes::projectedSwampSeerStudyExperience(tiva, knowledge, school->ID), 0);
	EXPECT_EQ(power.getDisplayedComponent(tiva, school->ID).type, ComponentType::PRIM_SKILL);
	EXPECT_EQ(knowledge.getDisplayedComponent(tiva, school->ID).type, ComponentType::PRIM_SKILL);
	NK2AI::RewardEvaluator evaluator(nullptr);
	EXPECT_FLOAT_EQ(evaluator.getSkillReward(school, tiva, NK2AI::HeroRole::MAIN), 1.0f);
	const auto before = tiva->exp;
	handler->objectVisited(school, tiva);
	const auto query = handler->queries->topQuery(PLAYER);
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(handler->queryReply(query->queryID, static_cast<int>(available.size()), PLAYER));
	EXPECT_EQ(tiva->exp, before);
	EXPECT_EQ(server->experiencePackets, 0);
}
TEST_F(NewHorizonsSwampSeerTest, PaidSchoolChoiceAndRefusalPreservePaymentAndVisitSemantics)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * school = site(Obj::SCHOOL_OF_MAGIC);
	ASSERT_NE(school, nullptr);
	const auto before = tiva->exp;
	const auto gold = gameState()->getPlayerState(PLAYER)->resources[GameResID(GameResID::GOLD)];
	handler->objectVisited(school, tiva);
	auto query = handler->queries->topQuery(PLAYER);
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(handler->queryReply(query->queryID, 0, PLAYER));
	EXPECT_EQ(tiva->exp, before);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[GameResID(GameResID::GOLD)], gold);
	EXPECT_FALSE(school->wasVisited(tiva));
	handler->objectVisited(school, tiva);
	query = handler->queries->topQuery(PLAYER);
	ASSERT_NE(query, nullptr);
	ASSERT_TRUE(handler->queryReply(query->queryID, 2, PLAYER));
	EXPECT_EQ(tiva->exp - before, 300);
	EXPECT_EQ(server->experiencePackets, 1);
	EXPECT_EQ(gameState()->getPlayerState(PLAYER)->resources[GameResID(GameResID::GOLD)], gold - 1000);
	EXPECT_TRUE(school->wasVisited(tiva));
}
TEST_F(NewHorizonsSwampSeerTest, UnaffordableAndUnknownChoiceModesAddNoSpeculativeAIReward)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * school = site(Obj::SCHOOL_OF_MAGIC);
	ASSERT_NE(school, nullptr);
	SetResources money;
	money.player = PLAYER;
	money.res = gameState()->getPlayerState(PLAYER)->resources;
	money.res[GameResID(GameResID::GOLD)] = 0;
	handler->sendAndApply(money);
	NK2AI::RewardEvaluator evaluator(nullptr);
	EXPECT_TRUE(school->getAvailableRewards(tiva, Rewardable::EEventType::EVENT_FIRST_VISIT).empty());
	EXPECT_FLOAT_EQ(evaluator.getSkillReward(school, tiva, NK2AI::HeroRole::MAIN), 1.0f);
	handler->giveResource(PLAYER, GameResID(GameResID::GOLD), 1000);
	school->configuration.selectMode = Rewardable::SELECT_RANDOM;
	EXPECT_FLOAT_EQ(evaluator.getSkillReward(school, tiva, NK2AI::HeroRole::MAIN), 1.0f);
	school->configuration.selectMode = Rewardable::SELECT_ALL;
	EXPECT_FLOAT_EQ(evaluator.getSkillReward(school, tiva, NK2AI::HeroRole::MAIN), 1.0f);
}
TEST_F(NewHorizonsSwampSeerTest, StudyAIPriorityUsesSameAvailableSingleRewardAndDoesNotMutateHero)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * garden = site(Obj::GARDEN_OF_REVELATION);
	ASSERT_NE(garden, nullptr);
	NK2AI::RewardEvaluator evaluator(nullptr);
	const auto before = tiva->exp;
	const auto knowledge = tiva->getBasePrimarySkillValue(PrimarySkill::KNOWLEDGE);
	EXPECT_NEAR(evaluator.getSkillReward(garden, tiva, NK2AI::HeroRole::MAIN), 1.0f + 300.0f / (1000 * std::sqrt(20.0f)), 1e-6f);
	EXPECT_EQ(tiva->exp, before);
	EXPECT_EQ(tiva->getBasePrimarySkillValue(PrimarySkill::KNOWLEDGE), knowledge);
	EXPECT_EQ(server->experiencePackets, 0);
	handler->objectVisited(garden, tiva);
	EXPECT_FLOAT_EQ(evaluator.getSkillReward(garden, tiva, NK2AI::HeroRole::MAIN), 1.0f);
}
TEST_F(NewHorizonsSwampSeerTest, HistorianCustomStudyComponentStaysSeparateFromOrdinaryPrimaryExperience)
{
	historianFixture = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * garden = site(Obj::GARDEN_OF_REVELATION);
	ASSERT_NE(garden, nullptr);
	auto & reward = garden->configuration.info.front().reward;
	reward.heroExperience = 1000;
	reward.primaryExperienceReward = true;
	EXPECT_EQ(reward.calculateHeroExperience(tiva, true, garden->ID), 2000); // ordinary1700 + study300, not2125.
	EXPECT_EQ(reward.calculateHeroExperience(tiva, false, garden->ID), 1500);
	EXPECT_EQ(preview(reward, garden->ID), 2000);
	const auto before = tiva->exp;
	handler->objectVisited(garden, tiva);
	EXPECT_EQ(tiva->exp - before, 2000);
	EXPECT_EQ(server->experiencePackets, 1);
}
TEST_F(NewHorizonsSwampSeerTest, LevelUpPrimaryGainsAndDirectPrimaryChangesDoNotRecurseIntoStudyXP)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1));
	handler->changePrimSkill(tiva, PrimarySkill::KNOWLEDGE, 1, ChangeValueMode::RELATIVE);
	EXPECT_EQ(server->experiencePackets, 0);
	auto * garden = site(Obj::GARDEN_OF_REVELATION);
	ASSERT_NE(garden, nullptr);
	const auto expected = firstReward(*garden).calculateHeroExperience(tiva, true, garden->ID);
	tiva->exp = LIBRARY->heroh->reqExp(2) - 10;
	const auto before = tiva->exp;
	handler->objectVisited(garden, tiva);
	ASSERT_TRUE(finishLevelUps());
	EXPECT_GT(tiva->level, 1);
	EXPECT_EQ(tiva->exp - before, expected);
	EXPECT_EQ(server->experiencePackets, 1);
}
TEST_F(NewHorizonsSwampSeerTest, SavedVisitAndCapturedPolicyRetainXPWithoutRegrantAfterReload)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * garden = site(Obj::GARDEN_OF_REVELATION);
	ASSERT_NE(garden, nullptr);
	handler->objectVisited(garden, tiva);
	const auto savedXP = tiva->exp;
	const auto heroID = tiva->id;
	const auto objectID = garden->id;
	auto restored = std::make_shared<CGameState>();
	restored->preInit(LIBRARY);
	restored->loadFromMemory(gameState()->saveToMemory());
	auto * hero = restored->getHero(heroID);
	auto * object = dynamic_cast<CRewardableObject *>(restored->getObjInstance(objectID));
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(object, nullptr);
	ASSERT_TRUE(newHorizonsHeroes::swampSeerRules(hero));
	EXPECT_TRUE(object->wasVisited(hero));
	StudyServer loadedServer(restored);
	CGameHandler loadedHandler(loadedServer, restored);
	loadedHandler.onAdvInterfaceReady(PLAYER);
	loadedHandler.objectVisited(object, hero);
	EXPECT_EQ(hero->exp, savedXP);
	EXPECT_EQ(loadedServer.experiencePackets, 0);
}
TEST_F(NewHorizonsSwampSeerTest, AbsenceAndWrongIdentityKeepOrdinaryOutputAndMalcomStoneIdentity)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(tiva->calculateXp(1000), 1100);
	auto * garden = site(Obj::GARDEN_OF_REVELATION);
	ASSERT_NE(garden, nullptr);
	EXPECT_EQ(firstReward(*garden).calculateHeroExperience(tiva, true, garden->ID), 0);
	EXPECT_EQ(firstReward(*garden).calculateHeroExperience(malcom, true, garden->ID), 0);
	Rewardable::Reward stone;
	stone.heroExperience = 1000;
	stone.primaryExperienceReward = true;
	EXPECT_EQ(stone.calculateHeroExperience(malcom, true, Obj::LEARNING_STONE), 1500);
	EXPECT_EQ(stone.calculateHeroExperience(tiva, true, Obj::LEARNING_STONE), 1100);
}
TEST_F(NewHorizonsSwampSeerTest, CoreXPZeroNegativeMaximumAndInactiveLearningUseExistingFloors)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(tiva->calculateXp(1000), 1200);
	EXPECT_EQ(tiva->calculateXp(1000, 50), 1700);
	EXPECT_EQ(tiva->calculateXp(19), 21);
	EXPECT_EQ(tiva->calculateXp(0), 0);
	EXPECT_EQ(tiva->calculateXp(-1000), -1100);
	tiva->level = std::numeric_limits<uint32_t>::max();
	EXPECT_EQ(tiva->calculateXp(1000), 1200);
	EXPECT_EQ(tiva->calculateXp(std::numeric_limits<TExpType>::max()), std::numeric_limits<TExpType>::max());
	tiva->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(LEARNING)), MasteryLevel::NONE, ChangeValueMode::ABSOLUTE);
	Rewardable::Reward study;
	study.primary[PrimarySkill::KNOWLEDGE.getNum()] = 1;
	EXPECT_EQ(study.calculateHeroExperience(tiva, true, Obj::GARDEN_OF_REVELATION), 0);
	EXPECT_EQ(tiva->calculateXp(1000), 1000);
	EXPECT_NE(tiva->getSpecialtyPresentation().description.find("study experience is inactive"), std::string::npos);
}
TEST_F(NewHorizonsSwampSeerTest, CoefficientExtremeAndSignedCompositionSaturateWithoutWideMultiply)
{
	coefficient = 1000000;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsHeroes::swampSeerStudyExperience(tiva, Obj::GARDEN_OF_REVELATION, 2000000), tiva->calculateXp(2000000000000LL));
	EXPECT_EQ(newHorizonsHeroes::swampSeerStudyExperience(tiva, Obj::GARDEN_OF_REVELATION, std::numeric_limits<int64_t>::max()), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(newHorizonsHeroes::addStudyExperience(std::numeric_limits<int64_t>::max() - 1, 2), std::numeric_limits<int64_t>::max());
	EXPECT_EQ(newHorizonsHeroes::addStudyExperience(std::numeric_limits<int64_t>::min() + 1, -2), std::numeric_limits<int64_t>::min());
	EXPECT_THROW(newHorizonsHeroes::swampSeerStudyExperience(tiva, Obj::STAR_AXIS, -1), std::invalid_argument);
}
