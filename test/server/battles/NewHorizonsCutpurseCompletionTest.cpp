/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CPlayerState.h"
#include "../../../lib/entities/hero/NewHorizonsInfernoAdventureSpecialties.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../server/queries/QueriesProcessor.h"

class CutpurseCompletion : public HeroCommandFixture, public testing::WithParamInterface<EBattleResult>
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
	}
};

TEST_P(CutpurseCompletion, AcceptedCompletionTransfersFromPostSurrenderTreasuryBeforeLastHeroRemoval)
{
	startGame();
	attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:octavia")));
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		SetResources resources;
		resources.player = player;
		resources.res[EGameResID::GOLD] = 10000;
		gameHandler->sendAndApply(static_cast<CPackForClient &>(resources));
	}
	ASSERT_EQ(gameState()->getPlayerState(PlayerColor(1))->getHeroes().size(), 1u);
	startBattle();
	beginCombat();
	auto query = std::make_shared<CBattleQuery>(gameHandler.get(), battle());
	gameHandler->queries->addQuery(query);
	int surrenderCost = 0;
	if(GetParam() == EBattleResult::NORMAL)
		gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	else
	{
		for(int count = 0; count < 8 && battle()->battleActiveUnit()
			&& battle()->battleActiveUnit()->unitSide() != BattleSide::DEFENDER; ++count)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->battleGetActionController(active), BattleAction::makeDefend(active)));
		}
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::DEFENDER);
		if(GetParam() == EBattleResult::SURRENDER)
		{
			surrenderCost = battle()->battleGetSurrenderCost(PlayerColor(1));
			ASSERT_GE(surrenderCost, 0);
		}
		const auto action = GetParam() == EBattleResult::SURRENDER
			? BattleAction::makeSurrender(BattleSide::DEFENDER) : BattleAction::makeRetreat(BattleSide::DEFENDER);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1), action));
	}
	ASSERT_TRUE(query->result);
	const auto expected = newHorizonsHeroes::cutpurseGold(attackerSideHero, 10000 - surrenderCost, 10000);
	ASSERT_GT(expected, 0);
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		auto dialog = gameHandler->queries->topQuery(player);
		if(dialog && dialog->getType() == QueryType::BattleDialog)
			ASSERT_TRUE(gameHandler->queryReply(dialog->queryID, 0, player));
	}
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources[EGameResID::GOLD], 10000 + expected);
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(1))->resources[EGameResID::GOLD], 10000 - surrenderCost - expected);
	EXPECT_TRUE(gameState()->getPlayerState(PlayerColor(1))->getHeroes().empty());
}
INSTANTIATE_TEST_SUITE_P(AllResults, CutpurseCompletion,
	testing::Values(EBattleResult::NORMAL, EBattleResult::ESCAPE, EBattleResult::SURRENDER));
