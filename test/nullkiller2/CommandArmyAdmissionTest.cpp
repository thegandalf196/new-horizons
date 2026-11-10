/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../server/battles/BattleTestFixture.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/NewHorizonsRaisedArmy.h"
#include "../../AI/Nullkiller2/Helpers/ArmyFormation.h"
#include "../../AI/Nullkiller2/Helpers/ArmyUpgradeSelection.h"
#include "../../lib/entities/creature/NewHorizonsAssimilation.h"
#include "../../lib/entities/hero/NewHorizonsLeadership.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/bonuses/Bonus.h"
#include "../../lib/networkPacks/StackLocation.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/CPlayerState.h"
#include <limits>

namespace
{
using namespace newHorizonsHeroes;
CreatureID creature(const char * key) { return CreatureID(CreatureID::decode(key)); }

class CommandArmyAdmissionTest : public BattleTestFixture
{
protected:
	void prepare(const char * firstHero = "core:orrin")
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(firstHero)), PlayerColor(0))
			.heroGarrison({{creature("core:pikeman"), 1}})
			.hero({8, 5, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
			.heroGarrison({{creature("core:pikeman"), 1}})
			.town({12, 12, 0}, FactionID::CASTLE, PlayerColor(0)).townGarrison({});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		ASSERT_NE(attackerSideHero, nullptr);
		ASSERT_NE(defenderSideHero, nullptr);
		ASSERT_TRUE(attackerSideHero->getArmyCommand());
		ASSERT_EQ(attackerSideHero->getArmyCommand()->effective(), 1);
	}
	void prepareDisplacement()
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		auto * hero = attackerSideHero;
		hero->level = 4;
		hero->clearSlots();
		ASSERT_TRUE(hero->setCreature(SlotID(1), creature("core:archer"), 2));
		ASSERT_TRUE(hero->setCreature(SlotID(2), creature("core:dwarf"), 3));
		ASSERT_TRUE(hero->setCreature(SlotID(3), creature("core:pikeman"), 1000000));
	}
	static bool sameRows(const std::vector<RecruitmentSlot> & a, const std::vector<RecruitmentSlot> & b)
	{
		if(a.size() != b.size()) return false;
		for(size_t i = 0; i < a.size(); ++i)
			if(a[i].slot != b[i].slot || a[i].creature != b[i].creature || a[i].count != b[i].count) return false;
		return true;
	}
};
}

TEST_F(CommandArmyAdmissionTest, IntrinsicElementalsIgnoreTierButNeverUnlockSlots)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	for(const auto * key : {"core:airElemental", "core:stormElemental", "core:waterElemental", "core:iceElemental",
		"core:fireElemental", "core:energyElemental", "core:earthElemental", "core:magmaElemental",
		"core:psychicElemental", "core:magicElemental"})
	{
		const auto id = creature(key);
		EXPECT_TRUE(newHorizonsCreatures::hasIntrinsicAssimilation(id)) << key;
		EXPECT_EQ(commandArmySlotEligibility(attackerSideHero, SlotID(2), id), CommandPlacementError::NONE) << key;
		EXPECT_EQ(commandArmySlotEligibility(attackerSideHero, SlotID(3), id), CommandPlacementError::LOCKED_SLOT) << key;
		EXPECT_EQ(attackerSideHero->getCommandCreatureTraits(id).category, newHorizonsCreatures::CreatureCategory::ELITE);
	}
	for(const auto * key : {"core:pixie", "core:sprite", "core:phoenix"})
		EXPECT_FALSE(newHorizonsCreatures::hasIntrinsicAssimilation(creature(key)));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::ASSIMILATION, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(commandArmySlotEligibility(attackerSideHero, SlotID(2), creature("core:griffin")),
		CommandPlacementError::TIER_NOT_PERMITTED);
}

TEST_F(CommandArmyAdmissionTest, ResolvedSpecialistFamilyAndUpgradeGainCorePermissionOnly)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:edric"));
	for(const auto * key : {"core:griffin", "core:royalGriffin"})
	{
		EXPECT_TRUE(newHorizonsCreatures::isCommandSpecialistCreature(*attackerSideHero, creature(key)));
		EXPECT_EQ(attackerSideHero->getCommandCreatureTraits(creature(key)).category, newHorizonsCreatures::CreatureCategory::ELITE);
		EXPECT_EQ(commandArmySlotEligibility(attackerSideHero, SlotID(1), creature(key)), CommandPlacementError::NONE);
		EXPECT_EQ(commandArmySlotEligibility(attackerSideHero, SlotID(3), creature(key)), CommandPlacementError::LOCKED_SLOT);
		EXPECT_EQ(commandArmySlotEligibility(defenderSideHero, SlotID(1), creature(key)), CommandPlacementError::TIER_NOT_PERMITTED);
	}
	EXPECT_FALSE(newHorizonsCreatures::isCommandSpecialistCreature(*attackerSideHero, creature("core:monk")));
	ASSERT_TRUE(gameHandler->insertNewStack({attackerSideHero->id, SlotID(1)}, creature("core:griffin").toCreature(), 4000));
	EXPECT_TRUE(gameHandler->changeStackType({attackerSideHero->id, SlotID(1)}, creature("core:royalGriffin").toCreature()));
	EXPECT_FALSE(gameHandler->changeStackType({attackerSideHero->id, SlotID(1)}, creature("core:angel").toCreature()));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), 4000);
}

TEST_F(CommandArmyAdmissionTest, DirectInsertAndReinforcementHaveNoLeadershipCountCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto pikeman = creature("core:pikeman");
	EXPECT_FALSE(attackerSideHero->getLeadershipSlotCapacity(pikeman));
	EXPECT_TRUE(gameHandler->changeStackCount({attackerSideHero->id, SlotID(0)}, 1000000, ChangeValueMode::RELATIVE));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 1000001);
	EXPECT_FALSE(gameHandler->insertNewStack({attackerSideHero->id, SlotID(3)}, pikeman.toCreature(), 1));
	EXPECT_FALSE(attackerSideHero->hasStackAtSlot(SlotID(3)));
	EXPECT_FALSE(gameHandler->changeStackCount({attackerSideHero->id, SlotID(0)},
		std::numeric_limits<TQuantity>::max(), ChangeValueMode::RELATIVE));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 1000001);
}

TEST_F(CommandArmyAdmissionTest, CompleteRewardMovesCoreOutOfOnlyEliteSlotWithoutLosingCounts)
{
	ASSERT_NO_FATAL_FAILURE(prepareDisplacement());
	CCreatureSet incoming;
	ASSERT_TRUE(incoming.setCreature(SlotID(0), creature("core:griffin"), 5000));
	const auto plan = planCommandArmyAddition(*attackerSideHero, *attackerSideHero, incoming);
	ASSERT_TRUE(plan);
	ASSERT_EQ(plan->moves.size(), 2u);
	EXPECT_TRUE(plan->moves.front().withinDestination);
	EXPECT_EQ(plan->moves.front().count, 1000000);
	gameHandler->giveCreatures(attackerSideHero, incoming);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 1000000);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(0))->getId(), creature("core:pikeman"));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(3)), 5000);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(3))->getId(), creature("core:griffin"));
}

TEST_F(CommandArmyAdmissionTest, ImpossibleRewardAndTwoSidedExchangeAreAtomic)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->level = 4;
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(3), creature("core:griffin"), 70));
	// First recipient accepts Pikemen, but the return Griffin is illegal in Core.
	const auto beforeA = commandArmySlots(*attackerSideHero), beforeB = commandArmySlots(*defenderSideHero);
	EXPECT_FALSE(gameHandler->swapStacks({attackerSideHero->id, SlotID(3)}, {defenderSideHero->id, SlotID(0)}));
	EXPECT_TRUE(sameRows(beforeA, commandArmySlots(*attackerSideHero)));
	EXPECT_TRUE(sameRows(beforeB, commandArmySlots(*defenderSideHero)));
	CCreatureSet incoming;
	ASSERT_TRUE(incoming.setCreature(SlotID(0), creature("core:angel"), 50));
	gameHandler->giveCreatures(attackerSideHero, incoming);
	EXPECT_TRUE(sameRows(beforeA, commandArmySlots(*attackerSideHero)));
	EXPECT_EQ(incoming.getStackCount(SlotID(0)), 50);
}

TEST_F(CommandArmyAdmissionTest, SplitRejectsLockedDestinationAndLastSourceCreatureStillCannotLeave)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(gameHandler->moveStack({attackerSideHero->id, SlotID(0)}, {defenderSideHero->id, SlotID(1)}, 1));
	ASSERT_TRUE(gameHandler->changeStackCount({attackerSideHero->id, SlotID(0)}, 9, ChangeValueMode::RELATIVE));
	EXPECT_FALSE(gameHandler->moveStack({attackerSideHero->id, SlotID(0)}, {attackerSideHero->id, SlotID(3)}, 5));
	EXPECT_TRUE(gameHandler->moveStack({attackerSideHero->id, SlotID(0)}, {attackerSideHero->id, SlotID(1)}, 5));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 5);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), 5);
}

TEST_F(CommandArmyAdmissionTest, AdventureProjectionUsesSameDisplacementAndPreservesSourceLastStack)
{
	ASSERT_NO_FATAL_FAILURE(prepareDisplacement());
	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), creature("core:griffin"), 5000));
	ASSERT_TRUE(source.setCreature(SlotID(1), creature("core:pixie"), 1));
	const auto before = commandArmySlots(*attackerSideHero);
	const auto projection = NK2AI::armyFormation::projectArmyExchange(attackerSideHero, &source,
		attackerSideHero, nullptr, {{creature("core:pikeman"), 1000000}, {creature("core:archer"), 2},
			{creature("core:dwarf"), 3}, {creature("core:griffin"), 5000}});
	ASSERT_EQ(projection.receiverSlots.size(), 4u);
	ASSERT_EQ(projection.sourceSlots.size(), 1u);
	EXPECT_EQ(projection.sourceSlots.front().creature, creature("core:pixie"));
	EXPECT_TRUE(sameRows(before, commandArmySlots(*attackerSideHero)));
	for(const auto & row : projection.receiverSlots)
		EXPECT_EQ(commandArmySlotEligibility(attackerSideHero, row.slot, row.creature), CommandPlacementError::NONE);
}

TEST_F(CommandArmyAdmissionTest, RaisedArmyPlanningReusesSlotMatchingAndNonHeroContainersStayUnrestricted)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto plan = newHorizonsNecromancy::planRaisedArmy(*attackerSideHero,
		{{creature("core:skeleton"), 10000}});
	ASSERT_TRUE(plan);
	ASSERT_TRUE(plan->commandArrangement);
	newHorizonsNecromancy::applyRaisedArmy(*gameHandler, *attackerSideHero, *plan);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(1)), 10000);
	CGTownInstance town(nullptr);
	EXPECT_TRUE(armySlotAccepts(&town, SlotID(6), creature("core:angel"), 1000000));
	EXPECT_FALSE(armySlotAccepts(attackerSideHero, SlotID(6), creature("core:angel"), 1));
}

TEST_F(CommandArmyAdmissionTest, NecessaryDuplicateConsolidationFreesLegalSlotWithoutCountFragmentation)
{
	ASSERT_NO_FATAL_FAILURE(prepareDisplacement());
	ASSERT_TRUE(attackerSideHero->setCreature(SlotID(0), creature("core:pikeman"), 12));
	CCreatureSet incoming;
	ASSERT_TRUE(incoming.setCreature(SlotID(0), creature("core:griffin"), 5000));
	const auto plan = planCommandArmyAddition(*attackerSideHero, *attackerSideHero, incoming);
	ASSERT_TRUE(plan);
	gameHandler->giveCreatures(attackerSideHero, incoming);
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(0)), 1000012);
	EXPECT_EQ(attackerSideHero->getCreature(SlotID(3))->getId(), creature("core:griffin"));
	EXPECT_EQ(attackerSideHero->getStackCount(SlotID(3)), 5000);
}

TEST_F(CommandArmyAdmissionTest, RecruitmentRearrangesBeforeDeliveryAndIllegalPurchaseSpendsNothing)
{
	ASSERT_NO_FATAL_FAILURE(prepareDisplacement());
	auto * hero = attackerSideHero;
	auto * town = findFirst<CGTownInstance>();
	ASSERT_NE(town, nullptr);
	ChangeObjPos position;
	position.objid = hero->id;
	position.nPos = town->visitablePos();
	position.initiator = hero->getOwner();
	gameState()->apply(position);
	SetHeroesInTown visitors;
	visitors.tid = town->id;
	visitors.visiting = hero->id;
	visitors.garrison = ObjectInstanceID::NONE;
	gameState()->apply(visitors);
	grantResources(hero->getOwner(), EGameResID::GOLD, 1000000);
	const auto griffin = creature("core:griffin");
	town->creatures = {{100, {griffin}}};
	const auto before = gameState()->getPlayerState(hero->getOwner())->resources;
	const auto cost = town->getTotalRecruitmentCost(griffin, 50, hero);
	ASSERT_TRUE(before.canAfford(cost));
	ASSERT_TRUE(gameHandler->recruitCreatures(town->id, hero->id, griffin, 50, 0, hero->getOwner()));
	EXPECT_EQ(hero->getStackCount(SlotID(0)), 1000000);
	EXPECT_EQ(hero->getCreature(SlotID(3))->getId(), griffin);
	EXPECT_EQ(hero->getStackCount(SlotID(3)), 50);
	EXPECT_EQ(town->creatures.front().first, 50u);
	EXPECT_EQ(gameState()->getPlayerState(hero->getOwner())->resources, before - cost);
	town->creatures = {{100, {creature("core:angel")}}};
	const auto beforeArmy = commandArmySlots(*hero);
	const auto beforeRejection = gameState()->getPlayerState(hero->getOwner())->resources;
	EXPECT_FALSE(gameHandler->recruitCreatures(town->id, hero->id, creature("core:angel"), 1, 0, hero->getOwner()));
	EXPECT_EQ(gameState()->getPlayerState(hero->getOwner())->resources, beforeRejection);
	EXPECT_EQ(town->creatures.front().first, 100u);
	EXPECT_TRUE(sameRows(beforeArmy, commandArmySlots(*hero)));
}
