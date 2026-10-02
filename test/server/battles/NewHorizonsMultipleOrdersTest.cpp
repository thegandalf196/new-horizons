/*
 * NewHorizonsMultipleOrdersTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto basicCommandPerk = "new-horizons:command.aggressiveCommander";
constexpr auto commandingPresencePerk = "new-horizons:command.commandingPresence";
constexpr auto combinedArmsPerk = "new-horizons:command.combinedArms";

class MultipleOrdersEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit MultipleOrdersEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsMultipleOrdersTest : public HeroCommandFixture
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
		auto magicRules = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		magicRules["physicalDamageReductionCapPercent"].Integer() = 12;
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, std::move(magicRules));
	}

	bool offerContains(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(const auto & offer : offers)
				if(offer.selection.skillId == commandSkill && offer.selection.perkId == perkId)
					return true;
		}
		return false;
	}

	void acceptPerk(CGHeroInstance * hero, const std::string & perkId)
	{
		const auto rankLookup = [hero](const std::string & skill)
		{
			return hero->getPerkSkillRank(skill);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
			{
				if(offers[choice].selection.skillId == commandSkill && offers[choice].selection.perkId == perkId)
				{
					gameHandler->levelUpHero(hero, offers, choice, seed, false);
					ASSERT_TRUE(hero->hasActivePerk(commandSkill, perkId));
					return;
				}
			}
		}
		FAIL() << "No legal perk offer for " << perkId;
	}

	void selectAdvancedCommandPerk(CGHeroInstance * hero, const std::string & advancedPerk)
	{
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(offerContains(hero, advancedPerk))
			<< "An Advanced Command perk is unavailable at Basic Command";
		acceptPerk(hero, basicCommandPerk);
		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, advancedPerk));
		acceptPerk(hero, advancedPerk);
	}

	void clearStartingUnits()
	{
		BattleUnitsChanged changes;
		changes.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			changes.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(changes);
	}

	void beginWith(CStack * activeStack)
	{
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = activeStack->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		ASSERT_EQ(battle()->battleActiveUnit(), activeStack);
	}

	void grantOrderAllowance(BattleSide side)
	{
		battle()->getSide(side).heroActionAllowances.grantAllowance(
			HeroActionAllowanceState::AllowanceKind::ORDER,
			HeroActionAllowanceState::GrantSource::PERK,
			battle()->getRound());
	}

	bool issue(BattleSide side, HeroCommand command)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeHeroCommand(side, command));
	}

	bool issueTargeted(BattleSide side, HeroCommand command, const CStack * target)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeTargetedHeroCommand(side, command, target->unitId()));
	}

	int expectedNegativeMorale() const
	{
		return -static_cast<int>(LIBRARY->engineSettings()->getVector(
			EGameSettings::COMBAT_BAD_MORALE_CHANCE).size());
	}

	void giveNegativeMorale(CStack * stack)
	{
		stack->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MORALE, BonusSource::OTHER, -100, BonusSourceID()));
	}

	BattleHeroOrderStateChanged orderStatePacket(BattleSide side, std::vector<HeroOrderState> states) const
	{
		BattleHeroOrderStateChanged packet;
		packet.battleID = BattleID(0);
		packet.side = side;
		packet.states = std::move(states);
		if(!packet.states->empty())
			packet.state = packet.states->back();
		return packet;
	}
};
}

TEST_F(NewHorizonsMultipleOrdersTest, AdvancedCommandPerksRequireTheirLegalRankOffers)
{
	startGame();
	selectAdvancedCommandPerk(attackerSideHero, commandingPresencePerk);
	const int decoded = SecondarySkill::decode(commandSkill);
	ASSERT_GE(decoded, 0);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(decoded)), MasteryLevel::ADVANCED);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(commandSkill, basicCommandPerk));
	EXPECT_TRUE(attackerSideHero->hasActivePerk(commandSkill, commandingPresencePerk));
}

TEST_F(NewHorizonsMultipleOrdersTest, ExistingOrderAllowanceEnablesADistinctSecondOrderButNotARepeat)
{
	startGame();
	selectAdvancedCommandPerk(attackerSideHero, commandingPresencePerk);
	startBattle();
	clearStartingUnits();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	ASSERT_NE(enemy, nullptr);
	beginWith(friendly);

	grantOrderAllowance(BattleSide::ATTACKER);
	const auto before = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound());
	ASSERT_EQ(before.heroActions, 1u);
	ASSERT_EQ(before.orderActions, 1u);
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE));
	const auto afterFirst = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound());
	EXPECT_EQ(afterFirst.heroActions, 1u);
	EXPECT_EQ(afterFirst.orderActions, 0u);

	EXPECT_FALSE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE))
		<< "The still-unspent flexible Hero allowance cannot reissue an already active Order";
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()), afterFirst);
	EXPECT_EQ(battle()->battleGetHeroOrderStates(BattleSide::ATTACKER).size(), 1u);

	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	const auto orders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(orders.size(), 2u);
	EXPECT_EQ(orders[0].command, HeroCommand::CHARGE);
	EXPECT_EQ(orders[1].command, HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE), orders[0]);
	EXPECT_EQ(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE), orders[1]);
	const auto afterSecond = battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound());
	EXPECT_EQ(afterSecond.heroActions, 0u);
	EXPECT_EQ(afterSecond.orderActions, 0u);
}

TEST_F(NewHorizonsMultipleOrdersTest, PresencePersistsAcrossIndependentOrderExpiryAndRejectsInvalidPackets)
{
	startGame();
	selectAdvancedCommandPerk(attackerSideHero, commandingPresencePerk);
	startBattle();
	clearStartingUnits();
	auto * held = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	ASSERT_NE(enemy, nullptr);
	beginWith(held);
	giveNegativeMorale(held);
	ASSERT_EQ(battle()->battleGetMorale(held), expectedNegativeMorale());

	grantOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	auto orders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(orders.size(), 2u);
	ASSERT_EQ(orders[0].command, HeroCommand::CHARGE);
	ASSERT_EQ(orders[1].command, HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(battle()->battleGetMorale(held), 0);

	const auto allowancesBeforeInvalidUpdates = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto activeOrderBeforeInvalidUpdates = battle()->battleGetActiveOrder(BattleSide::ATTACKER);
	auto droppedSibling = orderStatePacket(BattleSide::ATTACKER, {orders.back()});
	EXPECT_THROW(gameHandler->sendAndApply(droppedSibling), std::runtime_error)
		<< "A same-command update may not silently remove the issued Charge sibling";
	EXPECT_EQ(battle()->battleGetHeroOrderStates(BattleSide::ATTACKER), orders);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowancesBeforeInvalidUpdates);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), activeOrderBeforeInvalidUpdates);
	EXPECT_EQ(battle()->battleGetMorale(held), 0);

	auto changedAnchor = orders;
	auto hold = std::ranges::find(changedAnchor, HeroCommand::HOLD_THE_LINE, &HeroOrderState::command);
	ASSERT_NE(hold, changedAnchor.end());
	ASSERT_FALSE(hold->anchors.empty());
	hold->anchors.front().unitId = 123456789u; // valid wire ID, but no such issued recipient exists
	auto forgedAnchorUpdate = orderStatePacket(BattleSide::ATTACKER, changedAnchor);
	EXPECT_THROW(gameHandler->sendAndApply(forgedAnchorUpdate), std::runtime_error)
		<< "An active Order snapshot cannot replace its issued Hold anchor";
	EXPECT_EQ(battle()->battleGetHeroOrderStates(BattleSide::ATTACKER), orders);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), allowancesBeforeInvalidUpdates);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), activeOrderBeforeInvalidUpdates);
	EXPECT_EQ(battle()->battleGetMorale(held), 0);

	MultipleOrdersEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	auto branchOrders = branch->getHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(branchOrders.size(), 2u);
	branchOrders[0].consumedUnitIds.push_back(held->unitId());
	branchOrders[1].holdBrokenUnitIds.push_back(held->unitId());
	branch->setHeroOrderStates(BattleSide::ATTACKER, branchOrders);
	EXPECT_EQ(branch->battleGetMorale(branch->getForUpdate(held->unitId()).get()), expectedNegativeMorale());
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(held->unitId()).get()), 0);
	EXPECT_EQ(sibling->battleGetMorale(sibling->getForUpdate(held->unitId()).get()), 0);
	EXPECT_EQ(battle()->battleGetMorale(held), 0);

	ASSERT_TRUE(battle()->consumeHeroOrderUnit(BattleSide::ATTACKER, held->unitId()));
	EXPECT_EQ(battle()->battleGetMorale(held), 0)
		<< "Hold independently keeps Commanding Presence after this stack spends Charge";
	BattleStackMoved moved;
	moved.battleID = BattleID(0);
	moved.stack = held->unitId();
	moved.tilesToMove.insert(BattleHex(72));
	gameHandler->sendAndApply(moved);
	const auto finalOrders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(finalOrders.size(), 2u);
	EXPECT_TRUE(finalOrders[0].containsConsumed(held->unitId()));
	EXPECT_TRUE(finalOrders[1].containsHoldBroken(held->unitId()));
	EXPECT_EQ(battle()->battleGetMorale(held), expectedNegativeMorale())
		<< "After both independent benefits end, the negative Morale is visible again";
}

TEST_F(NewHorizonsMultipleOrdersTest, OffenseAddsWhileIndependentDefensesMultiplyUpToTheConfiguredCap)
{
	startGame();
	selectAdvancedCommandPerk(attackerSideHero, combinedArmsPerk);
	startBattle();
	clearStartingUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	ASSERT_NE(defender, nullptr);
	beginWith(attacker);
	const BattleAttackInfo attackInfo(attacker, defender, 0, false);
	const auto base = battle()->calculateDmgRange(attackInfo);
	ASSERT_GT(base.damageBeforeDefense.max, 0);

	grantOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(issueTargeted(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, defender));
	const auto focusOnly = battle()->calculateDmgRange(attackInfo);
	ASSERT_TRUE(std::ranges::find(focusOnly.attackerOrderCauses, HeroCommand::FOCUS_FIRE)
		!= focusOnly.attackerOrderCauses.end());
	EXPECT_GT(focusOnly.damageBeforeDefense.max, base.damageBeforeDefense.max);
	ASSERT_TRUE(issueTargeted(BattleSide::ATTACKER, HeroCommand::FLANK, defender));
	const auto bothOffense = battle()->calculateDmgRange(attackInfo);
	EXPECT_TRUE(std::ranges::find(bothOffense.attackerOrderCauses, HeroCommand::FOCUS_FIRE)
		!= bothOffense.attackerOrderCauses.end());
	EXPECT_TRUE(std::ranges::find(bothOffense.attackerOrderCauses, HeroCommand::FLANK)
		!= bothOffense.attackerOrderCauses.end());
	EXPECT_GT(bothOffense.damageBeforeDefense.max, focusOnly.damageBeforeDefense.max)
		<< "Distinct offensive Orders contribute additively rather than replacing the first snapshot";

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(attacker)));
	ASSERT_NE(battle()->battleActiveUnit(), nullptr);
	ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(1));
	grantOrderAllowance(BattleSide::DEFENDER);
	ASSERT_TRUE(issue(BattleSide::DEFENDER, HeroCommand::RIPOSTE));
	const auto riposteOnly = battle()->calculateDmgRange(attackInfo);
	ASSERT_TRUE(std::ranges::find(riposteOnly.defenderOrderCauses, HeroCommand::RIPOSTE)
		!= riposteOnly.defenderOrderCauses.end());
	ASSERT_TRUE(issue(BattleSide::DEFENDER, HeroCommand::HOLD_THE_LINE));
	const auto bothDefense = battle()->calculateDmgRange(attackInfo);
	EXPECT_TRUE(std::ranges::find(bothDefense.defenderOrderCauses, HeroCommand::RIPOSTE)
		!= bothDefense.defenderOrderCauses.end());
	EXPECT_TRUE(std::ranges::find(bothDefense.defenderOrderCauses, HeroCommand::HOLD_THE_LINE)
		!= bothDefense.defenderOrderCauses.end());
	EXPECT_EQ(bothDefense.damageBeforeDefense.min, bothOffense.damageBeforeDefense.min);
	EXPECT_EQ(bothDefense.damageBeforeDefense.max, bothOffense.damageBeforeDefense.max);
	EXPECT_LT(bothDefense.damage.max, riposteOnly.damage.max);
	EXPECT_NEAR(bothDefense.damage.max, bothDefense.damageBeforeDefense.max * 0.88, 2.0)
		<< "The independent 5% Riposte and 10% Hold reductions combine, then obey the 12% cap";
}

TEST_F(NewHorizonsMultipleOrdersTest, CurrentSavePreservesTheCollectionAndOlderWriterRejectsLoss)
{
	startGame();
	startBattle();
	clearStartingUnits();
	auto * friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
	addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	beginWith(friendly);
	grantOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	const auto original = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(original.size(), 2u);

	const auto restored = CMemorySerializer::deepCopy(*battle(), gameState().get());
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->battleGetHeroOrderStates(BattleSide::ATTACKER), original);
	EXPECT_EQ(restored->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::HOLD_THE_LINE);

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT;
	EXPECT_THROW(older.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty())
		<< "A multi-Order save must reject lossy downsave before writing any bytes";
}

TEST(NewHorizonsMultipleOrdersLegacyMigrationTest, OlderSingletonProjectionLoadsIntoTheCollection)
{
	HeroOrderState charge;
	charge.command = HeroCommand::CHARGE;
	charge.issuedRound = 1;
	SideInBattle original(nullptr);
	original.activeOrder = HeroCommand::CHARGE;
	original.upsertOrder(charge);

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT;
	older.iser.version = ESerializationVersion::NEW_HORIZONS_BLOOD_SCENT;
	ASSERT_NO_THROW(older.oser & original);
	SideInBattle restored(nullptr);
	ASSERT_NO_THROW(older.iser & restored);
	ASSERT_EQ(restored.orderStates.size(), 1u);
	EXPECT_EQ(restored.orderStates.front(), charge);
	EXPECT_EQ(restored.lastOrder(), charge);
}
