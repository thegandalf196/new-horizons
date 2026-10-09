/*
 * NewHorizonsRoyalStandardTest.cpp, part of VCMI engine
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
constexpr auto perkId = "new-horizons:divineMandate.royalStandard";

class RoyalEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit RoyalEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsRoyalStandardTest : public HeroCommandFixture
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

TEST_F(NewHorizonsRoyalStandardTest, ActiveRegistryRequiresActualAdvancedSelection)
{
	prepare();
	EXPECT_TRUE(newHorizonsDivineMandate::hasRoyalStandardPerk(attackerSideHero));
	EXPECT_FALSE(newHorizonsDivineMandate::hasRoyalStandardPerk(defenderSideHero));
	EXPECT_FALSE(newHorizonsDivineMandate::hasRoyalStandardPerk(nullptr));
	castLight();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	friendly->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::OTHER, 200, BonusSourceID()));
	ASSERT_GT(friendly->moraleVal(), 0);
	EXPECT_EQ(battle()->battleGetMorale(friendly), friendly->moraleVal());
}

TEST_F(NewHorizonsRoyalStandardTest, OrdinaryOrderDoesNotPretendToBeMandate)
{
	prepare();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(current(HeroCommand::CHARGE).royalStandardRecipientUnitIds.empty());
	EXPECT_LT(battle()->battleGetMorale(friendly), 0);
}

TEST_F(NewHorizonsRoyalStandardTest, UnselectedMandateDoesNotGrantFloor)
{
	prepare(false);
	castLight();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_TRUE(current(HeroCommand::CHARGE).royalStandardRecipientUnitIds.empty());
	EXPECT_LT(battle()->battleGetMorale(friendly), 0);
}

TEST_F(NewHorizonsRoyalStandardTest, ConsumedChargeKeepsScheduledFloorInDetachedBranch)
{
	prepare();
	castLight();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleGetMorale(friendly), 0);
	EXPECT_LT(battle()->battleGetMorale(enemy), 0);
	RoyalEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	auto order = current(HeroCommand::CHARGE);
	order.consumedUnitIds = {friendly->unitId()};
	branch->setHeroOrderState(BattleSide::ATTACKER, order);
	EXPECT_EQ(branch->battleGetMorale(branch->getForUpdate(friendly->unitId()).get()), 0);
	EXPECT_EQ(parent->battleGetMorale(parent->getForUpdate(friendly->unitId()).get()), 0);
	branch->setHeroOrderState(BattleSide::ATTACKER, {});
	EXPECT_LT(branch->battleGetMorale(branch->getForUpdate(friendly->unitId()).get()), 0);
	EXPECT_EQ(battle()->battleGetMorale(friendly), 0);
	advanceRound();
	EXPECT_LT(battle()->battleGetMorale(friendly), 0);
}

TEST_F(NewHorizonsRoyalStandardTest, ProtectCapturedPairKeepsFloorAfterBrokenLink)
{
	prepare();
	castLight();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, HeroCommand::PROTECT,
			friendly->unitId(), ward->unitId())));
	EXPECT_EQ(battle()->battleGetMorale(friendly), 0);
	EXPECT_EQ(battle()->battleGetMorale(ward), 0);
	EXPECT_LT(battle()->battleGetMorale(shooter), 0);
	auto order = current(HeroCommand::PROTECT);
	order.protectBroken = true;
	BattleHeroOrderStateChanged update;
	update.battleID = BattleID(0);
	update.side = BattleSide::ATTACKER;
	update.state = order;
	update.states = std::vector<HeroOrderState>{order};
	gameHandler->sendAndApply(update);
	EXPECT_EQ(battle()->battleGetMorale(ward), 0);
	auto * late = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(110), 100);
	late->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MORALE, BonusSource::OTHER, -100, BonusSourceID()));
	EXPECT_LT(battle()->battleGetMorale(late), 0);
}

TEST_F(NewHorizonsRoyalStandardTest, FocusOnlyCapturesEligibleFriendlyRecipients)
{
	prepare();
	castLight();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, enemy->unitId())));
	EXPECT_EQ(battle()->battleGetMorale(shooter), 0);
	EXPECT_LT(battle()->battleGetMorale(friendly), 0);
	EXPECT_LT(battle()->battleGetMorale(enemy), 0);
}

TEST_F(NewHorizonsRoyalStandardTest, CurrentHostileControllerLosesCapturedFloor)
{
	prepare();
	castLight();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(battle()->battleGetMorale(friendly), 0);
	auto control = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID());
	friendly->addNewBonus(control);
	ASSERT_EQ(battle()->battleGetOwnerHero(friendly), defenderSideHero);
	EXPECT_LT(battle()->battleGetMorale(friendly), 0);
	friendly->removeBonus(control);
	EXPECT_EQ(battle()->battleGetMorale(friendly), 0);
}

TEST(NewHorizonsRoyalStandardPacketsTest, CurrentRoundTripLegacyDefaultsAndOuterPrefixes)
{
	HeroOrderState order;
	order.command = HeroCommand::CHARGE;
	order.issuedRound = 1;
	order.royalStandardRecipientUnitIds = {1, 3};
	CMemorySerializer current;
	current.oser.version = ESerializationVersion::CURRENT;
	current.iser.version = ESerializationVersion::CURRENT;
	ASSERT_NO_THROW(current.oser & order);
	HeroOrderState restored;
	ASSERT_NO_THROW(current.iser & restored);
	EXPECT_EQ(order, restored);
	StartAction start(BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE));
	start.orderState = order;
	BattleHeroOrderStateChanged update;
	update.state = order;
	update.states = std::vector<HeroOrderState>{order};
	const auto checkOld = [](const auto & value)
	{
		CMemorySerializer older;
		older.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
		EXPECT_THROW(older.oser & value, std::runtime_error);
		EXPECT_TRUE(older.extractBuffer().empty());
	};
	checkOld(order);
	checkOld(start);
	checkOld(update);
	CGameState callback;
	callback.preInit(LIBRARY);
	SideInBattle side(&callback);
	side.orderStates = {order};
	checkOld(side);
	BattleStart battleStart;
	battleStart.battleID = BattleID(1);
	battleStart.info = std::make_unique<BattleInfo>(&callback);
	battleStart.info->getSide(BattleSide::ATTACKER).orderStates = {order};
	checkOld(*battleStart.info);
	checkOld(battleStart);
	order.royalStandardRecipientUnitIds.clear();
	CMemorySerializer legacy;
	legacy.oser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	legacy.iser.version = ESerializationVersion::NEW_HORIZONS_VETERAN_COHESION;
	ASSERT_NO_THROW(legacy.oser & order);
	ASSERT_NO_THROW(legacy.iser & restored);
	EXPECT_TRUE(restored.royalStandardRecipientUnitIds.empty());
	for(const auto & invalid : std::vector<std::vector<uint32_t>>{{3, 1}, {1, 1}, {UINT32_MAX}})
	{
		order.royalStandardRecipientUnitIds = invalid;
		CMemorySerializer wire;
		wire.oser.version = ESerializationVersion::CURRENT;
		EXPECT_THROW(wire.oser & order, std::runtime_error);
		EXPECT_TRUE(wire.extractBuffer().empty());
	}
}
