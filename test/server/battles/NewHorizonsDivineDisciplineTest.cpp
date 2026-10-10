/*
 * NewHorizonsDivineDisciplineTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

namespace
{
constexpr auto skillId = "new-horizons:divineMandate";
constexpr auto perkId = "new-horizons:divineMandate.divineDiscipline";

class DisciplineEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit DisciplineEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsDivineDisciplineTest : public HeroCommandFixture
{
protected:
	CStack * friendly = nullptr;
	CStack * ward = nullptr;
	CStack * shooter = nullptr;
	CStack * enemy = nullptr;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}

	void select(const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skillId)),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.perkId == perk && offers[choice].selection.skillId == skillId)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skillId, perk));
					return;
				}
		}
		FAIL() << "No legal offer for " << perk;
	}

	void prepare(bool selected = true)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		select("new-horizons:divineMandate.consecratedCasting", MasteryLevel::BASIC);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skillId)),
			MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		if(selected)
			select(perkId, MasteryLevel::ADVANCED);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:wisdom")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID(SpellID::BLESS));
		attackerSideHero->initializeSpellPoints(attackerSideHero->manaLimit(), 0);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(89), 100);
		ward = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(90), 100);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(70), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 100);
		beginCombat();
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = friendly->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
		for(auto * unit : {friendly, ward, shooter, enemy})
			unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::MORALE, BonusSource::OTHER, -100, BonusSourceID()));
		ASSERT_LT(battle()->battleGetMorale(friendly), 0);
	}

	void castLight()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = SpellID::BLESS;
		action.aimToUnit(friendly);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
		const auto allowance = battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER);
		ASSERT_TRUE(allowance);
		ASSERT_EQ(allowance->source, HeroActionAllowanceState::GrantSource::DIVINE_MANDATE);
	}

	HeroOrderState current(HeroCommand command)
	{
		const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, command);
		EXPECT_TRUE(state);
		return state.value_or(HeroOrderState{});
	}
};
}


TEST_F(NewHorizonsDivineDisciplineTest, ActualFollowupCarriesOnlyOriginalRecipientsWithoutSpendingNewHeroAction)
{
	prepare();
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const auto original = current(HeroCommand::RIPOSTE);
	ASSERT_FALSE(original.divineDisciplineRecipientUnitIds.empty());
	advanceRound();
	const auto carried = current(HeroCommand::RIPOSTE);
	EXPECT_EQ(carried.issuedRound, original.issuedRound);
	EXPECT_TRUE(battle()->battleOrderBenefitAppliesTo(carried, BattleSide::ATTACKER, friendly));
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	auto * late = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(110), 100);
	EXPECT_FALSE(battle()->battleOrderBenefitAppliesTo(carried, BattleSide::ATTACKER, late));
}

TEST_F(NewHorizonsDivineDisciplineTest, AcceptedDefendCompletesOnlyActorsCarry)
{
	prepare();
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = friendly->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(friendly)));
	const auto order = current(HeroCommand::RIPOSTE);
	EXPECT_FALSE(order.scheduledFor(friendly->unitId(), battle()->battleGetRound()));
	EXPECT_TRUE(order.scheduledFor(ward->unitId(), battle()->battleGetRound()));
}

TEST_F(NewHorizonsDivineDisciplineTest, WaitDoesNotCompleteTheDelayedActivation)
{
	prepare();
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	BattleSetActiveStack active;
	active.battleID = BattleID(0);
	active.stack = friendly->unitId();
	active.reason = BattleUnitTurnReason::TURN_QUEUE;
	gameHandler->sendAndApply(active);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeWait(friendly)));
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).scheduledFor(friendly->unitId(), battle()->battleGetRound()));
}

TEST_F(NewHorizonsDivineDisciplineTest, SameOrderReissueReplacesRatherThanStacks)
{
	prepare();
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(current(HeroCommand::RIPOSTE).issuedRound, battle()->battleGetRound());
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).divineDisciplineRecipientUnitIds.empty());
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER).size(), 1);
}

TEST_F(NewHorizonsDivineDisciplineTest, OrdinaryAndUnselectedFollowupsKeepOrdinaryExpiry)
{
	prepare(false);
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).divineDisciplineRecipientUnitIds.empty());
	advanceRound();
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE));
}

TEST_F(NewHorizonsDivineDisciplineTest, FocusMarkSurvivesButOnlyOriginalShootersRemainEligible)
{
	prepare();
	castLight();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, enemy->unitId())));
	advanceRound();
	ASSERT_TRUE(battle()->battleGetFocusFireState(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->battleIsTargetedRangedCommand(shooter, enemy, true, false));
	EXPECT_FALSE(battle()->battleIsTargetedRangedCommand(friendly, enemy, true, false));
}

TEST_F(NewHorizonsDivineDisciplineTest, DetachedForfeitCompletesCarryAndIsolatesParentSiblingAndLive)
{
	prepare();
	castLight();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	DisciplineEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	parent->nextRound();
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	child->addUnitBonus(friendly->unitId(), {stopped});
	child->nextTurn(friendly->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(friendly->unitId(), child->getRound()));
	EXPECT_TRUE(parent->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(friendly->unitId(), parent->getRound()));
	EXPECT_TRUE(sibling->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(friendly->unitId(), sibling->getRound()));
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).divineDisciplineCompletedUnitIds.empty());
}

TEST(NewHorizonsDivineDisciplineProtocolTest, CurrentRoundTripOldWriterPrefixesAndMalformedProgress)
{
	HeroOrderState order;
	order.command = HeroCommand::RIPOSTE;
	order.issuedRound = 1;
	order.divineDisciplineRecipientUnitIds = {1, 3};
	order.divineDisciplineCompletedUnitIds = {1};
	CMemorySerializer currentWire;
	currentWire.oser.version = ESerializationVersion::CURRENT;
	currentWire.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(currentWire.oser & order);
	HeroOrderState restored;
	ASSERT_NO_THROW(currentWire.iser & restored);
	EXPECT_EQ(order, restored);
	const auto old = [](const auto & value)
	{
		CMemorySerializer wire;
		wire.oser.version = ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE;
		EXPECT_THROW(wire.oser & value, std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
	};
	old(order);
	StartAction start(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::RIPOSTE));
	start.orderState = order;
	old(start);
	BattleHeroOrderStateChanged update;
	update.state = order;
	update.states = std::vector<HeroOrderState>{order};
	old(update);
	order.divineDisciplineCompletedUnitIds = {2};
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	order.divineDisciplineRecipientUnitIds.clear();
	order.divineDisciplineCompletedUnitIds.clear();
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_THANT_REANIMATE;
	ASSERT_NO_THROW(legacy.oser & order);
	ASSERT_NO_THROW(legacy.iser & restored);
	EXPECT_TRUE(restored.divineDisciplineRecipientUnitIds.empty());
	EXPECT_TRUE(restored.divineDisciplineCompletedUnitIds.empty());
}
