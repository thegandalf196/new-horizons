/*
 * ArmyFormationLeadershipTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the GNU General Public License can be found in license.txt
 */
#include "StdInc.h"

#include "AI/Nullkiller2/Analyzers/ArmyManager.h"
#include "AI/Nullkiller2/Engine/Nullkiller.h"
#include "AI/Nullkiller2/Helpers/ArmyFormation.h"
#include "nullkiller2/NullkillerTest.h"

#include "lib/mapObjects/CGHeroInstance.h"
#include "lib/networkPacks/PacksForClient.h"

namespace
{
const PlayerColor PLAYER(0);
const int3 HERO_POS(5, 5, 0);
const int3 SOURCE_HERO_POS(8, 5, 0);

class NewHorizonsArmyFormationLeadershipTest : public NullkillerTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	void startGame(std::vector<std::pair<CreatureID, uint16_t>> heroArmy)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.playerActive(PLAYER)
			.hero(HERO_POS, HeroTypeID(0), PLAYER)
			.heroGarrison(std::move(heroArmy));
		startWithMap(std::move(builder));
	}

	void startExchangeGame(
		std::vector<std::pair<CreatureID, uint16_t>> receiverArmy,
		std::vector<std::pair<CreatureID, uint16_t>> sourceArmy,
		uint32_t receiverExperience = 0)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder
			.size(36, false)
			.playerActive(PLAYER)
			.hero(HERO_POS, HeroTypeID(0), PLAYER)
			.heroGarrison(std::move(receiverArmy));
		if(receiverExperience)
			builder.heroExperience(receiverExperience);
		builder
			.hero(SOURCE_HERO_POS, HeroTypeID(1), PLAYER)
			.heroGarrison(std::move(sourceArmy));
		startWithMap(std::move(builder));
	}
};

int projectedCountAt(const std::vector<NK2AI::armyFormation::ProjectedArmyStack> & army, SlotID slot)
{
	const auto found = std::ranges::find_if(army, [slot](const auto & stack) { return stack.slot == slot; });
	return found == army.end() ? 0 : found->count;
}
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, MergePreflightRejectsAnOversizedIncomingStack)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startGame({{pikeman, 17}});

	const auto * destination = findHeroAt(HERO_POS);
	ASSERT_NE(destination, nullptr);
	const auto capacity = destination->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_EQ(capacity->maximum, 17);

	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 1));

	EXPECT_FALSE(NK2AI::armyFormation::canMergeOrSwapStacks(
		&source, destination, SlotID(0), SlotID(0)));
	EXPECT_TRUE(NK2AI::armyFormation::canReceiveStack(destination, pikeman, capacity->maximum));
	EXPECT_FALSE(NK2AI::armyFormation::canReceiveStack(destination, pikeman, capacity->maximum + 1));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, WholeArmyMergePreflightRejectsAnOversizedGarrisonSwap)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startGame({{pikeman, 17}});

	const auto * destination = findHeroAt(HERO_POS);
	ASSERT_NE(destination, nullptr);
	const auto capacity = destination->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_EQ(capacity->maximum, 17);

	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 1));

	EXPECT_FALSE(NK2AI::armyFormation::canMergeArmies(&source, destination));

	source.clearSlots();
	EXPECT_TRUE(NK2AI::armyFormation::canMergeArmies(&source, destination));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, GarrisonSwapPreflightChecksTheActualTownArmy)
{
	const CreatureID gog(CreatureID::decode("core:gog"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.playerActive(PLAYER)
		.town({9, 5, 0}, FactionID::INFERNO, PLAYER)
		.townGarrison({{gog, 1}})
		.hero({5, 5, 0}, HeroTypeID(0), PLAYER)
		.heroGarrison({});
	startWithMap(std::move(builder));

	auto * town = findFirst<CGTownInstance>();
	auto * visitingHero = findHeroByOwner(PLAYER);
	ASSERT_NE(town, nullptr);
	ASSERT_NE(visitingHero, nullptr);

	// Reproduce the state used by GarrisonHeroSwap: the visiting hero is
	// empty, while the town's army contains more than the hero can command.
	ChangeObjPos moveHero;
	moveHero.objid = visitingHero->id;
	moveHero.nPos = town->visitablePos();
	moveHero.initiator = PLAYER;
	gameState()->apply(moveHero);
	SetHeroesInTown setHeroes;
	setHeroes.tid = town->id;
	setHeroes.visiting = visitingHero->id;
	setHeroes.garrison = ObjectInstanceID::NONE;
	gameState()->apply(setHeroes);

	const auto capacity = visitingHero->getLeadershipSlotCapacity(gog);
	ASSERT_TRUE(capacity);
	town->setStackCount(SlotID(0), capacity->maximum + 1);

	EXPECT_FALSE(NK2AI::armyFormation::canSwapGarrisonHero(town));
	EXPECT_FALSE(NK2AI::armyFormation::canMergeArmies(town, visitingHero));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, SplitPreflightAllowsOnlyTheLegalFinalDestinationCount)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startGame({{pikeman, 16}});

	const auto * destination = findHeroAt(HERO_POS);
	ASSERT_NE(destination, nullptr);
	const auto capacity = destination->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);

	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 4));
	const auto destinationSlot = destination->getSlotFor(pikeman);
	ASSERT_TRUE(destinationSlot.validSlot());

	EXPECT_EQ(NK2AI::armyFormation::maxLegalTransferCount(
		&source, destination, SlotID(0), destinationSlot), 1);
	EXPECT_TRUE(NK2AI::armyFormation::canSplitStack(
		&source, destination, SlotID(0), destinationSlot, 17));
	EXPECT_FALSE(NK2AI::armyFormation::canSplitStack(
		&source, destination, SlotID(0), destinationSlot, 18));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, LegacyHeroesRemainUnrestrictedByThePreflight)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID archer(CreatureID::decode("core:archer"));
	CGHeroInstance destination(nullptr);
	CGHeroInstance source(nullptr);
	ASSERT_TRUE(destination.setCreature(SlotID(0), pikeman, 100));
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 100));
	ASSERT_TRUE(source.setCreature(SlotID(1), archer, 1));

	EXPECT_TRUE(NK2AI::armyFormation::canMergeOrSwapStacks(
		&source, &destination, SlotID(0), SlotID(0)));
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, ReinforcementValuationReturnsZeroWhenEverySameTypeSlotIsAtCapacity)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startExchangeGame({}, {{pikeman, 4}});

	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	const auto sourceCapacity = source->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(sourceCapacity);
	ASSERT_GE(sourceCapacity->maximum, 4);

	for(size_t i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
		ASSERT_TRUE(capacity);
		ASSERT_GT(capacity->maximum, 0);
		ASSERT_TRUE(receiver->setCreature(SlotID(i), pikeman, capacity->maximum));
	}
	ASSERT_EQ(receiver->stacksCount(), GameConstants::ARMY_SIZE);

	const auto gateway = makeGateway(PLAYER);
	EXPECT_EQ(gateway->nullkiller->armyManager->howManyReinforcementsCanGet(receiver, source), 0);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, ReinforcementValuationPreservesPerSlotLimitsAcrossDuplicateStacks)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startExchangeGame({}, {{pikeman, 4}});

	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	const auto pikemanCapacity = receiver->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(pikemanCapacity);
	ASSERT_GT(pikemanCapacity->maximum, 1);
	const auto sourceCapacity = source->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(sourceCapacity);
	ASSERT_GE(sourceCapacity->maximum, 4);

	ASSERT_TRUE(receiver->setCreature(SlotID(0), pikeman, pikemanCapacity->maximum - 1));
	for(size_t i = 1; i < GameConstants::ARMY_SIZE; ++i)
	{
		const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
		ASSERT_TRUE(capacity);
		ASSERT_GT(capacity->maximum, 0);
		ASSERT_TRUE(receiver->setCreature(SlotID(i), pikeman, capacity->maximum));
	}
	ASSERT_EQ(receiver->stacksCount(), GameConstants::ARMY_SIZE);

	const auto gateway = makeGateway(PLAYER);
	EXPECT_EQ(
		gateway->nullkiller->armyManager->howManyReinforcementsCanGet(receiver, source),
		pikeman.toCreature()->getAIValue());
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, EmptyNewHorizonsHeroReceivesOnlyLeadershipLegalStacksFromNonHeroDonor)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.playerActive(PLAYER)
		.hero(HERO_POS, HeroTypeID(0), PLAYER)
		.town({9, 5, 0}, FactionID::CASTLE, PLAYER)
		.townGarrison({{pikeman, 1}});
	startWithMap(std::move(builder));

	auto * receiver = findHeroAt(HERO_POS);
	auto * donor = findFirst<CGTownInstance>();
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(donor, nullptr);
	receiver->clearSlots();
	ASSERT_EQ(receiver->stacksCount(), 0);
	const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 0);
	const int availableCount = capacity->maximum * GameConstants::ARMY_SIZE + 5;
	donor->setStackCount(SlotID(0), availableCount);

	const auto gateway = makeGateway(PLAYER);
	const auto army = gateway->nullkiller->armyManager->getBestArmy(
		receiver, receiver, donor, TerrainId::NONE);

	ASSERT_EQ(army.size(), GameConstants::ARMY_SIZE);
	int projectedCount = 0;
	for(const auto & stack : army)
	{
		EXPECT_EQ(stack.creature->getId(), pikeman);
		EXPECT_LE(stack.count, capacity->maximum);
		projectedCount += stack.count;
	}
	EXPECT_EQ(projectedCount, capacity->maximum * GameConstants::ARMY_SIZE);
	EXPECT_EQ(
		gateway->nullkiller->armyManager->howManyReinforcementsCanGet(receiver, donor),
		static_cast<uint64_t>(projectedCount) * pikeman.toCreature()->getAIValue());
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, ExchangeProjectionKeepsDuplicateCreatureSlotLimitsIndependent)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startExchangeGame({}, {{pikeman, 4}});

	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();
	ASSERT_TRUE(source->setCreature(SlotID(0), pikeman, 4));
	const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_GT(capacity->maximum, 1);
	ASSERT_TRUE(receiver->setCreature(SlotID(0), pikeman, capacity->maximum));
	ASSERT_TRUE(receiver->setCreature(SlotID(1), pikeman, capacity->maximum - 1));

	const std::vector<NK2AI::armyFormation::DesiredArmyStack> desired = {
		{pikeman, capacity->maximum},
		{pikeman, capacity->maximum}
	};
	const auto projection = NK2AI::armyFormation::projectArmyExchange(
		receiver, source, receiver, source, desired);

	ASSERT_EQ(projection.transfers.size(), 1);
	EXPECT_EQ(projection.transfers.front().sourceSide, NK2AI::armyFormation::ArmyExchangeSide::SOURCE);
	EXPECT_EQ(projection.transfers.front().destinationSlot, SlotID(1));
	EXPECT_EQ(projection.transfers.front().transferCount, 1);
	EXPECT_EQ(projectedCountAt(projection.receiverSlots, SlotID(0)), capacity->maximum);
	EXPECT_EQ(projectedCountAt(projection.receiverSlots, SlotID(1)), capacity->maximum);
	EXPECT_EQ(projectedCountAt(projection.sourceSlots, SlotID(0)), 3);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, ExchangeProjectionRetainsTheDonorsLastStack)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startExchangeGame({{pikeman, 1}}, {{pikeman, 2}});

	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();
	ASSERT_TRUE(receiver->setCreature(SlotID(0), pikeman, 1));
	ASSERT_TRUE(source->setCreature(SlotID(0), pikeman, 2));
	const std::vector<NK2AI::armyFormation::DesiredArmyStack> desired = {{pikeman, 3}};
	const auto projection = NK2AI::armyFormation::projectArmyExchange(
		receiver, source, receiver, source, desired);

	ASSERT_EQ(projection.transfers.size(), 1);
	EXPECT_EQ(projection.transfers.front().transferCount, 1);
	EXPECT_EQ(projectedCountAt(projection.receiverSlots, SlotID(0)), 2);
	EXPECT_EQ(projectedCountAt(projection.sourceSlots, SlotID(0)), 1);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, ReprojectingDuplicateResidualSlotsCanLoseTheValuedSwap)
{
	const CreatureID angel(CreatureID::decode("core:angel"));
	const CreatureID phoenix(CreatureID::decode("core:phoenix"));
	const std::array<CreatureID, 5> fillers = {
		CreatureID(CreatureID::decode("core:cavalier")),
		CreatureID(CreatureID::decode("core:blackKnight")),
		CreatureID(CreatureID::decode("core:wyvern")),
		CreatureID(CreatureID::decode("core:efreet")),
		CreatureID(CreatureID::decode("core:cyclop"))
	};
	const CreatureID largerFiller(CreatureID::decode("core:magicElemental"));

	// Coronius has 650 Leadership and Yog has 1100. Both can command one
	// Angel/Phoenix, but Yog can receive two of each filler while Coronius can
	// receive only one. The donor's full seven slots therefore cannot accept the
	// second Angel after the first Angel is swapped out for the Phoenix.
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder
		.size(36, false)
		.playerActive(PLAYER)
		.hero(HERO_POS, HeroTypeID(HeroTypeID::decode("core:coronius")), PLAYER)
		.hero(SOURCE_HERO_POS, HeroTypeID(HeroTypeID::decode("core:yog")), PLAYER);
	startWithMap(std::move(builder));

	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();

	const auto receiverAngelCapacity = receiver->getLeadershipSlotCapacity(angel);
	const auto sourceAngelCapacity = source->getLeadershipSlotCapacity(angel);
	const auto receiverPhoenixCapacity = receiver->getLeadershipSlotCapacity(phoenix);
	const auto sourcePhoenixCapacity = source->getLeadershipSlotCapacity(phoenix);
	ASSERT_TRUE(receiverAngelCapacity);
	ASSERT_TRUE(sourceAngelCapacity);
	ASSERT_TRUE(receiverPhoenixCapacity);
	ASSERT_TRUE(sourcePhoenixCapacity);
	ASSERT_EQ(receiverAngelCapacity->maximum, 1);
	ASSERT_EQ(sourceAngelCapacity->maximum, 1);
	ASSERT_EQ(receiverPhoenixCapacity->maximum, 1);
	ASSERT_EQ(sourcePhoenixCapacity->maximum, 1);

	ASSERT_TRUE(receiver->setCreature(SlotID(0), angel, 1));
	ASSERT_TRUE(receiver->setCreature(SlotID(1), angel, 1));
	ASSERT_TRUE(source->setCreature(SlotID(0), phoenix, 1));
	std::vector<NK2AI::armyFormation::DesiredArmyStack> desired = {{phoenix, 1}};
	for(size_t i = 0; i < fillers.size(); ++i)
	{
		const auto receiverCapacity = receiver->getLeadershipSlotCapacity(fillers[i]);
		const auto sourceCapacity = source->getLeadershipSlotCapacity(fillers[i]);
		ASSERT_TRUE(receiverCapacity);
		ASSERT_TRUE(sourceCapacity);
		ASSERT_EQ(receiverCapacity->maximum, 1);
		ASSERT_EQ(sourceCapacity->maximum, 2);
		ASSERT_TRUE(receiver->setCreature(SlotID(static_cast<int>(i + 2)), fillers[i], 1));
		ASSERT_TRUE(source->setCreature(SlotID(static_cast<int>(i + 1)), fillers[i], 2));
		desired.push_back({fillers[i], 1});
	}
	const auto receiverLargerFillerCapacity = receiver->getLeadershipSlotCapacity(largerFiller);
	const auto sourceLargerFillerCapacity = source->getLeadershipSlotCapacity(largerFiller);
	ASSERT_TRUE(receiverLargerFillerCapacity);
	ASSERT_TRUE(sourceLargerFillerCapacity);
	ASSERT_EQ(receiverLargerFillerCapacity->maximum, 2);
	ASSERT_EQ(sourceLargerFillerCapacity->maximum, 3);
	ASSERT_TRUE(source->setCreature(SlotID(6), largerFiller, 3));
	ASSERT_EQ(receiver->stacksCount(), GameConstants::ARMY_SIZE);
	ASSERT_EQ(source->stacksCount(), GameConstants::ARMY_SIZE);

	const auto projection = NK2AI::armyFormation::projectArmyExchange(
		receiver, source, receiver, source, desired);
	ASSERT_EQ(projection.transfers.size(), 1);
	EXPECT_TRUE(projection.transfers.front().swapsStacks);
	EXPECT_EQ(projection.transfers.front().sourceSide, NK2AI::armyFormation::ArmyExchangeSide::SOURCE);
	EXPECT_EQ(projection.transfers.front().expectedSourceCreature, phoenix);
	EXPECT_EQ(projection.transfers.front().expectedDestinationCreature, angel);
	EXPECT_EQ(projection.transfers.front().destinationSlot, SlotID(0));
	EXPECT_EQ(projection.receiverSlots.front().creature, phoenix);
	EXPECT_EQ(projectedCountAt(projection.receiverSlots, SlotID(1)), 1);
	EXPECT_EQ(projection.receiverSlots[1].creature, angel);
	EXPECT_EQ(projection.sourceSlots.front().creature, angel);
	EXPECT_GT(phoenix.toCreature()->getAIValue(), angel.toCreature()->getAIValue());

	// ArmyManager returns physical receiver slots from this first projection.
	// Reinterpreting those slots as a new desired army loses the original swap:
	// the surviving duplicate Angel makes the other Angel stack look desired,
	// so the full donor cannot make room for the Phoenix on a second pass.
	std::vector<NK2AI::armyFormation::DesiredArmyStack> projectedDesired;
	for(const auto & stack : projection.receiverSlots)
		projectedDesired.push_back({stack.creature, stack.count});
	const auto secondProjection = NK2AI::armyFormation::projectArmyExchange(
		receiver, source, receiver, source, projectedDesired);
	EXPECT_TRUE(secondProjection.transfers.empty());
	ASSERT_EQ(secondProjection.receiverSlots.front().creature, angel);
	EXPECT_EQ(secondProjection.receiverSlots.front().count, 1);
	EXPECT_EQ(projectedCountAt(secondProjection.receiverSlots, SlotID(1)), 1);
	EXPECT_EQ(secondProjection.receiverSlots[1].creature, angel);

	// Both projected plans conserve every creature even though the second plan
	// fails to realize the higher-value incoming stack.
	EXPECT_EQ(projection.receiverSlots.size() + projection.sourceSlots.size(),
		secondProjection.receiverSlots.size() + secondProjection.sourceSlots.size());
	int firstProjectionTotal = 0;
	for(const auto & stack : projection.receiverSlots)
		firstProjectionTotal += stack.count;
	for(const auto & stack : projection.sourceSlots)
		firstProjectionTotal += stack.count;
	EXPECT_EQ(firstProjectionTotal, 21);
	int secondProjectionTotal = 0;
	for(const auto & stack : secondProjection.receiverSlots)
		secondProjectionTotal += stack.count;
	for(const auto & stack : secondProjection.sourceSlots)
		secondProjectionTotal += stack.count;
	EXPECT_EQ(secondProjectionTotal, firstProjectionTotal);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, ExchangeProjectionLeavesLegacyArmiesUnrestricted)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	CGHeroInstance receiver(nullptr);
	CGHeroInstance source(nullptr);
	ASSERT_TRUE(receiver.setCreature(SlotID(0), pikeman, 100));
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 100));
	EXPECT_FALSE(NK2AI::armyFormation::hasLeadershipCapacityRules(&receiver, &receiver));

	const std::vector<NK2AI::armyFormation::DesiredArmyStack> desired = {{pikeman, 200}};
	const auto projection = NK2AI::armyFormation::projectArmyExchange(
		&receiver, &source, &receiver, &source, desired);

	ASSERT_EQ(projection.transfers.size(), 1);
	EXPECT_EQ(projection.transfers.front().transferCount, 99);
	EXPECT_EQ(projectedCountAt(projection.receiverSlots, SlotID(0)), 199);
	EXPECT_EQ(projectedCountAt(projection.sourceSlots, SlotID(0)), 1);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, ExchangeProjectionChecksSourceLeadershipOnDifferingCreatureSwap)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID angel(CreatureID::decode("core:angel"));
	startExchangeGame({}, {{pikeman, 1}}, 1000000);

	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();
	ASSERT_TRUE(source->setCreature(SlotID(0), pikeman, 1));
	const auto receiverAngelCapacity = receiver->getLeadershipSlotCapacity(angel);
	const auto sourceAngelCapacity = source->getLeadershipSlotCapacity(angel);
	ASSERT_TRUE(receiverAngelCapacity);
	ASSERT_TRUE(sourceAngelCapacity);
	ASSERT_GE(receiverAngelCapacity->maximum, 2);
	ASSERT_EQ(sourceAngelCapacity->maximum, 1);
	ASSERT_TRUE(source->getLeadershipSlotCapacity(pikeman));
	for(size_t i = 0; i < GameConstants::ARMY_SIZE; ++i)
		ASSERT_TRUE(receiver->setCreature(SlotID(i), angel, 2));
	for(size_t i = 1; i < GameConstants::ARMY_SIZE; ++i)
		ASSERT_TRUE(source->setCreature(SlotID(i), pikeman, 1));
	ASSERT_EQ(source->stacksCount(), GameConstants::ARMY_SIZE);

	const std::vector<NK2AI::armyFormation::DesiredArmyStack> desired = {{pikeman, 1}};
	const auto projection = NK2AI::armyFormation::projectArmyExchange(
		receiver, source, receiver, source, desired);

	EXPECT_TRUE(projection.transfers.empty());
	EXPECT_EQ(projectedCountAt(projection.receiverSlots, SlotID(0)), 2);
	EXPECT_EQ(projectedCountAt(projection.sourceSlots, SlotID(0)), 1);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, SimulatedDonorRetainsItsCarriersSwapCapacity)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID archangel(CreatureID::decode("core:archangel"));
	startExchangeGame({}, {}, 1000);
	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();
	const auto sourceCapacity = source->getLeadershipSlotCapacity(pikeman);
	const auto receiverCapacity = receiver->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(sourceCapacity);
	ASSERT_TRUE(receiverCapacity);
	const int count = sourceCapacity->maximum + 1;
	ASSERT_LE(count, receiverCapacity->maximum);
	const auto angelCapacity = receiver->getLeadershipSlotCapacity(archangel);
	ASSERT_TRUE(angelCapacity);
	ASSERT_EQ(angelCapacity->maximum, 1);
	ASSERT_GT(archangel.toCreature()->getAIValue(), count * pikeman.toCreature()->getAIValue());
	CCreatureSet simulatedSource;
	for(int i = 0; i < GameConstants::ARMY_SIZE; ++i)
	{
		ASSERT_TRUE(receiver->setCreature(SlotID(i), pikeman, count));
		ASSERT_TRUE(source->setCreature(SlotID(i), archangel, 1));
		ASSERT_TRUE(simulatedSource.setCreature(SlotID(i), archangel, 1));
	}

	const auto gateway = makeGateway(PLAYER);
	const auto * manager = gateway->nullkiller->armyManager.get();
	EXPECT_EQ(manager->howManyReinforcementsCanGet(receiver, source), 0);
	EXPECT_EQ(manager->howManyReinforcementsCanGet(
		receiver, receiver, &simulatedSource, TerrainId::NONE, source), 0);
	// Omitting the carrier reproduces the optimistic simulated-swap estimate.
	EXPECT_GT(manager->howManyReinforcementsCanGet(
		receiver, receiver, &simulatedSource, TerrainId::NONE), 0);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, EmptyDonorPreservesExistingPhysicalStacks)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	startExchangeGame({}, {});
	auto * receiver = findHeroAt(HERO_POS);
	auto * source = findHeroAt(SOURCE_HERO_POS);
	ASSERT_NE(receiver, nullptr);
	ASSERT_NE(source, nullptr);
	receiver->clearSlots();
	source->clearSlots();
	const auto capacity = receiver->getLeadershipSlotCapacity(pikeman);
	ASSERT_TRUE(capacity);
	ASSERT_TRUE(receiver->setCreature(SlotID(0), pikeman, capacity->maximum));
	ASSERT_TRUE(receiver->setCreature(SlotID(1), pikeman, capacity->maximum));
	const auto gateway = makeGateway(PLAYER);
	const auto army = gateway->nullkiller->armyManager->getBestArmy(
		receiver, receiver, source, TerrainId::NONE);
	ASSERT_EQ(army.size(), 2);
	EXPECT_EQ(army[0].count, capacity->maximum);
	EXPECT_EQ(army[1].count, capacity->maximum);
}

TEST_F(NewHorizonsArmyFormationLeadershipTest, SwapPreflightHandlesEmptySlotsAndLastStackProtection)
{
	const CreatureID pikeman(CreatureID::decode("core:pikeman"));
	const CreatureID archer(CreatureID::decode("core:archer"));
	startGame({{pikeman, 16}});

	const auto * destination = findHeroAt(HERO_POS);
	ASSERT_NE(destination, nullptr);
	CGHeroInstance source(nullptr);
	ASSERT_TRUE(source.setCreature(SlotID(0), pikeman, 1));
	ASSERT_TRUE(source.setCreature(SlotID(1), archer, 1));

	EXPECT_TRUE(NK2AI::armyFormation::canSwapStacks(
		&source, destination, SlotID(0), SlotID(1)));

	CGHeroInstance oversizedSource(nullptr);
	ASSERT_TRUE(oversizedSource.setCreature(SlotID(0), pikeman, 2));
	EXPECT_FALSE(NK2AI::armyFormation::canSwapStacks(
		&oversizedSource, destination, SlotID(0), SlotID(1)));

	CGHeroInstance lastStackSource(nullptr);
	ASSERT_TRUE(lastStackSource.setCreature(SlotID(0), pikeman, 1));
	EXPECT_FALSE(NK2AI::armyFormation::canSwapStacks(
		&lastStackSource, destination, SlotID(0), SlotID(1)));
}
