/*
 * NewHorizonsHeroicSpiritTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../../lib/mapObjects/army/CStackBasicDescriptor.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <vcmi/Environment.h>

namespace
{
class SpiritEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SpiritEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsHeroicSpiritTest : public HeroCommandFixture
{
protected:
	CStack * actor = nullptr;
	CStack * enemy = nullptr;
	bool chanceSucceeds = true;
	bool selectDoubleCommand = false;
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires New Horizons";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode chance;
		for(int i = 0; i < 10; ++i)
			chance.Vector().emplace_back(chanceSucceeds ? 100 : 0);
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_MORALE_CHANCE, chance);
		loaded->overrideGameSetting(EGameSettings::COMBAT_MORALE_DICE_SIZE, JsonNode(100));
		// The live randomizer reads captured New Horizons Morale before legacy settings.
		auto magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
		magic["morale"]["goodChance"] = chance;
		magic["morale"]["diceSize"].Integer() = 100;
		magic.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
	}
	void select(const char * perk, MasteryLevel::Type rank, const char * skill = newHorizonsHeroicSpirit::SKILL)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)),
			rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == skill
					&& offers[choice].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No legal offer for " << perk;
	}
	void activate(BattleUnitTurnReason reason)
	{
		BattleSetActiveStack pack;
		pack.battleID = BattleID(0);
		pack.stack = actor->unitId();
		pack.reason = reason;
		gameHandler->sendAndApply(pack);
	}
	void prepare(bool selected = true)
	{
		startGame();
		if(selected)
		{
			select("new-horizons:discipline.steadfast", MasteryLevel::BASIC);
			select("new-horizons:discipline.holdFast", MasteryLevel::ADVANCED);
			select(newHorizonsHeroicSpirit::PERK, MasteryLevel::EXPERT);
		}
		if(selectDoubleCommand)
		{
			ASSERT_NO_FATAL_FAILURE(select("new-horizons:command.aggressiveCommander", MasteryLevel::BASIC, "new-horizons:command"));
			ASSERT_NO_FATAL_FAILURE(select("new-horizons:command.veteranCommander", MasteryLevel::ADVANCED, "new-horizons:command"));
			ASSERT_NO_FATAL_FAILURE(select("new-horizons:command.doubleCommand", MasteryLevel::EXPERT, "new-horizons:command"));
		}
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		actor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(89), 20);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(94), 1000);
		actor->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::MORALE, BonusSource::OTHER, 3, BonusSourceID()));
		beginCombat();
		activate(BattleUnitTurnReason::TURN_QUEUE);
		ASSERT_GT(battle()->battleGetMorale(actor), 0);
		ASSERT_EQ(actor->counterAttacks.total(), 1);
	}
	void earn()
	{
		ASSERT_FALSE(actor->hadMorale);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeMove(actor, BattleHex(88))));
		ASSERT_TRUE(actor->hadMorale);
		ASSERT_EQ(battle()->battleActiveUnit(), actor);
	}
	void setHeroMorale(int value)
	{
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
			BonusType::MORALE, BonusSource::OTHER, value - attackerSideHero->moraleVal(), BonusSourceID()));
		ASSERT_EQ(attackerSideHero->moraleVal(), value);
	}
};
}

TEST_F(NewHorizonsHeroicSpiritTest, AcceptedFlexibleOrderGrantsOneOrderButBonusOrderCannotChain)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(3));
	const auto round = battle()->getRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto & granted = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(granted.remainingCounts(round), (HeroActionAllowanceState::Counts{0, 1, 0}));
	ASSERT_EQ(granted.heroicSpiritProcessedReceipts.size(), 1u);
	const auto processed = granted.heroicSpiritProcessedReceipts;
	const auto bonus = battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(bonus);
	EXPECT_EQ(bonus->source, HeroActionAllowanceState::GrantSource::HEROIC_SPIRIT);
	EXPECT_FALSE(granted.eligibleAllowance(HeroActionAllowanceState::ActionKind::SPELL, round));
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(round),
		(HeroActionAllowanceState::Counts{}));
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).heroicSpiritProcessedReceipts, processed);
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_EQ(actor->counterAttacks.total(), 1);
}

TEST_F(NewHorizonsHeroicSpiritTest, RequiredDoubleCommandRunsBeforeHeroicBonusAndNeitherBonusChainsMorale)
{
	selectDoubleCommand = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(newHorizonsHeroicSpirit::hasPerk(attackerSideHero));
	ASSERT_TRUE(heroCommands::hasDoubleCommand(attackerSideHero));
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(3));
	const auto round = battle()->getRound();
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto & ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	ASSERT_EQ(ledger.remainingCounts(round), (HeroActionAllowanceState::Counts{0, 2, 0}));
	ASSERT_EQ(ledger.heroicSpiritProcessedReceipts.size(), 1u);
	const auto processed = ledger.heroicSpiritProcessedReceipts;
	ASSERT_TRUE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto required = battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(required);
	EXPECT_EQ(required->source, HeroActionAllowanceState::GrantSource::DOUBLE_COMMAND);
	ASSERT_TRUE(issue(HeroCommand::HOLD_THE_LINE));
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	EXPECT_TRUE(battle()->getDoubleCommandState(BattleSide::ATTACKER).used);
	EXPECT_EQ(ledger.remainingCounts(round), (HeroActionAllowanceState::Counts{0, 1, 0}));
	EXPECT_EQ(ledger.heroicSpiritProcessedReceipts, processed);
	const auto moraleBonus = battle()->battleGetOrderActionAllowance(BattleSide::ATTACKER);
	ASSERT_TRUE(moraleBonus);
	EXPECT_EQ(moraleBonus->source, HeroActionAllowanceState::GrantSource::HEROIC_SPIRIT);
	ASSERT_TRUE(issue(HeroCommand::RIPOSTE));
	EXPECT_EQ(ledger.remainingCounts(round), (HeroActionAllowanceState::Counts{}));
	EXPECT_EQ(ledger.heroicSpiritProcessedReceipts, processed);
	EXPECT_FALSE(battle()->battleHasPendingDoubleCommand(BattleSide::ATTACKER));
	const auto & orders = battle()->getHeroOrderStates(BattleSide::ATTACKER);
	ASSERT_EQ(orders.size(), 3u);
	EXPECT_EQ(orders[0].command, HeroCommand::CHARGE);
	EXPECT_EQ(orders[1].command, HeroCommand::HOLD_THE_LINE);
	EXPECT_EQ(orders[2].command, HeroCommand::RIPOSTE);
}

TEST_F(NewHorizonsHeroicSpiritTest, UnusedMoraleOrderAndProcessedReceiptExpireAtNextRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(3));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	ASSERT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER).remainingCounts(battle()->getRound()).orderActions, 1u);
	advanceRound();
	const auto & ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(ledger.remainingCounts(battle()->getRound()), (HeroActionAllowanceState::Counts{1, 0, 0}));
	EXPECT_TRUE(ledger.heroicSpiritProcessedReceipts.empty());
}

TEST_F(NewHorizonsHeroicSpiritTest, ZeroHeroMoraleNeverGrantsAnOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(0));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto & ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(ledger.remainingCounts(battle()->getRound()), (HeroActionAllowanceState::Counts{}));
	EXPECT_TRUE(ledger.heroicSpiritProcessedReceipts.empty());
}

TEST_F(NewHorizonsHeroicSpiritTest, NegativeHeroMoraleNeverPenalizesOrGrantsAnOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(-1));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto & ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(ledger.remainingCounts(battle()->getRound()), (HeroActionAllowanceState::Counts{}));
	EXPECT_TRUE(ledger.heroicSpiritProcessedReceipts.empty());
	EXPECT_EQ(battle()->battleCastSpells(BattleSide::ATTACKER), 0);
}

TEST_F(NewHorizonsHeroicSpiritTest, PositiveHeroMoraleWithoutPerkNeverGrantsAnOrder)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(3));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto & ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(ledger.remainingCounts(battle()->getRound()), (HeroActionAllowanceState::Counts{}));
	EXPECT_TRUE(ledger.heroicSpiritProcessedReceipts.empty());
}

TEST_F(NewHorizonsHeroicSpiritTest, DisabledMoraleChanceRecordsResolutionWithoutGrant)
{
	chanceSucceeds = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(3));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto & ledger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(ledger.remainingCounts(battle()->getRound()), (HeroActionAllowanceState::Counts{}));
	EXPECT_EQ(ledger.heroicSpiritProcessedReceipts.size(), 1u);
}

TEST_F(NewHorizonsHeroicSpiritTest, ActualCreatureMoraleStillOccursWithoutRetiredRetaliationGrant)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	actor->counterAttacks.use();
	ASSERT_EQ(actor->counterAttacks.available(), 0);
	ASSERT_NO_FATAL_FAILURE(earn());
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_FALSE(actor->heroicSpiritMoralePending);
	EXPECT_EQ(actor->counterAttacks.total(), 1);
	EXPECT_EQ(actor->counterAttacks.available(), 0);
	EXPECT_TRUE(battle()->getHeroActionAllowances(BattleSide::ATTACKER).heroicSpiritProcessedReceipts.empty());
	SpiritEnvironment environment(gameState());
	auto parent = std::make_shared<HypotheticBattle>(&environment,
		std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0)));
	auto child = std::make_shared<HypotheticBattle>(&environment, parent);
	auto projected = child->getForUpdate(actor->unitId());
	projected->hadMorale = false; // Isolated already-earned projection control.
	ASSERT_TRUE(child->projectEarnedMoraleActivation(actor->unitId()));
	EXPECT_EQ(projected->counterAttacks.total(), 1);
	EXPECT_FALSE(projected->heroicSpiritRetaliation);
	EXPECT_FALSE(actor->heroicSpiritRetaliation);
	EXPECT_TRUE(actor->hadMorale);
}

TEST_F(NewHorizonsHeroicSpiritTest, LegacyCreatureFieldsRoundTripButAreInertAndMalformedStateStillRejects)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = actor->acquireState();
	state->heroicSpiritRetaliation = true;
	state->heroicSpiritMoralePending = false;
	const auto snapshot = state->save();
	auto copy = actor->acquireState();
	ASSERT_NO_THROW(copy->load(snapshot));
	EXPECT_TRUE(copy->heroicSpiritRetaliation);
	EXPECT_EQ(copy->counterAttacks.total(), 1);
	EXPECT_FALSE(newHorizonsHeroicSpirit::canGrantEarnedMorale(*copy, attackerSideHero));
	EXPECT_FALSE(newHorizonsHeroicSpirit::grantEarnedMorale(*copy, attackerSideHero));
	newHorizonsHeroicSpirit::beginActivation(*copy, BattleUnitTurnReason::MORALE);
	EXPECT_FALSE(copy->heroicSpiritRetaliation);
	EXPECT_FALSE(copy->heroicSpiritMoralePending);
	auto malformed = snapshot;
	malformed["state"]["heroicSpiritRetaliation"].String() = "not a boolean";
	const auto before = copy->save();
	EXPECT_THROW(copy->load(malformed), std::runtime_error);
	EXPECT_EQ(copy->save(), before);
	malformed = snapshot;
	malformed["state"]["heroicSpiritRetaliation"].Bool() = false;
	malformed["state"]["heroicSpiritMoralePending"].Bool() = true;
	EXPECT_THROW(copy->load(malformed), std::runtime_error);
	EXPECT_EQ(copy->save(), before);

	CStackBasicDescriptor descriptor(actor->creatureId(), actor->unitBaseAmount());
	CStack native(&descriptor, actor->unitOwner(), static_cast<int>(actor->unitId()), actor->unitSide(), actor->unitSlot());
	native.heroicSpiritRetaliation = true; // Legacy descriptor, not a new producer.
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & native);
	CStack decoded;
	ASSERT_NO_THROW(current.iser & decoded);
	EXPECT_TRUE(decoded.heroicSpiritRetaliation);
	ASSERT_NO_THROW(decoded.localInit(battle()));
	EXPECT_TRUE(decoded.heroicSpiritRetaliation);
	EXPECT_EQ(decoded.counterAttacks.total(), 1);
	const auto reject = [](const auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
		EXPECT_THROW(bytes.oser & value, std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	};
	UnitChanges change(actor->unitId(), UnitChanges::EOperation::UPDATE);
	change.data = snapshot;
	reject(change);
	BattleUnitsChanged update;
	update.battleID = BattleID(0);
	update.changedStacks = {change};
	reject(update);
	reject(native);
	native.heroicSpiritRetaliation = false;
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
	old.iser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
	ASSERT_NO_THROW(old.oser & native);
	CStack oldDecoded;
	ASSERT_NO_THROW(old.iser & oldDecoded);
	EXPECT_FALSE(oldDecoded.heroicSpiritRetaliation);
	EXPECT_FALSE(oldDecoded.heroicSpiritMoralePending);
}

TEST_F(NewHorizonsHeroicSpiritTest, LegacyCreatureEventIsReadableButCannotProduceRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleTriggerEffect grant;
	grant.battleID = BattleID(0);
	grant.stackID = actor->unitId();
	grant.effect = BonusType::MORALE;
	grant.val = 1;
	grant.heroicSpiritGrant = true;
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & grant);
	BattleTriggerEffect decoded;
	ASSERT_NO_THROW(current.iser & decoded);
	EXPECT_TRUE(decoded.heroicSpiritGrant);
	const auto before = actor->save();
	EXPECT_THROW(gameHandler->sendAndApply(decoded), std::runtime_error);
	EXPECT_EQ(actor->save(), before);
	CMemorySerializer oldWriter;
	oldWriter.oser.version = ESerializationVersion::NEW_HORIZONS_OFFENSIVE_START_SPECIALTIES;
	EXPECT_THROW(oldWriter.oser & grant, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
}
