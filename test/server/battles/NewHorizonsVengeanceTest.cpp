/*
 * NewHorizonsVengeanceTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/IGameSettings.h"
#include "../../../lib/battle/BattleInfo.h"
#include "../../../lib/battle/NewHorizonsOffense.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/serializer/ESerializationVersion.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
class NewHorizonsVengeanceTest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void prepare(bool vengeance = true)
	{
		startGame();
		if(vengeance)
		{
			const int offense = SecondarySkill::decode(newHorizonsOffense::SKILL);
			ASSERT_GE(offense, 0);
			attackerSideHero->setSecSkillLevel(SecondarySkill(offense), MasteryLevel::ADVANCED,
				ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE});
			ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsOffense::SKILL, newHorizonsOffense::VENGEANCE));
		}
		startBattle();
		beginCombat();
	}

	bool issue(HeroCommand command = HeroCommand::RIPOSTE)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeHeroCommand(BattleSide::ATTACKER, command));
	}

	CStack * addSummonedStack(BattleSide side, const CreatureID & creature, const BattleHex & position)
	{
		battle::UnitInfo info;
		info.id = battle()->battleNextUnitId();
		info.count = 3;
		info.type = creature;
		info.side = side;
		info.position = position;
		info.summoned = true;

		BattleUnitsChanged update;
		update.battleID = BattleID(0);
		update.changedStacks.emplace_back(info.id, UnitChanges::EOperation::ADD);
		info.save(update.changedStacks.back().data);
		gameHandler->sendAndApply(update);
		return battle()->getStack(info.id);
	}

	CStack * addCommanderSentinel()
	{
		CStackBasicDescriptor descriptor(creatureByName("core:angel"), 2);
		auto sentinel = std::make_unique<CStack>(&descriptor, PlayerColor(0), battle()->nextUnitId(),
			BattleSide::ATTACKER, SlotID::COMMANDER_SLOT_PLACEHOLDER);
		auto * commander = sentinel.get();
		commander->initialPosition = BattleHex(49);
		battle()->stacks.push_back(std::move(sentinel));
		commander->localInit(battle());
		return commander;
	}

	static size_t vengeanceBonusCount(const CStack * stack)
	{
		const auto bonuses = stack->getBonusesFrom(BonusSource::HERO_COMMAND);
		return static_cast<size_t>(std::count_if(bonuses->begin(), bonuses->end(), [](const auto & bonus)
		{
			return bonus && bonus->duration == BonusDuration::N_TURNS
				&& bonus->sid == BonusSourceID(BonusCustomSource(static_cast<int32_t>(HeroCommand::RIPOSTE)))
				&& bonus->type == BonusType::ADDITIONAL_RETALIATION && bonus->val == 1;
		}));
	}
};
}

TEST_F(NewHorizonsVengeanceTest, AcceptedRiposteGrantsTwoCountersAndTheThirdIsExhausted)
{
	prepare();
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 20);
	const auto angel = creatureByName("core:angel");
	auto * firstEnemy = addStack(BattleSide::DEFENDER, angel, BattleHex(69), 20);
	auto * secondEnemy = addStack(BattleSide::DEFENDER, angel, BattleHex(71), 20);
	auto * thirdEnemy = addStack(BattleSide::DEFENDER, angel, BattleHex(53), 20);
	ASSERT_NE(ally, nullptr);
	ASSERT_NE(firstEnemy, nullptr);
	ASSERT_NE(secondEnemy, nullptr);
	ASSERT_NE(thirdEnemy, nullptr);
	ASSERT_EQ(ally->counterAttacks.total(), 1);

	ASSERT_TRUE(issue());
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::RIPOSTE);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
	{
		return line.find("Vengeance grants each affected stack one additional retaliation this round.") != std::string::npos;
	})) << ::testing::PrintToString(server.battleLogLines);
	ASSERT_EQ(vengeanceBonusCount(ally), 1u);
	const auto commandBonuses = ally->getBonusesFrom(BonusSource::HERO_COMMAND);
	const auto bonus = std::ranges::find_if(*commandBonuses, [](const auto & value)
	{
		return value && value->type == BonusType::ADDITIONAL_RETALIATION;
	});
	ASSERT_NE(bonus, commandBonuses->end());
	EXPECT_EQ((*bonus)->duration, BonusDuration::N_TURNS);
	EXPECT_EQ((*bonus)->turnsRemain, 1);
	EXPECT_EQ((*bonus)->source, BonusSource::HERO_COMMAND);
	EXPECT_EQ((*bonus)->sid,
		BonusSourceID(BonusCustomSource(static_cast<int32_t>(HeroCommand::RIPOSTE))));
	EXPECT_EQ((*bonus)->val, 1);
	EXPECT_EQ(ally->counterAttacks.total(), 2);
	EXPECT_EQ(ally->counterAttacks.available(), 2);

	const auto retaliationCount = [&]
	{
		return std::ranges::count_if(server.attacks, [ally](const auto & action)
		{
			return action.counter() && action.stackAttacking == ally->unitId();
		});
	};
	ASSERT_TRUE(attack(firstEnemy, ally->getPosition()));
	EXPECT_EQ(retaliationCount(), 1);
	EXPECT_EQ(ally->counterAttacks.available(), 1);
	ASSERT_TRUE(attack(secondEnemy, ally->getPosition()));
	EXPECT_EQ(retaliationCount(), 2);
	EXPECT_EQ(ally->counterAttacks.available(), 0);
	ASSERT_TRUE(attack(thirdEnemy, ally->getPosition()));
	EXPECT_EQ(retaliationCount(), 2) << "A third authoritative attack must not receive a retaliation";
	EXPECT_EQ(ally->counterAttacks.available(), 0);
	EXPECT_FALSE(ally->counterAttacks.canUse());
	EXPECT_FALSE(issue()) << "A second accepted Riposte must not duplicate Vengeance";
	EXPECT_EQ(vengeanceBonusCount(ally), 1u);
}

TEST_F(NewHorizonsVengeanceTest, NonRiposteOrderDoesNotGrantVengeance)
{
	prepare();
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 5);
	ASSERT_NE(ally, nullptr);
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_FALSE(newHorizonsOffense::hasVengeanceRetaliationBonus(ally));
	EXPECT_EQ(ally->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsVengeanceTest, RiposteWithoutPerkDoesNotGrantVengeance)
{
	prepare(false);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 5);
	ASSERT_NE(ally, nullptr);
	ASSERT_TRUE(issue());
	EXPECT_FALSE(newHorizonsOffense::hasVengeanceRetaliationBonus(ally));
	EXPECT_EQ(ally->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsVengeanceTest, OnlyLivingOrdinaryAlliedStacksReceiveTheBonus)
{
	prepare();
	auto * ordinary = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 5);
	auto * ghost = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80), 5);
	auto * turret = addStack(BattleSide::ATTACKER, CreatureID::ARROW_TOWERS, BattleHex(90), 1);
	auto * siege = addStack(BattleSide::ATTACKER, creatureByName("core:ballista"), BattleHex(100), 1);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 5);
	auto * commander = addCommanderSentinel();
	ASSERT_NE(ordinary, nullptr);
	ASSERT_NE(ghost, nullptr);
	ASSERT_NE(turret, nullptr);
	ASSERT_NE(siege, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_TRUE(turret->isTurret());
	ASSERT_TRUE(siege->hasBonusOfType(BonusType::SIEGE_WEAPON));
	ghost->makeGhost();
	ASSERT_TRUE(ghost->isGhost());

	ASSERT_TRUE(issue());
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(ordinary));
	for(const auto * excluded : {ghost, turret, siege, enemy, commander})
		EXPECT_FALSE(newHorizonsOffense::hasVengeanceRetaliationBonus(excluded));
}

TEST_F(NewHorizonsVengeanceTest, NoRetaliationStillSuppressesVengeanceCounters)
{
	prepare();
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 20);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 20);
	ASSERT_NE(ally, nullptr);
	ASSERT_NE(enemy, nullptr);
	ally->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_RETALIATION, BonusSource::OTHER, 0, BonusSourceID()));
	ASSERT_TRUE(issue());
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(ally));
	EXPECT_EQ(ally->counterAttacks.total(), 0);

	ASSERT_TRUE(attack(enemy, ally->getPosition()));
	EXPECT_EQ(ally->counterAttacks.available(), 0);
	EXPECT_EQ(std::ranges::count_if(server.attacks, [ally](const auto & action)
	{
		return action.counter() && action.stackAttacking == ally->unitId();
	}), 0);
}

TEST_F(NewHorizonsVengeanceTest, AStackThatAlreadySpentItsCounterGetsOneNewCounter)
{
	prepare();
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 5);
	ASSERT_NE(ally, nullptr);
	ally->counterAttacks.use();
	ASSERT_FALSE(ally->counterAttacks.canUse());

	ASSERT_TRUE(issue());
	EXPECT_EQ(ally->counterAttacks.total(), 2);
	EXPECT_EQ(ally->counterAttacks.available(), 1);
	EXPECT_TRUE(ally->counterAttacks.canUse());
	ally->counterAttacks.use();
	EXPECT_FALSE(ally->counterAttacks.canUse());
}

TEST_F(NewHorizonsVengeanceTest, LateArrivalsInheritRiposteAndIntrinsicRetaliationStacks)
{
	prepare();
	auto * griffin = addStack(BattleSide::ATTACKER, creatureByName("core:royalGriffin"), BattleHex(70), 5);
	ASSERT_NE(griffin, nullptr);
	ASSERT_EQ(griffin->counterAttacks.total(), 2);
	ASSERT_TRUE(issue());
	EXPECT_EQ(griffin->counterAttacks.total(), 3);

	auto * arrival = addSummonedStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(80));
	ASSERT_NE(arrival, nullptr);
	EXPECT_TRUE(arrival->summoned);
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(arrival));
	EXPECT_EQ(arrival->counterAttacks.total(), 2);
}

TEST_F(NewHorizonsVengeanceTest, BonusExpiresAtRoundEndAndCanBeIssuedAgain)
{
	prepare();
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 5);
	ASSERT_NE(ally, nullptr);
	ASSERT_TRUE(issue());
	ASSERT_EQ(ally->counterAttacks.total(), 2);

	advanceRound();
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::NONE);
	EXPECT_FALSE(newHorizonsOffense::hasVengeanceRetaliationBonus(ally));
	EXPECT_EQ(ally->counterAttacks.total(), 1);
	EXPECT_EQ(ally->counterAttacks.available(), 1);

	ASSERT_TRUE(issue());
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(ally));
	EXPECT_EQ(ally->counterAttacks.total(), 2);
}

TEST_F(NewHorizonsVengeanceTest, BonusAndSpentCounterSurviveBattleStartRoundTrip)
{
	prepare();
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 5);
	ASSERT_NE(ally, nullptr);
	ASSERT_TRUE(issue());
	ally->counterAttacks.use();
	ASSERT_EQ(ally->counterAttacks.available(), 1);

	const auto copyState = [](const BattleInfo & source, IGameInfoCallback * callback)
	{
		return CMemorySerializer::deepCopy(source, callback);
	};
	const auto restored = copyState(*battle(), gameState().get());
	const auto * restoredAlly = restored->getStack(ally->unitId(), false);
	ASSERT_NE(restoredAlly, nullptr);
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(restoredAlly));
	EXPECT_EQ(restoredAlly->counterAttacks.total(), 2);
	EXPECT_EQ(restoredAlly->counterAttacks.available(), 1);

	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = copyState(*battle(), gameState().get());
	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	wire.oser & outgoing;
	wire.iser.cb = gameState().get();
	BattleStart incoming;
	wire.iser & incoming;
	ASSERT_NE(incoming.info, nullptr);
	const auto * packetAlly = incoming.info->getStack(ally->unitId(), false);
	ASSERT_NE(packetAlly, nullptr);
	EXPECT_TRUE(newHorizonsOffense::hasVengeanceRetaliationBonus(packetAlly));
	EXPECT_EQ(packetAlly->counterAttacks.total(), 2);
	EXPECT_EQ(packetAlly->counterAttacks.available(), 1);
}
