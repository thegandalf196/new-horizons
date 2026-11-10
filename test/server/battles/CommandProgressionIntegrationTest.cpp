/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/queries/MapQueries.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/networkPacks/PacksForServer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/campaign/CampaignState.h"

namespace
{
using namespace newHorizonsHeroes;

template<typename Packet> Packet wire(const Packet & outgoing)
{
	CMemorySerializer memory;
	const CPack * base = &outgoing;
	memory.oser & base;
	std::unique_ptr<CPack> incoming;
	memory.iser & incoming;
	const auto * typed = dynamic_cast<const Packet *>(incoming.get());
	if(!typed)
		throw std::runtime_error("Command packet registration mismatch");
	return *typed;
}

class CommandProgressionIntegrationTest : public BattleTestFixture
{
protected:
	SecondarySkill authority() const { return SecondarySkill(SecondarySkill::decode("new-horizons:authority")); }
	SecondarySkill learning() const { return SecondarySkill(SecondarySkill::decode("new-horizons:learning")); }
	void prepare()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(0))
			.heroGarrison({{CreatureID(0), 1}})
			.hero({8, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
			.heroGarrison({{CreatureID(0), 1}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_TRUE(attackerSideHero->getArmyCommandRules());
		ASSERT_EQ(attackerSideHero->getSecSkillLevel(authority()), 0);
		gameHandler->onAdvInterfaceReady(PlayerColor(0));
	}
	void answerOrdinary()
	{
		const auto query = std::dynamic_pointer_cast<CHeroLevelUpDialogQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
		ASSERT_TRUE(query);
		const size_t count = query->hlu.skills.size() + query->hlu.perks.size();
		for(const bool avoidLearning : {true, false})
			for(size_t offset = 0; offset < std::max<size_t>(1, count); ++offset)
			{
				const size_t index = count ? (query->hlu.skills.size() + offset) % count : 0;
				if(index < query->hlu.skills.size() && (query->hlu.skills[index] == authority()
					|| (avoidLearning && query->hlu.skills[index] == learning())))
					continue;
				if(index >= query->hlu.skills.size() && index < count
					&& query->hlu.perks[index - query->hlu.skills.size()].selection.perkId == "new-horizons:learning.prodigy")
					continue;
				if(query->isValidReply(static_cast<int32_t>(index)))
				{
					ASSERT_TRUE(gameHandler->queryReply(query->queryID, static_cast<int32_t>(index), PlayerColor(0)));
					return;
				}
			}
		FAIL() << "No non-Authority ordinary choice in the deterministic fixture";
	}
	void advanceTo(uint32_t level)
	{
		gameHandler->giveExperience(attackerSideHero,
			static_cast<TExpType>(LIBRARY->heroh->reqExp(level)) - attackerSideHero->exp);
		for(size_t budget = 0; budget < 100; ++budget)
		{
			const auto query = gameHandler->queries->topQuery(PlayerColor(0));
			if(!query)
				break;
			if(const auto command = std::dynamic_pointer_cast<CommandEnhancementQuery>(query))
			{
				ASSERT_TRUE(gameHandler->heroCommandEnhancementReply(command->queryID, command->offer.hero,
					command->offer.milestone, static_cast<int>(CommandEnhancement::FORTUNE), PlayerColor(0)));
			}
			else
				ASSERT_NO_FATAL_FAILURE(answerOrdinary());
		}
		ASSERT_EQ(attackerSideHero->level, level);
		ASSERT_FALSE(gameHandler->queries->topQuery(PlayerColor(0)));
	}
};
}

TEST_F(CommandProgressionIntegrationTest, OneExperienceGrantRetainsEachIntermediateAcquisitionLevel)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(advanceTo(23));
	const auto & awards = attackerSideHero->getCommandState().getAwards();
	ASSERT_EQ(awards.size(), 3u);
	for(size_t i = 0; i < awards.size(); ++i)
	{
		EXPECT_EQ(awards[i].milestone, 6 + i);
		EXPECT_EQ(awards[i].acquisitionLevel, 16 + 3 * i);
		EXPECT_EQ(awards[i].enhancement, CommandEnhancement::FORTUNE);
		EXPECT_EQ(awards[i].contribution, i == 2 ? 3 : 2);
	}
	EXPECT_FALSE(attackerSideHero->hasPendingCommandLevelEvent());
}

TEST_F(CommandProgressionIntegrationTest, AuthorityDuringOrdinaryChoiceIsCapturedCoherentlyAndReplyRejectionIsAtomic)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(advanceTo(6));
	gameHandler->giveExperience(attackerSideHero,
		static_cast<TExpType>(LIBRARY->heroh->reqExp(7)) - attackerSideHero->exp);
	ASSERT_EQ(attackerSideHero->level, 7u);
	ASSERT_TRUE(attackerSideHero->hasPendingCommandLevelEvent());
	gameHandler->changeSecSkill(attackerSideHero, authority(), 3, ChangeValueMode::ABSOLUTE);
	EXPECT_FALSE(attackerSideHero->getCommandState().nextPending());
	EXPECT_THROW(attackerSideHero->commandProgressionToJson(), std::runtime_error);
	HeroLevelUp duplicate;
	duplicate.heroId = attackerSideHero->id;
	EXPECT_THROW(gameState()->apply(duplicate), std::runtime_error);
	EXPECT_EQ(attackerSideHero->level, 7u);
	ASSERT_NO_FATAL_FAILURE(answerOrdinary());
	const auto query = std::dynamic_pointer_cast<CommandEnhancementQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
	ASSERT_TRUE(query);
	EXPECT_EQ(query->offer.milestone, 6);
	EXPECT_EQ(query->offer.acquisitionLevel, 7u);
	EXPECT_EQ(query->offer.options[5].contribution, 1);
	const auto before = attackerSideHero->getCommandState();
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, 0, PlayerColor(0)));
	EXPECT_FALSE(gameHandler->heroCommandEnhancementReply(query->queryID, query->offer.hero, 7, 0, PlayerColor(0)));
	EXPECT_FALSE(gameHandler->heroCommandEnhancementReply(query->queryID, query->offer.hero, 6, 9, PlayerColor(0)));
	EXPECT_FALSE(gameHandler->heroCommandEnhancementReply(query->queryID, query->offer.hero, 6, 0, PlayerColor(1)));
	EXPECT_EQ(attackerSideHero->getCommandState(), before);
	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), query);
	HeroCommandEnhancementChosen forged;
	forged.offer = query->offer;
	forged.offer.options[5].contribution = 99;
	forged.choice = 5;
	EXPECT_THROW(gameState()->apply(forged), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getCommandState(), before);
	HeroCommandEnhancementReply reply;
	reply.player = PlayerColor(0);
	reply.qid = query->queryID;
	reply.hero = query->offer.hero;
	reply.milestone = 6;
	reply.choice = 5;
	const auto decoded = wire(reply);
	ASSERT_TRUE(gameHandler->heroCommandEnhancementReply(decoded.qid, decoded.hero, decoded.milestone, decoded.choice, decoded.player));
	EXPECT_FALSE(gameHandler->heroCommandEnhancementReply(decoded.qid, decoded.hero, decoded.milestone, decoded.choice, decoded.player));
	EXPECT_EQ(attackerSideHero->getCommandState().getAwards().front().contribution, 1);
}

TEST_F(CommandProgressionIntegrationTest, PendingAwardSurvivesHeroAndCampaignRoundtripsAndResumesFromSavedValues)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(advanceTo(7));
	gameHandler->changeSecSkill(attackerSideHero, authority(), 3, ChangeValueMode::ABSOLUTE);
	const auto original = std::dynamic_pointer_cast<CommandEnhancementQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
	ASSERT_TRUE(original);
	const auto copiedHero = CMemorySerializer::deepCopy(*attackerSideHero, gameState().get());
	ASSERT_TRUE(copiedHero);
	EXPECT_EQ(copiedHero->getCommandState(), attackerSideHero->getCommandState());
	const auto saved = attackerSideHero->commandProgressionToJson();
	copiedHero->restoreCommandProgression(saved);
	EXPECT_EQ(copiedHero->commandProgressionToJson(), saved);
	CampaignState campaign;
	const auto crossover = campaign.crossoverSerialize(attackerSideHero);
	const auto restored = campaign.crossoverDeserialize(crossover, &gameState()->getMap());
	EXPECT_EQ(restored->getCommandState(), attackerSideHero->getCommandState());
	// Recreate only the simulation handler, just as saved entitlements rebuild queries after load.
	auto resumed = std::make_shared<CGameHandler>(server, gameState());
	resumed->onAdvInterfaceReady(PlayerColor(0));
	const auto query = std::dynamic_pointer_cast<CommandEnhancementQuery>(resumed->queries->topQuery(PlayerColor(0)));
	ASSERT_TRUE(query);
	EXPECT_EQ(query->offer, original->offer);
	HeroCommandEnhancementDialog dialog;
	dialog.queryID = query->queryID;
	dialog.offer = query->offer;
	EXPECT_EQ(wire(dialog).offer, query->offer);
	HeroCommandEnhancementChosen chosen;
	chosen.offer = wire(dialog).offer;
	chosen.choice = 8;
	auto accepted = wire(chosen);
	gameState()->apply(accepted);
	EXPECT_THROW(gameState()->apply(accepted), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getCommandState().getAwards().front().contribution, 5);
}

TEST_F(CommandProgressionIntegrationTest, CoherentEventWireRejectsWrongLevelAndDuplicateWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	HeroLevelUp level;
	level.heroId = attackerSideHero->id;
	level.player = PlayerColor(0);
	gameState()->apply(level);
	HeroCommandProgression event;
	event.hero = attackerSideHero->id;
	event.level = attackerSideHero->level;
	event.authorityRank = attackerSideHero->getPerkSkillRank("new-horizons:authority");
	const auto before = attackerSideHero->getCommandState();
	auto decoded = wire(event);
	++decoded.level;
	EXPECT_THROW(gameState()->apply(decoded), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getCommandState(), before);
	EXPECT_TRUE(attackerSideHero->hasPendingCommandLevelEvent());
	decoded = wire(event);
	gameState()->apply(decoded);
	const auto after = attackerSideHero->getCommandState();
	EXPECT_THROW(gameState()->apply(decoded), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getCommandState(), after);
}

TEST_F(CommandProgressionIntegrationTest, RestoredOfferCannotBeAnsweredBeforeReadinessAndRemainsMandatoryWhenCovered)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(advanceTo(7));
	gameHandler->uiReadyForDialogs.erase(PlayerColor(0));
	gameHandler->changeSecSkill(attackerSideHero, authority(), 3, ChangeValueMode::ABSOLUTE);
	const auto query = std::dynamic_pointer_cast<CommandEnhancementQuery>(gameHandler->queries->topQuery(PlayerColor(0)));
	ASSERT_TRUE(query);
	ASSERT_FALSE(query->wasPrompted());
	const auto before = attackerSideHero->getCommandState();
	EXPECT_FALSE(gameHandler->heroCommandEnhancementReply(query->queryID, query->offer.hero, 6, 0, PlayerColor(0)));
	EXPECT_EQ(attackerSideHero->getCommandState(), before);
	gameHandler->onAdvInterfaceReady(PlayerColor(0));
	ASSERT_TRUE(query->wasPrompted());
	auto covering = std::make_shared<TimerPauseQuery>(gameHandler.get(), PlayerColor(0));
	gameHandler->queries->addQuery(covering);
	EXPECT_FALSE(gameHandler->heroCommandEnhancementReply(query->queryID, query->offer.hero, 6, 0, PlayerColor(0)));
	EXPECT_FALSE(gameHandler->queryReply(query->queryID, 0, PlayerColor(0)));
	EXPECT_EQ(attackerSideHero->getCommandState(), before);
	gameHandler->queries->popQuery(covering);
	EXPECT_EQ(gameHandler->queries->topQuery(PlayerColor(0)), query);
	ASSERT_TRUE(gameHandler->heroCommandEnhancementReply(query->queryID, query->offer.hero, 6, 0, PlayerColor(0)));
}
