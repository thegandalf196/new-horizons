/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/battle/NewHorizonsCommandResources.h"
#include "../../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../../lib/battle/HeroCommandCoefficient.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <limits>

TEST(NewHorizonsCommandResources, LevelRankAndEquipmentCapacityBoundaries)
{
	using newHorizonsCommand::maximum;
	EXPECT_EQ(maximum(1, 0, false, 0), 3);
	EXPECT_EQ(maximum(1, 1, false, 0), 4);
	EXPECT_EQ(maximum(9, 2, false, 0), 5);
	EXPECT_EQ(maximum(10, 2, false, 0), 6);
	EXPECT_EQ(maximum(19, 3, false, 0), 7);
	EXPECT_EQ(maximum(20, 3, false, 0), 8);
	EXPECT_EQ(maximum(200, 3, true, 0), 9);
	EXPECT_EQ(maximum(200, 3, true, 100), 10);
}
TEST(NewHorizonsCommandResources, ExhaustionIsBoundedAndCopiesAreIndependent)
{
	newHorizonsCommand::ResourceState live{true, 3, 3};
	auto projection = live;
	ASSERT_TRUE(projection.spend());
	EXPECT_EQ(live.remaining, 3);
	EXPECT_EQ(projection.remaining, 2);
	ASSERT_TRUE(projection.spend());
	ASSERT_TRUE(projection.spend());
	EXPECT_FALSE(projection.spend());
	EXPECT_EQ(projection.remaining, 0);
	EXPECT_EQ(projection.maximum, 3);
}
TEST(NewHorizonsCommandResources, InvalidStateIsRejected)
{
	for(auto state : {newHorizonsCommand::ResourceState{false, 3, 3},
		{true, 2, 2}, {true, 11, 11}, {true, 3, 4}, {true, 3, -1}})
		EXPECT_THROW(state.validate(), std::runtime_error);
	newHorizonsCommand::ResourceState absent;
	EXPECT_FALSE(absent.spend());
}
TEST(NewHorizonsCommandResources, RoundtripPreservesSpentPoolWithoutRefill)
{
	newHorizonsCommand::ResourceState saved{true, 10, 2};
	CMemorySerializer wire;
	wire.oser & saved;
	newHorizonsCommand::ResourceState restored;
	wire.iser & restored;
	EXPECT_EQ(restored, saved);
	EXPECT_EQ(restored.remaining, 2);
}
TEST(NewHorizonsCommandResources, HeroMoraleEfficiencyUsesPercentagePointsAndCapsSafely)
{
	using newHorizonsCommand::heroicSpiritEfficiencyForMorale;
	EXPECT_EQ(heroicSpiritEfficiencyForMorale(std::numeric_limits<int>::min()), 0);
	EXPECT_EQ(heroicSpiritEfficiencyForMorale(-1), 0);
	EXPECT_EQ(heroicSpiritEfficiencyForMorale(0), 0);
	EXPECT_EQ(heroicSpiritEfficiencyForMorale(1), 5);
	EXPECT_EQ(heroicSpiritEfficiencyForMorale(3), 15);
	EXPECT_EQ(heroicSpiritEfficiencyForMorale(6), 30);
	EXPECT_EQ(heroicSpiritEfficiencyForMorale(std::numeric_limits<int>::max()), 30);
}

class NewHorizonsFiniteCommandTest : public HeroCommandFixture
{
protected:
	NewHorizonsFiniteCommandTest() { useRevisedOrders = true; }
	void commandPerk(const char * id, int rank)
	{
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:command"));
		attackerSideHero->setSecSkillLevel(skill, rank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk("new-horizons:command", id, rank));
		attackerSideHero->applyPerkSelection({"new-horizons:command", id});
	}
};
TEST_F(NewHorizonsFiniteCommandTest, AcceptedOrderPaysOneAndInvalidSecondOrderPaysNothing)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	const auto before = battle()->getCommandResources(BattleSide::ATTACKER);
	ASSERT_TRUE(before.initialized);
	ASSERT_GT(before.remaining, 0);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).remaining, before.remaining - 1);
	const auto paid = battle()->getCommandResources(BattleSide::ATTACKER);
	EXPECT_FALSE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), paid);
}
TEST_F(NewHorizonsFiniteCommandTest, EmptyPoolBlocksOrdersButNotHeroSpellAllowance)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands(true));
	auto exhausted = battle()->getCommandResources(BattleSide::ATTACKER);
	exhausted.remaining = 0;
	battle()->setCommandResources(BattleSide::ATTACKER, exhausted);
	EXPECT_FALSE(battle()->battleCanUseHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::HASTE));
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), exhausted);
}
TEST_F(NewHorizonsFiniteCommandTest, RoundAdvanceDoesNotRegenerateCommand)
{
	ASSERT_NO_FATAL_FAILURE(prepareCommands());
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto paid = battle()->getCommandResources(BattleSide::ATTACKER);
	advanceRound();
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER), paid);
}
TEST_F(NewHorizonsFiniteCommandTest, BattlePlanAddsCapacityWithoutOpeningOrder)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.battlePlan", MasteryLevel::BASIC));
	startBattle();
	beginCombat();
	EXPECT_EQ(battle()->getCommandResources(BattleSide::ATTACKER).maximum, 5);
	EXPECT_FALSE(battle()->getPreCombatOrderState(BattleSide::ATTACKER).orderPending());
	EXPECT_FALSE(heroCommands::hasBattlePlan(attackerSideHero));
}
TEST_F(NewHorizonsFiniteCommandTest, GrandMarshalChangesActualLeadershipOnce)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.battlePlan", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.veteranCommander", MasteryLevel::ADVANCED));
	const auto before = attackerSideHero->getLeadershipCapacity();
	ASSERT_TRUE(before);
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.seizeInitiative", MasteryLevel::EXPERT));
	ASSERT_TRUE(attackerSideHero->getLeadershipCapacity());
	EXPECT_EQ(attackerSideHero->getLeadershipCapacity()->capacity, before->capacity + (before->capacity * 20 + 50) / 100);
	const auto slot = attackerSideHero->getLeadershipSlotCapacity(CreatureID(CreatureID::decode("core:pikeman")));
	ASSERT_TRUE(slot);
	EXPECT_EQ(slot->leadership, attackerSideHero->getLeadershipCapacity()->capacity);
	startBattle();
	EXPECT_FALSE(battle()->getSeizeInitiativeState().enabled());
}
TEST_F(NewHorizonsFiniteCommandTest, MasterCommanderAddsOnlyAttributeEfficiency)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.aggressiveCommander", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.veteranCommander", MasteryLevel::ADVANCED));
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.doubleCommand", MasteryLevel::EXPERT));
	EXPECT_EQ(heroCommands::efficiencyPercent(*attackerSideHero), 145);
	EXPECT_EQ(heroCommands::attributeEfficiencyPercent(*attackerSideHero, PrimarySkill::ATTACK, 30, true), 195);
	EXPECT_EQ(heroCommands::leadershipEfficiencyPercent(*attackerSideHero, 30), 200);
	JsonNode fixed;
	fixed["base"].Integer() = 17;
	fixed["attack"].Integer() = 0;
	fixed["defense"].Integer() = 0;
	EXPECT_EQ(heroCommands::coefficient(fixed, *attackerSideHero, 30), 17);
	EXPECT_FALSE(heroCommands::hasDoubleCommand(attackerSideHero));
}

TEST_F(NewHorizonsFiniteCommandTest, UnbrokenRanksRequiresAnActualUnconsumedBenefit)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.battlePlan", MasteryLevel::BASIC));
	ASSERT_NO_FATAL_FAILURE(commandPerk("new-horizons:command.crisisCommand", MasteryLevel::ADVANCED));
	startBattle();
	beginCombat();
	const auto * recipient = battle()->battleActiveUnit();
	ASSERT_NE(recipient, nullptr);
	EXPECT_FALSE(newHorizonsCommand::unbrokenRanks(*battle(), recipient));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(newHorizonsCommand::unbrokenRanks(*battle(), recipient));
	EXPECT_EQ(newHorizonsCommand::mitigateIncomingDamage(*battle(), recipient, 40), 32);
	EXPECT_EQ(newHorizonsCommand::mitigateIncomingDamage(*battle(), recipient, 4), 3);
	auto charge = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	ASSERT_TRUE(charge);
	charge->consumedUnitIds = {recipient->unitId()};
	battle()->setHeroOrderState(BattleSide::ATTACKER, charge);
	EXPECT_FALSE(newHorizonsCommand::unbrokenRanks(*battle(), recipient));
	// A second genuinely active benefit preserves protection after Charge is consumed.
	auto riposte = *charge;
	riposte.command = HeroCommand::RIPOSTE;
	riposte.consumedUnitIds.clear();
	battle()->setHeroOrderState(BattleSide::ATTACKER, riposte);
	EXPECT_TRUE(newHorizonsCommand::unbrokenRanks(*battle(), recipient));
}
TEST_F(NewHorizonsFiniteCommandTest, HeroicSpiritSnapshotsHeroMoraleAndCreatesNoBonusOrder)
{
	startGame();
	const SecondarySkill discipline(SecondarySkill::decode("new-horizons:discipline"));
	const std::array<const char *, 3> perks = {"new-horizons:discipline.inspirationalLeader",
		"new-horizons:discipline.standardBearer", "new-horizons:discipline.heroicSpirit"};
	for(int rank = 1; rank <= 3; ++rank)
	{
		attackerSideHero->setSecSkillLevel(discipline, rank, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk("new-horizons:discipline", perks[rank - 1], rank));
		attackerSideHero->applyPerkSelection({"new-horizons:discipline", perks[rank - 1]});
	}
	startBattle();
	beginCombat();
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MORALE, BonusSource::OTHER, 4 - attackerSideHero->moraleVal(), BonusSourceID()));
	ASSERT_EQ(attackerSideHero->moraleVal(), 4);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto issued = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	ASSERT_TRUE(issued);
	EXPECT_EQ(issued->heroicSpiritEfficiencyBonusPercent, 20);
	EXPECT_FALSE(issued->heroMorale);
	EXPECT_EQ(issued->heroMoraleActionGrantId, 0);
	EXPECT_FALSE(battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER));
	attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::MORALE, BonusSource::OTHER, -8, BonusSourceID()));
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE)->heroicSpiritEfficiencyBonusPercent, 20);
}

TEST(NewHorizonsCommandResources, FullCurrentCapturesRequireFiniteCommandMarkers)
{
	const JsonNode combat(JsonPath::builtin("config/newHorizonsCombat"));
	auto orders = combat["combat"]["heroCommands"];
	ASSERT_NO_THROW(heroCommands::validateRules(orders));
	orders.Struct().erase("commandResourcePolicyVersion");
	EXPECT_FALSE(newHorizonsCommand::usesRules(orders));
	EXPECT_FALSE(heroCommands::supportedByRules(orders, HeroCommand::CHARGE));
	EXPECT_THROW(heroCommands::validateRules(orders), std::runtime_error);
	auto perks = JsonNode(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_NO_THROW(newHorizonsHeroes::validatePerkRules(perks));
	perks.Struct().erase("commandPolicyVersion");
	EXPECT_THROW(newHorizonsHeroes::validatePerkRules(perks), std::runtime_error);
	EXPECT_NO_THROW(heroCommands::validateRules(JsonNode()));
	EXPECT_NO_THROW(newHorizonsHeroes::validatePerkRules(JsonNode()));
}
TEST(NewHorizonsCommandResources, RetiredOrderSourcesCannotGrantEvenWithoutPolicyMarkers)
{
	HeroActionAllowanceState ledger;
	ledger.resetForRound(1);
	const auto before = ledger;
	for(const auto source : {HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND,
		HeroActionAllowanceState::GrantSource::BATTLE_PLAN, HeroActionAllowanceState::GrantSource::CRISIS_COMMAND,
		HeroActionAllowanceState::GrantSource::HEROIC_SPIRIT})
	{
		// Heroic Spirit requires its receipt API; other retired sources fail current-rules admission.
		if(source == HeroActionAllowanceState::GrantSource::HEROIC_SPIRIT)
		{
			EXPECT_THROW(ledger.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER, source, 1), std::invalid_argument);
		}
		else
		{
			EXPECT_THROW(ledger.grantAllowance(HeroActionAllowanceState::AllowanceKind::ORDER, source, 1), std::runtime_error);
		}
		EXPECT_EQ(ledger, before);
	}
	EXPECT_FALSE(heroCommands::hasDoubleCommand(nullptr));
	EXPECT_FALSE(heroCommands::hasBattlePlan(nullptr));
	newHorizonsCrisisCommand::State retired;
	retired.used[0] = true;
	EXPECT_THROW(retired.validateShape(), std::runtime_error);
}
