/*
 * Part of VCMI / New Horizons
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsReclaimerFixture.h"
#include "../../../server/battles/BattleProcessor.h"
#include "../../../server/queries/QueriesProcessor.h"
#include "../../../server/queries/BattleQueries.h"
#include "../../../client/NewHorizonsNecromancyText.h"
#include "../../../lib/texts/MetaString.h"
#include "../../../lib/texts/CGeneralTextHandler.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
TEST_F(NewHorizonsReclaimerFixture, OwnedBaseAndUpgradeDwellingsUnlockWithoutSpendingEmptyStock)
{
	EXPECT_FALSE(newHorizonsHeroes::ownsZombieDwelling(owner(), *attackerSideHero));
	dwelling(*nearTown);
	EXPECT_TRUE(newHorizonsHeroes::ownsZombieDwelling(owner(), *attackerSideHero));
	EXPECT_EQ(newHorizonsHeroes::reclaimerConversionPercent(*attackerSideHero, owner()), 100);
	EXPECT_EQ(nearTown->creatures[1].first, 0);
	nearTown->removeAllBuildings();
	dwelling(*nearTown, true);
	EXPECT_TRUE(newHorizonsHeroes::ownsZombieDwelling(owner(), *attackerSideHero));
	EXPECT_EQ(nearTown->creatures[1].first, 0);
}
TEST_F(NewHorizonsReclaimerFixture, ActualTownOwnershipAndLostDwellingRemoveUnlock)
{
	dwelling(*nearTown);
	ASSERT_TRUE(newHorizonsHeroes::ownsZombieDwelling(owner(), *attackerSideHero));
	nearTown->tempOwner = PlayerColor(1); // Even a stale owner-list entry cannot authorize a foreign town.
	EXPECT_FALSE(newHorizonsHeroes::ownsZombieDwelling(owner(), *attackerSideHero));
	EXPECT_EQ(newHorizonsHeroes::reclaimerConversionPercent(*attackerSideHero, owner()), 0);
	nearTown->tempOwner = PlayerColor(0);
	nearTown->removeAllBuildings();
	EXPECT_FALSE(newHorizonsHeroes::ownsZombieDwelling(owner(), *attackerSideHero));
}
TEST_F(NewHorizonsReclaimerFixture, CapturedRulesLevelAndActiveNecromancyGateSpecialty)
{
	dwelling(*nearTown);
	attackerSideHero->level = 5;
	EXPECT_EQ(newHorizonsHeroes::reclaimerConversionPercent(*attackerSideHero, owner()), 25);
	attackerSideHero->level = 100;
	EXPECT_EQ(newHorizonsHeroes::reclaimerConversionPercent(*attackerSideHero, owner()), 100);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:necromancy")),
		0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(newHorizonsHeroes::reclaimerConversionPercent(*attackerSideHero, owner()), 0);
	EXPECT_EQ(newHorizonsHeroes::reclaimerConversionPercent(*defenderSideHero, owner()), 0);
}
TEST_F(NewHorizonsReclaimerFixture, EnhancedHeroBatchHasPriorityAndPredictionDoesNotMutate)
{
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), id("core:skeleton"), 1));
	const auto rng = rngState();
	const auto world = gameState()->saveReplaySnapshot();
	const auto beforeCount = attackerSideHero->getStackCount(SlotID(0));
	const auto selected = newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), plans());
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->army, attackerSideHero);
	EXPECT_EQ(selected->summary.reclaimerZombiesRaised, 2);
	EXPECT_EQ(selected->summary.ossuaryTown, ObjectInstanceID::NONE);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), beforeCount);
	EXPECT_EQ(attackerSideHero->stacksCount(), 1);
	EXPECT_EQ(rngState(), rng);
	EXPECT_EQ(gameState()->saveReplaySnapshot(), world);
}
TEST_F(NewHorizonsReclaimerFixture, CompleteOrdinaryHeroBatchWinsBeforeEnhancedOssuary)
{
	fullArmy(*attackerSideHero, true);
	const auto selected = newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), plans());
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->army, attackerSideHero);
	EXPECT_EQ(selected->summary.reclaimerZombiesRaised, 0);
	EXPECT_EQ(selected->summary.skeletonsRaised, 6);
	EXPECT_EQ(selected->summary.ossuaryTown, ObjectInstanceID::NONE);
}
TEST_F(NewHorizonsReclaimerFixture, EnhancedNearestOssuaryIsChosenOnlyWhenHeroCannotAcceptEitherBatch)
{
	fullArmy(*attackerSideHero, false);
	const auto selected = newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), plans());
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->army, nearTown->getUpperArmy());
	EXPECT_EQ(selected->summary.reclaimerZombiesRaised, 2);
	EXPECT_EQ(selected->summary.ossuaryTown, nearTown->id);
}
TEST_F(NewHorizonsReclaimerFixture, OssuaryOrdinaryFallbackKeepsEntireRemainderAndDoesNotSearchFarther)
{
	fullArmy(*attackerSideHero, false);
	fullArmy(*nearTown, true); // Town Skeleton count is unbounded; it has no free Zombie slot.
	const auto ordinary = newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), plans());
	ASSERT_TRUE(ordinary);
	EXPECT_EQ(ordinary->army, nearTown->getUpperArmy());
	EXPECT_EQ(ordinary->summary.reclaimerZombiesRaised, 0);
	EXPECT_EQ(ordinary->summary.skeletonsRaised, 6);
	nearTown->clearSlots();
	const std::array<const char *, 7> filler = {"core:pikeman", "core:archer", "core:swordsman",
		"core:griffin", "core:monk", "core:cavalier", "core:halberdier"};
	for(int i = 0; i < 7; ++i) ASSERT_TRUE(nearTown->setCreature(SlotID(i), id(filler[i]), 1));
	EXPECT_FALSE(newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), plans()));
	EXPECT_EQ(farTown->stacksCount(), 0); // A farther empty town is deliberately not searched.
}
TEST_F(NewHorizonsReclaimerFixture, ActualOssuaryPerkIsRequiredByAuthoritativePreparation)
{
	dwelling(*nearTown);
	BattleResult result;
	result.winner = BattleSide::ATTACKER;
	result.necromancyEligibilityCaptured = true;
	result.necromancyEligibleCasualties[BattleSide::DEFENDER][id("core:pikeman")] = 60;
	const auto prepared = newHorizonsNecromancy::prepareBattleNecromancy(result,
		gameState()->getCreatureCategoryRules(), 0, *attackerSideHero, owner());
	EXPECT_FALSE(prepared.ossuary);
	EXPECT_GT(prepared.enhanced.reclaimerZombiesRaised, 0);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::DARK_CONVERSION_ID));
	EXPECT_FALSE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
		newHorizonsNecromancy::CORPSE_PRESERVATION_ID));
}
TEST_F(NewHorizonsReclaimerFixture, BothZombieSourcesHaveDistinctActualExplanationCounts)
{
	auto ordinary = newHorizonsNecromancy::resolve(2, 200, 60, false, false, true,
		true, true, 0, 100, 30, true, true);
	const auto enhanced = newHorizonsNecromancy::reclaimResidualSkeletons(ordinary, 50, 0, 100);
	ASSERT_EQ(enhanced.reclaimerZombiesRaised, 3);
	ASSERT_EQ(enhanced.zombiesRaised, 7);
	MetaString text;
	necromancyPresentation::appendConversions(text, enhanced);
	const auto actual = text.toString(LIBRARY->generaltexth.get());
	EXPECT_NE(actual.find("Converted: 4"), std::string::npos);
	EXPECT_NE(actual.find("Dark Reclaimer: 3"), std::string::npos);
	EXPECT_EQ(actual.find("Converted: 7"), std::string::npos);
}
TEST_F(NewHorizonsReclaimerFixture, OwnRecoveryReadbackPreservesFormsAndExcludesFreshConversions)
{
	newHorizonsNecromancy::NecromancyResult result;
	result.skeletonCreature = id("core:skeletonWarrior");
	result.skeletonsRaised = 11;
	result.zombiesRaised = 7;
	result.reclaimerZombiesRaised = 7;
	result.recoveredSkeletons = 3;
	result.recoveredSkeletonWarriors = 2;
	result.manaRecovered = 1;
	const auto outputs = necromancyPresentation::recoveryOutputs(result);
	ASSERT_EQ(outputs.size(), 2);
	EXPECT_EQ(outputs[0].creature, id("core:skeleton"));
	EXPECT_EQ(outputs[0].count, 3);
	EXPECT_EQ(outputs[1].creature, id("core:skeletonWarrior"));
	EXPECT_EQ(outputs[1].count, 2);
	MetaString recovery;
	necromancyPresentation::appendRecovery(recovery, result);
	const auto restored = recovery.toString(LIBRARY->generaltexth.get());
	EXPECT_NE(restored.find("Grave Knowledge restored to army: 3 "), std::string::npos);
	EXPECT_NE(restored.find(" and 2 "), std::string::npos);
	EXPECT_NE(restored.find("from your own fallen troops."), std::string::npos);
	EXPECT_EQ(restored.find("11"), std::string::npos);
	EXPECT_EQ(restored.find("Mana"), std::string::npos);
	MetaString fresh;
	necromancyPresentation::appendFreshDelivery(fresh, result, "");
	const auto delivered = fresh.toString(LIBRARY->generaltexth.get());
	EXPECT_NE(delivered.find("Delivered to army: 11 "), std::string::npos);
	EXPECT_NE(delivered.find(" and 7 "), std::string::npos);
	EXPECT_EQ(delivered.find("own fallen"), std::string::npos);
	EXPECT_EQ(result.recoveredSkeletons, 3);
	EXPECT_EQ(result.recoveredSkeletonWarriors, 2);
	EXPECT_EQ(result.manaRecovered, 1);
}
TEST_F(NewHorizonsReclaimerFixture, RecoveryOnlyDoesNotInventFreshOssuaryOutput)
{
	newHorizonsNecromancy::NecromancyResult result;
	result.recoveredSkeletonWarriors = 4;
	MetaString fresh;
	necromancyPresentation::appendFreshDelivery(fresh, result, "Example town");
	EXPECT_TRUE(fresh.toString(LIBRARY->generaltexth.get()).empty());
	const auto outputs = necromancyPresentation::recoveryOutputs(result);
	ASSERT_EQ(outputs.size(), 1);
	EXPECT_EQ(outputs[0].creature, id("core:skeletonWarrior"));
	EXPECT_EQ(outputs[0].count, 4);
}
TEST_F(NewHorizonsReclaimerFixture, HistoricalZeroRecoveryAddsNoRowOrComponent)
{
	newHorizonsNecromancy::NecromancyResult result;
	result.skeletonsRaised = 20;
	result.graveKnowledgeCasualties = 100;
	result.graveKnowledgeSkeletons = 20;
	EXPECT_TRUE(necromancyPresentation::recoveryOutputs(result).empty());
	MetaString recovery;
	necromancyPresentation::appendRecovery(recovery, result);
	EXPECT_TRUE(recovery.toString(LIBRARY->generaltexth.get()).empty());
}
class NewHorizonsReclaimerAbsentFixture : public NewHorizonsReclaimerFixture
{
public:
	NewHorizonsReclaimerAbsentFixture() { capturedPolicy = false; }
};
TEST_F(NewHorizonsReclaimerAbsentFixture, InstalledDwellingDoesNotInventHistoricalCapture)
{
	ASSERT_TRUE(gameState()->getMap().getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS)["reclamationSpecialty"].isNull());
	ASSERT_TRUE(gameState()->getHeroDevelopmentRules()["reclamationSpecialty"].isNull());
	dwelling(*nearTown);
	EXPECT_FALSE(attackerSideHero->getPrimaryGrowthRules().Struct().contains("reclamationSpecialty"));
	EXPECT_EQ(newHorizonsHeroes::reclaimerConversionPercent(*attackerSideHero, owner()), 0);
}
TEST_F(NewHorizonsReclaimerFixture, ComputerWinnerUsesFinalEnhancedBatchThroughAuthoritativeResult)
{
	dwelling(*nearTown);
	ASSERT_FALSE(gameState()->getPlayerState(PlayerColor(0))->isHuman());
	attackerSideHero->clearSlots();
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), id("core:skeleton"), 1));
	defenderSideHero->clearSlots();
	ASSERT_TRUE(defenderSideHero->setCreature(SlotID(0), id("core:pikeman"), 60));
	gameHandler->battles->startBattle(attackerSideHero, defenderSideHero);
	ASSERT_NE(gameState()->getBattle(PlayerColor(0)), nullptr);
	gameHandler->battles->cheatBattleVictory(PlayerColor(0));
	for(const auto player : {PlayerColor(0), PlayerColor(1)})
	{
		const auto query = gameHandler->queries->topQuery(player);
		if(query && query->getType() == QueryType::BattleDialog)
			ASSERT_TRUE(gameHandler->queryReply(query->queryID, 0, player));
	}
	ASSERT_EQ(recorded.results.size(), 1u);
	const auto & actual = recorded.results.front().necromancy;
	EXPECT_GT(actual.reclaimerZombiesRaised, 0);
	EXPECT_EQ(actual.zombiesRaised, actual.reclaimerZombiesRaised);
	EXPECT_TRUE(actual.isReclaimerSummaryValid());
	EXPECT_FALSE(actual.darkConversionChosen);
	EXPECT_EQ(actual.ossuaryTown, ObjectInstanceID::NONE);
	EXPECT_EQ(gameState()->getBattle(PlayerColor(0)), nullptr);
	int totalZombies = 0;
	for(const auto & [slot, stack] : attackerSideHero->Slots())
		if(stack->getCreatureID() == id("core:zombie")) totalZombies += stack->getCount();
	EXPECT_EQ(totalZombies, actual.zombiesRaised);
}

TEST_F(NewHorizonsReclaimerFixture, OrdinaryFallbackRestoresBlackHarvestDerivedFromFinalHeadcount)
{
	fullArmy(*attackerSideHero, true);
	const auto selected = newHorizonsNecromancy::selectRaisedArmyDelivery(*attackerSideHero, owner(), plans(120));
	ASSERT_TRUE(selected);
	EXPECT_EQ(selected->army, attackerSideHero);
	EXPECT_EQ(selected->summary.reclaimerZombiesRaised, 0);
	EXPECT_EQ(selected->summary.skeletonsRaised, 12);
	EXPECT_EQ(selected->summary.manaRecovered, 1);
}
