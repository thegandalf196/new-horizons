/*
 * NewHorizonsDoubleCommandTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "BattleStartSnapshotFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/HeroActionAllowanceState.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClientBattle.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto commandSkill = "new-horizons:command";
constexpr auto doubleCommandPerk = "new-horizons:command.doubleCommand";
constexpr auto basicCommandPerk = "new-horizons:command.aggressiveCommander";
constexpr auto advancedCommandPerk = "new-horizons:command.veteranCommander";

class DoubleCommandEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DoubleCommandEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsDoubleCommandTest : public HeroCommandFixture
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
		JsonNode perks(JsonPath::builtin("config/newHorizonsPerks"));
		bool found = false;
		for(auto & perk : perks["skills"][commandSkill]["perks"].Vector())
		{
			if(perk["id"].String() == doubleCommandPerk)
			{
				// After activation, exercise production registration without a test override.
				if(perk["effect"]["status"].String() == "active")
					return;
				perk["effect"]["status"].String() = "active";
				found = true;
			}
		}
		if(!found)
			throw std::runtime_error("Missing Double Command registry entry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perks));
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

	void selectDoubleCommand(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(commandSkill);
		ASSERT_GE(decoded, 0);
		const auto skill = SecondarySkill(decoded);
		hero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(offerContains(hero, doubleCommandPerk))
			<< "Double Command must not be offered below Expert Command";
		acceptPerk(hero, basicCommandPerk);

		hero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		EXPECT_FALSE(offerContains(hero, doubleCommandPerk))
			<< "Double Command must not be offered at Advanced Command";
		acceptPerk(hero, advancedCommandPerk);

		hero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(offerContains(hero, doubleCommandPerk));
		acceptPerk(hero, doubleCommandPerk);
		EXPECT_TRUE(hero->hasActivePerk(commandSkill, basicCommandPerk));
		EXPECT_TRUE(hero->hasActivePerk(commandSkill, advancedCommandPerk));
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

	bool issueTargeted(BattleSide side, HeroCommand command, uint32_t targetId)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), battle()->sideToPlayer(side),
			BattleAction::makeTargetedHeroCommand(side, command, targetId));
	}

	bool issueProtect(const CStack * protector, const CStack * ward)
	{
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
				protector->unitId(), ward->unitId()));
	}

	void startGameWithPerk(bool spellbook = false)
	{
		startGame();
		selectDoubleCommand(attackerSideHero);
		if(spellbook)
		{
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
			setTestSpellPointTotal(attackerSideHero, 100);
		}
	}

	void startCommandBattle(CStack *& active, CStack *& target, CStack *& enemy)
	{
		startBattle();
		clearStartingUnits();
		active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
		target = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
		ASSERT_NE(active, nullptr);
		ASSERT_NE(target, nullptr);
		ASSERT_NE(enemy, nullptr);
		beginWith(active);
	}
};
}

TEST_F(NewHorizonsDoubleCommandTest, ExpertPerkUsesTheOrdinaryRankGatedOfferPath)
{
	startGame();
	selectDoubleCommand(attackerSideHero);

	const int decoded = SecondarySkill::decode(commandSkill);
	ASSERT_GE(decoded, 0);
	EXPECT_EQ(attackerSideHero->getSecSkillLevel(SecondarySkill(decoded)), MasteryLevel::EXPERT);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(commandSkill, doubleCommandPerk));
}

TEST_F(NewHorizonsDoubleCommandTest, OnlyAHeroPaidOrderTriggersOnceAndTheFollowUpMustBeDifferent)
{
	startGameWithPerk(true);
	CStack * active = nullptr;
	CStack * shooter = nullptr;
	CStack * enemy = nullptr;
	startCommandBattle(active, shooter, enemy);

	grantOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE));
	auto commandState = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	EXPECT_FALSE(commandState.used);
	EXPECT_EQ(commandState.phase, DoubleCommandState::Phase::NONE);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));

	const auto afterTypedOrder = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound());
	EXPECT_EQ(afterTypedOrder.heroActions, 1u);
	EXPECT_EQ(afterTypedOrder.orderActions, 0u);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(0)))
		<< "A Hero-paid spell is not an Order and must not trigger Double Command";
	commandState = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	EXPECT_FALSE(commandState.used);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));

	advanceRound();
	auto & attackerAllowances = battle()->getSide(BattleSide::ATTACKER).heroActionAllowances;
	const auto validAllowances = attackerAllowances;
	attackerAllowances.nextGrantId = std::numeric_limits<uint32_t>::max();
	ASSERT_NO_THROW(attackerAllowances.validateShape());
	const auto exhaustedAllowances = attackerAllowances;
	const auto doubleCommandBeforeExhaustion = battle()->getDoubleCommandState(BattleSide::ATTACKER);
	const auto ordersBeforeExhaustion = battle()->getHeroOrderStates(BattleSide::ATTACKER);
	const auto startsBeforeExhaustion = server.startedActions.size();
	EXPECT_FALSE(issue(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE))
		<< "A HERO-paid Order cannot start Double Command without a grant ID for its follow-up";
	EXPECT_EQ(attackerAllowances, exhaustedAllowances);
	EXPECT_EQ(battle()->getDoubleCommandState(BattleSide::ATTACKER), doubleCommandBeforeExhaustion);
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER), ordersBeforeExhaustion);
	EXPECT_EQ(server.startedActions.size(), startsBeforeExhaustion)
		<< "Exhausted continuation provenance must be rejected before StartAction";
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	attackerAllowances = validAllowances;
	ASSERT_NO_THROW(attackerAllowances.validateShape());

	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	commandState = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	ASSERT_TRUE(commandState.used);
	EXPECT_EQ(commandState.phase, DoubleCommandState::Phase::ORDER_REQUIRED);
	EXPECT_EQ(commandState.firstOrder, HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(commandState.issuedRound, battle()->getRound());
	EXPECT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));

	const auto pendingBudget = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound());
	ASSERT_EQ(pendingBudget.heroActions, 0u);
	ASSERT_EQ(pendingBudget.orderActions, 1u);
	const auto pendingStartedActions = server.startedActions.size();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(active)))
		<< "A Creature Action cannot interrupt Double Command's mandatory immediate Order";
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(0)))
		<< "A spell cannot replace the mandatory Order continuation";
	EXPECT_EQ(server.startedActions.size(), pendingStartedActions)
		<< "Rejected Creature and spell actions must not enter StartAction";
	EXPECT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_FALSE(issue(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE))
		<< "The same Order cannot be repeated inside the immediate Double Command sequence";
	EXPECT_FALSE(issueTargeted(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE,
		static_cast<uint32_t>(std::numeric_limits<int32_t>::max())))
		<< "An invalid target must not spend or clear the pending continuation";
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound()), pendingBudget);
	EXPECT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));

	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::BRACE));
	commandState = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	EXPECT_TRUE(commandState.used);
	EXPECT_EQ(commandState.phase, DoubleCommandState::Phase::NONE);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	auto orders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(orders.size(), 2u);
	EXPECT_EQ(orders[0].command, HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(orders[1].command, HeroCommand::BRACE);
	EXPECT_EQ(orders[0].issuedRound, orders[1].issuedRound);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::BRACE);
	const auto completedBudget = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound());
	EXPECT_EQ(completedBudget.heroActions, 0u);
	EXPECT_EQ(completedBudget.orderActions, 0u);

	advanceRound();
	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::RIPOSTE));
	commandState = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	EXPECT_TRUE(commandState.used);
	EXPECT_EQ(commandState.phase, DoubleCommandState::Phase::NONE);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER))
		<< "A later Hero-paid Order cannot trigger the once-per-combat benefit again";
	EXPECT_EQ(battle()->battleGetHeroOrderStates(BattleSide::ATTACKER).size(), 1u);
	EXPECT_EQ(battle()->battleGetActiveOrder(BattleSide::ATTACKER), HeroCommand::RIPOSTE);
}

TEST_F(NewHorizonsDoubleCommandTest, PendingChoiceDescriptorRoundTripsAndDetachedAIProjectionsRemainIndependent)
{
	startGameWithPerk();
	CStack * active = nullptr;
	CStack * shooter = nullptr;
	CStack * enemy = nullptr;
	startCommandBattle(active, shooter, enemy);

	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::CHARGE));
	ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto pending = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	ASSERT_EQ(pending.phase, DoubleCommandState::Phase::ORDER_REQUIRED);
	ASSERT_EQ(pending.firstOrder, HeroCommand::CHARGE);

	CMemorySerializer older;
	older.oser.version = ESerializationVersion::NEW_HORIZONS_MULTIPLE_ORDERS;
	EXPECT_THROW(older.oser & *battle(), std::runtime_error);
	EXPECT_TRUE(older.extractBuffer().empty())
		<< "A pre-Double-Command writer must reject pending or used perk state before writing bytes";

	// Binary CStack data omits CUnitState, so this verifies serialized battle
	// metadata and structural IDs only; it does not claim resumable combat.
	auto descriptor = battleStartFixture::snapshot(*battle(), gameState().get());
	ASSERT_NE(descriptor, nullptr);
	EXPECT_EQ(descriptor->getDoubleCommandState(BattleSide::ATTACKER), pending);
	EXPECT_EQ(descriptor->getHeroActionAllowances(BattleSide::ATTACKER),
		battle()->getHeroActionAllowances(BattleSide::ATTACKER));
	EXPECT_EQ(descriptor->getHeroOrderStates(BattleSide::ATTACKER),
		battle()->getHeroOrderStates(BattleSide::ATTACKER));
	EXPECT_EQ(descriptor->getActiveStackID(), static_cast<int32_t>(pending.anchorStackId));
	auto * descriptorAnchor = descriptor->getStack(descriptor->getActiveStackID(), false);
	ASSERT_NE(descriptorAnchor, nullptr);
	EXPECT_EQ(descriptorAnchor->unitId(), pending.anchorStackId);
	ASSERT_NO_THROW(descriptor->validateDoubleCommandStructure());
	const auto missingAnchorId = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	descriptor->getSide(BattleSide::ATTACKER).doubleCommandState.anchorStackId = missingAnchorId;
	EXPECT_THROW(descriptor->validateDoubleCommandStructure(), std::runtime_error)
		<< "A decoded pending continuation must reject a missing structural anchor reference";
	descriptor->getSide(BattleSide::ATTACKER).doubleCommandState.anchorStackId = pending.anchorStackId;
	EXPECT_NO_THROW(descriptor->validateDoubleCommandStructure());

	DoubleCommandEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	EXPECT_TRUE(parent->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_TRUE(branch->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_TRUE(sibling->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_TRUE(parent->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(branch->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(sibling->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto projectedOrder = branch->prepareHeroOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(projectedOrder);
	EXPECT_EQ(projectedOrder->action.receipt.allowance, HeroActionAllowanceState::AllowanceKind::ORDER);
	EXPECT_EQ(projectedOrder->action.receipt.source, HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND);
	ASSERT_TRUE(branch->beginProjectedHeroAction(BattleSide::ATTACKER, *projectedOrder));
	ASSERT_TRUE(branch->projectAcceptedHeroOrder(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE,
		{}, *projectedOrder));
	EXPECT_FALSE(branch->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_TRUE(parent->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_TRUE(sibling->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_FALSE(branch->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(parent->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(sibling->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));

	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::BRACE));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(parent->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_FALSE(branch->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_TRUE(sibling->getDoubleCommandState(BattleSide::ATTACKER).orderPending());
	EXPECT_TRUE(parent->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_FALSE(branch->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(sibling->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
}

TEST_F(NewHorizonsDoubleCommandTest, SecondWindActivationWaitsUntilTheImmediateFollowUpResolves)
{
	startGameWithPerk();
	startBattle();
	clearStartingUnits();
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(54), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(71), 100);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NE(enemy, nullptr);
	beginWith(active);
	target->movedThisRound = true;

	ASSERT_FALSE(issueTargeted(BattleSide::ATTACKER, HeroCommand::SECOND_WIND,
		static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	ASSERT_TRUE(issueTargeted(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, target->unitId()));
	const auto deferred = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	ASSERT_EQ(deferred.phase, DoubleCommandState::Phase::ORDER_REQUIRED);
	EXPECT_EQ(deferred.deferredSecondWindTargetUnitId, target->unitId());
	EXPECT_EQ(battle()->battleActiveUnit(), active)
		<< "Second Wind must not begin its extra activation before the immediate follow-up choice";
	const auto secondWind = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(secondWind);
	EXPECT_FALSE(secondWind->secondWindActive);

	ASSERT_TRUE(issue(BattleSide::ATTACKER, HeroCommand::BRACE));
	EXPECT_EQ(battle()->battleActiveUnit(), target)
		<< "The deferred extra activation starts after the distinct follow-up is committed";
	const auto completed = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	EXPECT_TRUE(completed.used);
	EXPECT_EQ(completed.phase, DoubleCommandState::Phase::NONE);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto activeSecondWind = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(activeSecondWind);
	EXPECT_TRUE(activeSecondWind->secondWindActive);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetOwner(target), BattleAction::makeDefend(target)));
	const auto spent = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(spent);
	EXPECT_FALSE(spent->secondWindActive);
	EXPECT_TRUE(battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER).used)
		<< "The once-per-combat Double Command state survives the deferred activation";
}

TEST_F(NewHorizonsDoubleCommandTest, EightDistinctOrdersExhaustTheImmediateChoiceWithoutStalling)
{
	startGameWithPerk();
	startBattle();
	clearStartingUnits();
	auto * active = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(70), 100);
	auto * ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(71), 100);
	auto * shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(3, 5), 100);
	auto * enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(14, 5), 100);
	ASSERT_NE(active, nullptr);
	ASSERT_NE(ward, nullptr);
	ASSERT_NE(shooter, nullptr);
	ASSERT_NE(enemy, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(shooter, enemy->getPosition()));
	beginWith(active);
	shooter->movedThisRound = true;

	for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE})
	{
		grantOrderAllowance(BattleSide::ATTACKER);
		ASSERT_TRUE(issue(BattleSide::ATTACKER, command));
	}
	grantOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(issueTargeted(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, enemy->unitId()));
	for(const auto command : {HeroCommand::RIPOSTE, HeroCommand::BRACE})
	{
		grantOrderAllowance(BattleSide::ATTACKER);
		ASSERT_TRUE(issue(BattleSide::ATTACKER, command));
	}
	grantOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(issueProtect(active, ward));
	grantOrderAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(issueTargeted(BattleSide::ATTACKER, HeroCommand::FLANK, enemy->unitId()));

	EXPECT_FALSE(battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER).used)
		<< "Dedicated Order allowances do not start the once-per-combat sequence";
	ASSERT_EQ(battle()->battleGetHeroOrderStates(BattleSide::ATTACKER).size(), 7u);
	const auto beforeTrigger = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound());
	ASSERT_EQ(beforeTrigger.heroActions, 1u);
	ASSERT_EQ(beforeTrigger.orderActions, 0u);

	ASSERT_TRUE(issueTargeted(BattleSide::ATTACKER, HeroCommand::SECOND_WIND, shooter->unitId()));
	const auto exhausted = battle()->getBattle()->getDoubleCommandState(BattleSide::ATTACKER);
	EXPECT_TRUE(exhausted.used);
	EXPECT_EQ(exhausted.phase, DoubleCommandState::Phase::NONE)
		<< "With all seven alternative Orders already issued, the mandatory continuation exhausts cleanly";
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto orders = battle()->battleGetHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(orders.size(), 8u);
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::HOLD_THE_LINE, HeroCommand::FOCUS_FIRE,
		HeroCommand::RIPOSTE, HeroCommand::BRACE, HeroCommand::PROTECT, HeroCommand::FLANK,
		HeroCommand::SECOND_WIND})
		EXPECT_EQ(std::ranges::count(orders, command, &HeroOrderState::command), 1);
	EXPECT_EQ(battle()->battleActiveUnit(), shooter)
		<< "Auto-exhausting the optional extra Order still releases the deferred Second Wind activation";
	const auto activeSecondWind = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(activeSecondWind);
	EXPECT_TRUE(activeSecondWind->secondWindActive);
	const auto afterExhaustion = battle()->getHeroActionAllowances(BattleSide::ATTACKER)
		.remainingCounts(battle()->getRound());
	EXPECT_EQ(afterExhaustion.heroActions, 0u);
	EXPECT_EQ(afterExhaustion.orderActions, 0u);

	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->battleGetOwner(shooter), BattleAction::makeDefend(shooter)));
	const auto spent = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
	ASSERT_TRUE(spent);
	EXPECT_FALSE(spent->secondWindActive);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
}
