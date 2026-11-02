/*
 * NewHorizonsSkillRewardApplicationTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/entities/hero/CHeroClass.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/mapObjects/CRewardableObject.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/CStack.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/mapObjects/CGPandoraBox.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/JsonDeserializer.h"
#include "../../../lib/serializer/JsonSerializer.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../mock/GameHandlerTestServer.h"

namespace
{
class SkillRewardObject : public CRewardableObject
{
public:
	using CRewardableObject::CRewardableObject;
	using Rewardable::Interface::grantRewardBeforeLevelup;
	using Rewardable::Interface::getAvailableRewards;
};
}

class NewHorizonsSkillRewardApplicationTest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		if(!useCommands)
		{
			JsonNode historicalPerks;
			historicalPerks.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, historicalPerks);
		}
	}

	void startGameAsAlchemist(bool newHorizons = true)
	{
		const HeroTypeID actor(HeroTypeID::decode("core:piquedram"));
		ASSERT_GE(actor.getNum(), 0);
		const auto * prototype = actor.toHeroType();
		ASSERT_NE(prototype, nullptr);
		ASSERT_NE(prototype->heroClass, nullptr);
		ASSERT_EQ(prototype->heroClass->getJsonKey(), "core:alchemist");
		useCommands = newHorizons;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false)
			.playerActive(PlayerColor(0))
			.playerActive(PlayerColor(1))
			.hero({5, 5, 0}, actor, PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}})
			.hero({7, 7, 0}, HeroTypeID(1), PlayerColor(1))
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));

		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_EQ(attackerSideHero->getHeroTypeID(), actor);
		ASSERT_EQ(attackerSideHero->getHeroClass()->getJsonKey(), "core:alchemist");
	}
};

TEST_F(NewHorizonsSkillRewardApplicationTest, RawRewardAdjustsExistingEffectiveRankWithoutDowngradingOrIgnoringLoss)
{
	prepareCommands(true);
	const auto effective = newHorizonsMagic::replacementSkill(gameState()->getMagicRules(), SecondarySkill::FIRE_MAGIC);
	attackerSideHero->setSecSkillLevel(effective, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.secondary[SecondarySkill::FIRE_MAGIC] = 1;
	// Actual Rewardable::Interface calculation and authoritative game callbacks;
	// not an asserted graphical visit or bypass of player choice validation.
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	ASSERT_EQ(attackerSideHero->getSecSkillLevel(effective), MasteryLevel::EXPERT);
	if(effective != SecondarySkill::FIRE_MAGIC)
		EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::FIRE_MAGIC), 0);

	info.reward.secondary[SecondarySkill::FIRE_MAGIC] = -1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(effective), MasteryLevel::ADVANCED);
	if(effective != SecondarySkill::FIRE_MAGIC)
		EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::FIRE_MAGIC), 0);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, NewSkillTeacherRewardRespectsAlchemistOfferWeight)
{
	startGameAsAlchemist();
	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	const SecondarySkill lightMagic(SecondarySkill::decode("new-horizons:lightMagic"));

	ASSERT_TRUE(newHorizonsHeroes::usesSkillOfferWeights(attackerSideHero->getPrimaryGrowthRules()));
	const auto wisdomWeight = newHorizonsHeroes::skillOfferWeight(attackerSideHero->getPrimaryGrowthRules(), wisdom);
	ASSERT_TRUE(wisdomWeight);
	EXPECT_EQ(*wisdomWeight, 0);
	const auto lightMagicWeight = newHorizonsHeroes::skillOfferWeight(attackerSideHero->getPrimaryGrowthRules(), lightMagic);
	ASSERT_TRUE(lightMagicWeight);
	EXPECT_GT(*lightMagicWeight, 0);
	ASSERT_TRUE(attackerSideHero->canLearnSkill());
	EXPECT_FALSE(attackerSideHero->canLearnSkill(wisdom));
	ASSERT_TRUE(attackerSideHero->canLearnSkill(lightMagic));

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.secondary[wisdom] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::NONE);

	info.reward.secondary.clear();
	info.reward.secondary[lightMagic] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(lightMagic), MasteryLevel::BASIC);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, MixedTeacherRewardKeepsResourcesWhenSkillIsForbidden)
{
	startGameAsAlchemist();
	const SecondarySkill wisdom(SecondarySkill::decode("new-horizons:wisdom"));
	const auto goldBefore = gameState()->getPlayerState(PlayerColor(0))->resources[GameResID::GOLD];

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.resources[GameResID::GOLD] = 100;
	info.reward.secondary[wisdom] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);

	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[GameResID::GOLD], goldBefore + 100);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(wisdom), MasteryLevel::NONE);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, LegacyDirectSkillRewardStillUsesFreeSlotEligibility)
{
	startGameAsAlchemist(false);
	ASSERT_FALSE(newHorizonsHeroes::usesSkillOfferWeights(attackerSideHero->getPrimaryGrowthRules()));
	ASSERT_TRUE(attackerSideHero->canLearnSkill());

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.reward.secondary[SecondarySkill::WISDOM] = 1;
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);

	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill::WISDOM), MasteryLevel::BASIC);
}

TEST_F(NewHorizonsSkillRewardApplicationTest, ExistingSkillTeacherRewardCanUpgradeWithFullSkillBar)
{
	startGameAsAlchemist();
	const SecondarySkill lightMagic(SecondarySkill::decode("new-horizons:lightMagic"));
	attackerSideHero->setSecSkillLevel(lightMagic, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);

	for(const auto & candidate : LIBRARY->skillh->objects)
	{
		if(!attackerSideHero->canLearnSkill())
			break;
		if(!candidate || attackerSideHero->getSecSkillLevel(candidate->getId()) != MasteryLevel::NONE)
			continue;
		attackerSideHero->setSecSkillLevel(candidate->getId(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	}
	ASSERT_FALSE(attackerSideHero->canLearnSkill());

	SkillRewardObject object(gameState().get());
	Rewardable::VisitInfo info;
	info.visitType = Rewardable::EEventType::EVENT_FIRST_VISIT;
	info.reward.secondary[lightMagic] = 1;
	object.configuration.info.push_back(info);
	EXPECT_EQ(object.getAvailableRewards(attackerSideHero, Rewardable::EEventType::EVENT_FIRST_VISIT),
		(std::vector<ui32>{0}));
	object.grantRewardBeforeLevelup(*gameHandler, info, attackerSideHero);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(lightMagic), MasteryLevel::ADVANCED);
}

namespace
{
class NewHorizonsPandoraRewardDialogTest : public TinyMapGameTest
{
protected:
	Services * gameServices() override { return LIBRARY; }
	bool teachesSkill = true;
	bool guarded = false;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
	CGHeroInstance * hero = nullptr;
	CGPandoraBox * box = nullptr;

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto * pandora = loaded->getObjects<CGPandoraBox>().front();
		JsonNode authored;
		JsonSerializer writer(nullptr, authored);
		pandora->CGObjectInstance::serializeJson(writer);
		authored["options"].clear();
		if(teachesSkill)
			authored["options"]["secondarySkills"]["new-horizons:learning"].String() = "basic";
		JsonDeserializer reader(nullptr, authored);
		pandora->CGObjectInstance::serializeJson(reader);
		if(guarded)
			pandora->putStack(SlotID(0), std::make_unique<CStackInstance>(gameState().get(), CreatureID(0), 1));
	}

	void preparePandora()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:nimbus")), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 10}})
			.hero({12, 12, 0}, HeroTypeID(1), PlayerColor(1)).heroGarrison({{CreatureID(0), 10}})
			.pandora({8, 5, 0});
		startWithMap(std::move(builder));
		hero = findHeroByOwner(PlayerColor(0));
		box = findFirst<CGPandoraBox>();
		ASSERT_NE(hero, nullptr);
		ASSERT_NE(box, nullptr);
		server = std::make_unique<GameHandlerTestServer>(gameState(), PlayerColor(0));
		handler = std::make_unique<CGameHandler>(*server, gameState());
		handler->onAdvInterfaceReady(PlayerColor(0));
		hero->setSecSkillLevel(learning(), MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		// Legacy JSON adds its authored entry; Pandora initialization appends an empty one.
		ASSERT_EQ(box->getAvailableRewards(hero, Rewardable::EEventType::EVENT_FIRST_VISIT).size(), 2u);
	}

	SecondarySkill learning() const { return SecondarySkill(SecondarySkill::decode("new-horizons:learning")); }

	std::vector<std::byte> randomState() const
	{
		CMemorySerializer memory;
		memory.oser & *handler->randomizer;
		return memory.extractBuffer();
	}
};
}

TEST_F(NewHorizonsPandoraRewardDialogTest, SelectedSkillReplyCommitsOnceThroughRealVirtualDispatch)
{
	ASSERT_NO_FATAL_FAILURE(preparePandora());
	const auto id = box->id;
	const auto resources = gameState()->getPlayerState(PlayerColor(0))->resources;
	const auto random = randomState();
	handler->objectVisited(box, hero);
	const auto open = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(open, nullptr);
	ASSERT_EQ(open->getType(), QueryType::BlockingDialog);
	EXPECT_FALSE(handler->queryReply(open->queryID, 1, PlayerColor(1)));
	EXPECT_EQ(handler->queries->topQuery(PlayerColor(0)), open);
	ASSERT_TRUE(handler->queryReply(open->queryID, 1, PlayerColor(0)));
	const auto selection = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(selection, nullptr);
	ASSERT_NE(selection->queryID, open->queryID);
	ASSERT_EQ(selection->getType(), QueryType::BlockingDialog);
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::BASIC);
	ASSERT_TRUE(handler->queryReply(selection->queryID, 1, PlayerColor(0)));
	EXPECT_EQ(handler->queries->topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::ADVANCED);
	EXPECT_EQ(gameState()->getObjInstance(id), nullptr);
	EXPECT_FALSE(handler->queryReply(selection->queryID, 1, PlayerColor(0)));
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::ADVANCED);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources, resources);
	EXPECT_EQ(randomState(), random);
}

TEST_F(NewHorizonsPandoraRewardDialogTest, RefusedSkillSelectionRetiresBeforeFreshInitialConfirmation)
{
	ASSERT_NO_FATAL_FAILURE(preparePandora());
	const auto id = box->id;
	const auto random = randomState();
	handler->objectVisited(box, hero);
	const auto initial = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(initial, nullptr);
	ASSERT_TRUE(handler->queryReply(initial->queryID, 1, PlayerColor(0)));
	const auto refused = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(refused, nullptr);
	ASSERT_TRUE(handler->queryReply(refused->queryID, 0, PlayerColor(0)));
	EXPECT_EQ(handler->queries->topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::BASIC);
	ASSERT_EQ(gameState()->getObjInstance(id), box);
	handler->objectVisited(box, hero);
	const auto reopened = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(reopened, nullptr);
	ASSERT_TRUE(handler->queryReply(reopened->queryID, 1, PlayerColor(0)));
	const auto fresh = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(fresh, nullptr);
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::BASIC);
	EXPECT_FALSE(handler->queryReply(refused->queryID, 1, PlayerColor(0)));
	EXPECT_EQ(handler->queries->topQuery(PlayerColor(0)), fresh);
	ASSERT_TRUE(handler->queryReply(fresh->queryID, 1, PlayerColor(0)));
	EXPECT_EQ(handler->queries->topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::ADVANCED);
	EXPECT_EQ(gameState()->getObjInstance(id), nullptr);
	EXPECT_EQ(randomState(), random);
}

TEST_F(NewHorizonsPandoraRewardDialogTest, InitialRefusalAndEmptyBoxKeepOrdinaryRoutes)
{
	teachesSkill = false;
	ASSERT_NO_FATAL_FAILURE(preparePandora());
	const auto id = box->id;
	const auto random = randomState();
	handler->objectVisited(box, hero);
	const auto refused = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(refused, nullptr);
	ASSERT_TRUE(handler->queryReply(refused->queryID, 0, PlayerColor(0)));
	EXPECT_EQ(handler->queries->topQuery(PlayerColor(0)), nullptr);
	ASSERT_EQ(gameState()->getObjInstance(id), box);
	handler->objectVisited(box, hero);
	const auto opened = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(opened, nullptr);
	ASSERT_TRUE(handler->queryReply(opened->queryID, 1, PlayerColor(0)));
	EXPECT_EQ(handler->queries->topQuery(PlayerColor(0)), nullptr);
	EXPECT_EQ(gameState()->getObjInstance(id), nullptr);
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::BASIC);
	EXPECT_EQ(randomState(), random);
}

TEST_F(NewHorizonsPandoraRewardDialogTest, InitialGuardedConfirmationStartsBattleWithoutGrantingSkill)
{
	guarded = true;
	ASSERT_NO_FATAL_FAILURE(preparePandora());
	const auto id = box->id;
	handler->objectVisited(box, hero);
	const auto opened = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(opened, nullptr);
	ASSERT_TRUE(handler->queryReply(opened->queryID, 1, PlayerColor(0)));
	const auto battleQuery = handler->queries->topQuery(PlayerColor(0));
	ASSERT_NE(battleQuery, nullptr);
	EXPECT_EQ(battleQuery->getType(), QueryType::Battle);
	EXPECT_EQ(gameState()->getObjInstance(id), box);
	EXPECT_EQ(hero->getSecSkillLevel(learning()), MasteryLevel::BASIC);
}
