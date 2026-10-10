/*
 * NewHorizonsIronWillTest.cpp, part of VCMI engine
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
#include "../../../lib/battle/NewHorizonsIronWill.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/battle/SideInBattle.h"
#include "../../../lib/battle/ReachabilityInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

namespace
{
constexpr auto skillId = "new-horizons:command";
constexpr auto perkId = "new-horizons:command.ironWill";

class IronWillEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit IronWillEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsIronWillTest : public HeroCommandFixture
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

	void select(const char * skill, const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.perkId == perk && offers[choice].selection.skillId == skill)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No legal offer for " << perk;
	}

	void prepare(bool selected = true)
	{
		startGame();
		ASSERT_EQ(attackerSideHero->getFactionID(), FactionID::CASTLE);
		if(selected)
			select(skillId, perkId, MasteryLevel::BASIC);
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
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1000);
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

	void activateFriendly()
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = friendly->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	HeroOrderState current(HeroCommand command)
	{
		const auto state = battle()->battleGetHeroOrderState(BattleSide::ATTACKER, command);
		EXPECT_TRUE(state);
		return state.value_or(HeroOrderState{});
	}
};
}



TEST_F(NewHorizonsIronWillTest, AcceptedOrdinaryOrderCarriesOriginalRecipientsWithoutNewHeroAction)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const auto original = current(HeroCommand::RIPOSTE);
	ASSERT_TRUE(original.ironWillLifetime);
	ASSERT_FALSE(original.divineDisciplineRecipientUnitIds.empty());
	EXPECT_FALSE(newHorizonsDivineMandate::hasDivineDisciplinePerk(attackerSideHero));
	advanceRound();
	const auto carried = current(HeroCommand::RIPOSTE);
	EXPECT_EQ(carried.issuedRound, original.issuedRound);
	EXPECT_TRUE(battle()->battleOrderBenefitAppliesTo(carried, BattleSide::ATTACKER, friendly));
	EXPECT_FALSE(battle()->getHeroCommandUsed(BattleSide::ATTACKER));
	auto * late = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(110), 100);
	EXPECT_FALSE(battle()->battleOrderBenefitAppliesTo(carried, BattleSide::ATTACKER, late));
}

TEST_F(NewHorizonsIronWillTest, ActualDefendCompletesOnlyActorCarry)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	activateFriendly();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(friendly)));
	const auto carried = current(HeroCommand::RIPOSTE);
	EXPECT_FALSE(carried.scheduledFor(friendly->unitId(), battle()->battleGetRound()));
	EXPECT_TRUE(carried.scheduledFor(ward->unitId(), battle()->battleGetRound()));
}

TEST_F(NewHorizonsIronWillTest, WaitAndContinuationDoNotCompleteCarry)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	activateFriendly();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeWait(friendly)));
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).scheduledFor(friendly->unitId(), battle()->battleGetRound()));
	BattleSetActiveStack continuation;
	continuation.battleID = BattleID(0);
	continuation.stack = friendly->unitId();
	continuation.reason = BattleUnitTurnReason::UNIT_SPELLCAST;
	gameHandler->sendAndApply(continuation);
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).scheduledFor(friendly->unitId(), battle()->battleGetRound()));
}

TEST_F(NewHorizonsIronWillTest, SameOrderReissueReplacesCarriedSnapshotWithoutDuplicates)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const auto replacement = current(HeroCommand::RIPOSTE);
	EXPECT_EQ(replacement.issuedRound, battle()->battleGetRound());
	EXPECT_TRUE(replacement.ironWillLifetime);
	EXPECT_TRUE(replacement.divineDisciplineCompletedUnitIds.empty());
	EXPECT_EQ(battle()->getHeroOrderStates(BattleSide::ATTACKER).size(), 1u);
	EXPECT_EQ(replacement.divineDisciplineRecipientUnitIds,
		newHorizonsIronWill::recipients(*battle(), BattleSide::ATTACKER, replacement));
}

TEST_F(NewHorizonsIronWillTest, UnselectedOrdinaryOrderExpiresNormally)
{
	prepare(false);
	EXPECT_FALSE(newHorizonsIronWill::hasPerk(attackerSideHero));
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_FALSE(current(HeroCommand::RIPOSTE).ironWillLifetime);
	advanceRound();
	EXPECT_FALSE(battle()->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE));
}

TEST_F(NewHorizonsIronWillTest, ActualSpentChargeDoesNotReviveAfterRoundBoundary)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	blockRetaliation(enemy);
	const auto reachability = battle()->getReachability(friendly);
	BattleHex attackFrom = BattleHex::INVALID;
	for(const auto hex : battle()->battleGetAvailableHexes(friendly, false))
		if(reachability.distances[hex.toInt()] >= 3
			&& battle()->isMeleeAttackPossible(friendly, enemy, hex))
		{
			attackFrom = hex;
			break;
		}
	ASSERT_TRUE(attackFrom.isValid());
	ASSERT_GE(reachability.distances[attackFrom.toInt()], 3u);
	ASSERT_TRUE(battle()->isMeleeAttackPossible(friendly, enemy, attackFrom));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(friendly, enemy->getPosition(), attackFrom)));
	ASSERT_TRUE(current(HeroCommand::CHARGE).containsConsumed(friendly->unitId()));
	advanceRound();
	EXPECT_FALSE(battle()->battleOrderBenefitAppliesTo(current(HeroCommand::CHARGE),
		BattleSide::ATTACKER, friendly));
}

TEST_F(NewHorizonsIronWillTest, ActualSeparatedProtectStaysBrokenAfterRoundBoundary)
{
	prepare();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			friendly->unitId(), ward->unitId())));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(friendly, BattleHex(55))));
	ASSERT_TRUE(current(HeroCommand::PROTECT).protectBroken);
	advanceRound();
	EXPECT_FALSE(battle()->battleOrderBenefitAppliesTo(current(HeroCommand::PROTECT),
		BattleSide::ATTACKER, ward));
}

TEST_F(NewHorizonsIronWillTest, SecondWindIsNotAnEndRoundCarry)
{
	prepare();
	HeroOrderState order;
	order.command = HeroCommand::SECOND_WIND;
	order.issuedRound = battle()->battleGetRound();
	order.primaryTargetUnitId = friendly->unitId();
	EXPECT_TRUE(newHorizonsIronWill::recipients(*battle(), BattleSide::ATTACKER, order).empty());
	order.ironWillLifetime = true;
	order.divineDisciplineRecipientUnitIds = {friendly->unitId()};
	EXPECT_THROW(order.validateShape(), std::runtime_error);
}

TEST_F(NewHorizonsIronWillTest, ActualTimeStopPassWithoutActivationPreservesCarry)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	SetStackEffect effect;
	effect.battleID = BattleID(0);
	effect.toAdd.emplace_back(friendly->unitId(), std::vector<Bonus>{stopped});
	gameHandler->sendAndApply(effect);
	activateFriendly();
	ASSERT_TRUE(friendly->isTimeStopped());
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeNoAction(friendly)));
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).scheduledFor(friendly->unitId(), battle()->battleGetRound()));
}

TEST_F(NewHorizonsIronWillTest, ActualFrozenForfeitureCompletesCarryAndThaws)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	advanceRound();
	activateFriendly();
	SetStackEffect frozen;
	frozen.battleID = BattleID(0);
	frozen.toAdd.emplace_back(friendly->unitId(), std::vector<Bonus>{
		newHorizonsFrozen::marker(BonusSourceID(friendly->creatureId()), battle()->battleGetRound())});
	gameHandler->sendAndApply(frozen);
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*friendly));
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeNoAction(friendly)));
	EXPECT_FALSE(newHorizonsFrozen::isFrozen(*friendly));
	EXPECT_FALSE(current(HeroCommand::RIPOSTE).scheduledFor(friendly->unitId(), battle()->battleGetRound()));
}

TEST_F(NewHorizonsIronWillTest, DetachedStasisPassAndGenuineCompletionIsolateParentSiblingAndLive)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	IronWillEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	parent->nextRound();
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	Bonus stopped(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP, BonusSource::OTHER, 1, BonusSourceID());
	child->addUnitBonus(friendly->unitId(), {stopped});
	child->nextTurn(friendly->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(friendly->unitId(), child->getRound()));
	child->removeUnitBonus(friendly->unitId(), {stopped});
	EXPECT_FALSE(child->battleGetUnitByID(friendly->unitId())->isTimeStopped());
	child->completeSwiftNormalActivation(friendly->unitId());
	EXPECT_FALSE(child->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(friendly->unitId(), child->getRound()));
	EXPECT_TRUE(parent->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(friendly->unitId(), parent->getRound()));
	EXPECT_TRUE(sibling->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::RIPOSTE)
		->scheduledFor(friendly->unitId(), sibling->getRound()));
	EXPECT_TRUE(current(HeroCommand::RIPOSTE).divineDisciplineCompletedUnitIds.empty());
}

TEST_F(NewHorizonsIronWillTest, BothPerksCaptureOneSharedNonstackingRecipientLedger)
{
	prepare();
	select("new-horizons:divineMandate", "new-horizons:divineMandate.consecratedCasting", MasteryLevel::BASIC);
	select("new-horizons:divineMandate", "new-horizons:divineMandate.divineDiscipline", MasteryLevel::ADVANCED);
	BattleAction spell;
	spell.actionType = EActionType::HERO_SPELL;
	spell.side = BattleSide::ATTACKER;
	spell.spell = SpellID(SpellID::BLESS);
	spell.aimToUnit(friendly);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), spell));
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	const auto order = current(HeroCommand::RIPOSTE);
	EXPECT_TRUE(order.ironWillLifetime);
	EXPECT_EQ(order.divineDisciplineRecipientUnitIds,
		newHorizonsIronWill::recipients(*battle(), BattleSide::ATTACKER, order));
	advanceRound();
	activateFriendly();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeDefend(friendly)));
	EXPECT_EQ(current(HeroCommand::RIPOSTE).divineDisciplineCompletedUnitIds,
		std::vector<uint32_t>{friendly->unitId()});
}

TEST(NewHorizonsIronWillProtocolTest, CurrentRoundTripOldPrefixesLegacyDefaultsAndMalformedShape)
{
	HeroOrderState order;
	order.command = HeroCommand::RIPOSTE;
	order.issuedRound = 1;
	order.ironWillLifetime = true;
	order.divineDisciplineRecipientUnitIds = {1, 3};
	order.divineDisciplineCompletedUnitIds = {1};
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & order);
	HeroOrderState restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(restored, order);
	const auto reject = [](const auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES;
		EXPECT_THROW(bytes.oser & value, std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	};
	reject(order);
	StartAction action(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::RIPOSTE));
	action.orderState = order;
	reject(action);
	BattleHeroOrderStateChanged changed;
	changed.state = order;
	changed.states = std::vector<HeroOrderState>{order};
	reject(changed);
	CGameState callback;
	callback.preInit(LIBRARY);
	SideInBattle side(&callback);
	side.orderStates = {order};
	reject(side);
	BattleStart battleStart;
	battleStart.battleID = BattleID(1);
	battleStart.info = std::make_unique<BattleInfo>(&callback);
	battleStart.info->getSide(BattleSide::ATTACKER).orderStates = {order};
	reject(*battleStart.info);
	reject(battleStart);
	order.divineDisciplineCompletedUnitIds = {2};
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	order.divineDisciplineRecipientUnitIds.clear();
	order.divineDisciplineCompletedUnitIds.clear();
	EXPECT_THROW(order.validateShape(), std::runtime_error);
	order.ironWillLifetime = false;
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_DEFENSIVE_START_SPECIALTIES;
	ASSERT_NO_THROW(old.oser & order);
	restored.ironWillLifetime = true;
	ASSERT_NO_THROW(old.iser & restored);
	EXPECT_FALSE(restored.ironWillLifetime);
}
