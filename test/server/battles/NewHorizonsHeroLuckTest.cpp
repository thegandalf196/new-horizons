/*
 * NewHorizonsHeroLuckTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/battle/NewHorizonsHeroLuck.h"
#include "../../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"

#include <cmath>
#include <limits>

namespace
{
using newHorizonsHeroLuck::Proc;
using newHorizonsHeroLuck::Receipt;

std::vector<std::byte> savedRandomizer(GameRandomizer & randomizer)
{
	CMemorySerializer wire;
	wire.oser & randomizer;
	return wire.extractBuffer();
}

class HeroLuckOrderTest : public HeroCommandFixture, public ::testing::WithParamInterface<HeroCommand>
{
protected:
	int chance = 100;
	CStack * friendly = nullptr;
	CStack * protector = nullptr;
	CStack * enemy = nullptr;
	CStack * shooter = nullptr;
	bool compoundCapacity = false;
	void selectOffer(const char * skill, const char * perk, MasteryLevel::Type rank)
	{
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(skill)), rank, ChangeValueMode::ABSOLUTE);
		const auto lookup = [this](const std::string & id) { return attackerSideHero->getPerkSkillRank(id); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(lookup, seed, attackerSideHero->getPerkOfferWeightContext());
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == skill && offers[choice].selection.perkId == perk)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(skill, perk));
					return;
				}
		}
		FAIL() << "No legal perk offer for compound capacity fixture";
	}
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
		JsonNode curve;
		curve.Vector().assign(4, JsonNode(chance));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, curve);
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_BIAS, JsonNode(0));
	}
	void prepareLuckyOrder()
	{
		startGame();
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:luck"));
		ASSERT_TRUE(skill.hasValue());
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:luck", "new-horizons:luck.fortuneSFavor"});
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:luck", "new-horizons:luck.luckyRecovery"});
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCK, BonusSource::OTHER, 1 - attackerSideHero->luckVal(), BonusSourceID()));
		ASSERT_EQ(attackerSideHero->luckVal(), 1);
		ASSERT_TRUE(newHorizonsHeroLuck::hasLuckyDay(attackerSideHero));
		if(compoundCapacity)
		{
			ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:discipline", "new-horizons:discipline.steadfast", MasteryLevel::BASIC));
			ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:discipline", "new-horizons:discipline.holdFast", MasteryLevel::ADVANCED));
			ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:discipline", "new-horizons:discipline.heroicSpirit", MasteryLevel::EXPERT));
			ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:command", "new-horizons:command.aggressiveCommander", MasteryLevel::BASIC));
			ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:command", "new-horizons:command.veteranCommander", MasteryLevel::ADVANCED));
			ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:command", "new-horizons:command.doubleCommand", MasteryLevel::EXPERT));
		}
		startBattle();
		friendly = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(70), 100);
		protector = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(87), 100);
		shooter = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(53), 100);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(71), 100);
		beginCombat();
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
	}
	void preparePaidHaste(bool erratic)
	{
		startGame();
		ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:luck", "new-horizons:luck.fortuneSFavor", MasteryLevel::BASIC));
		ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:luck", erratic
			? "new-horizons:luck.gambler" : "new-horizons:luck.serendipity", MasteryLevel::ADVANCED));
		if(!erratic)
		{
			ASSERT_NO_FATAL_FAILURE(selectOffer("new-horizons:luck", "new-horizons:luck.twistOfFate", MasteryLevel::EXPERT));
		}
		ASSERT_EQ(newHorizonsHeroLuck::hasErraticMana(attackerSideHero), erratic);
		ASSERT_EQ(newHorizonsHeroLuck::hasDruidicLuck(attackerSideHero), !erratic);
		ASSERT_FALSE(newHorizonsHeroLuck::hasLuckyDay(attackerSideHero));
		attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
			BonusType::LUCK, BonusSource::OTHER, 1 - attackerSideHero->luckVal(), BonusSourceID()));
		ASSERT_EQ(attackerSideHero->luckVal(), 1);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->initializeSpellPoints(20, 0);
		startBattle();
		beginCombat();
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleGetOwner(battle()->battleActiveUnit()), PlayerColor(0));
		ASSERT_TRUE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::HASTE));
	}
	BattleAction order(HeroCommand command)
	{
		if(command == HeroCommand::FOCUS_FIRE || command == HeroCommand::FLANK)
			return BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, command, enemy->unitId());
		if(command == HeroCommand::PROTECT)
			return BattleAction::makePairedHeroCommand(BattleSide::ATTACKER, command, protector->unitId(), friendly->unitId());
		if(command == HeroCommand::SECOND_WIND)
		{
			// Existing Second Wind fixture prerequisite: spent activation, not a
			// fabricated Hero Action allowance or forged positive Luck receipt.
			friendly->movedThisRound = true;
			return BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, command, friendly->unitId());
		}
		return BattleAction::makeHeroCommand(BattleSide::ATTACKER, command);
	}
};
}

TEST(HeroLuckReceiptTest, OneDrawRetainsConfiguredDiceAndSharesScaledThresholds)
{
	Receipt receipt{3, 1000, 120, 100, 0, 0};
	EXPECT_TRUE(receipt.triggered());
	EXPECT_FALSE(receipt.triggered(50));
	EXPECT_TRUE(receipt.triggered(200));
	EXPECT_EQ(receipt.draw, 100);
	EXPECT_FALSE(receipt.triggered(0));
	receipt.priorBias = -30000;
	EXPECT_FALSE(receipt.triggered());
	receipt.baseThreshold = 1000;
	receipt.draw = 1000;
	// Preserve existing biased mathematics: a capped weight is not an
	// unqualified guaranteed success when accumulated prior bias is negative.
	EXPECT_FALSE(receipt.triggered());
}

TEST(HeroLuckReceiptTest, RawBiasedReceiptMatchesExistingStreamAndUpdatesOnce)
{
	RandomGeneratorWithBias oldStream(917), rawStream(917);
	CRandomGenerator reference(917);
	int64_t bias = 0;
	for(int index = 0; index < 24; ++index)
	{
		const bool expected = oldStream.roll(37, 250, 60);
		const auto receipt = rawStream.rollReceipt(37, 250, 60);
		const int draw = reference.nextInt(1, 250);
		const bool independentlyExpected = static_cast<int64_t>(draw) * 250 - bias <= 37 * 250;
		EXPECT_EQ(receipt.draw, draw);
		EXPECT_EQ(receipt.priorBias, bias);
		EXPECT_EQ(expected, independentlyExpected);
		bias += independentlyExpected ? -213 * 60 : 37 * 60;
		EXPECT_EQ(receipt.triggered(), expected);
		EXPECT_EQ(receipt.triggered(), expected); // pure read, not another draw
		EXPECT_EQ(receipt.diceSize, 250);
		EXPECT_EQ(receipt.baseThreshold, 37);
	}
}

TEST(HeroLuckReceiptTest, UnsupportedBiasRepresentationRejectsBeforeDrawOrMutation)
{
	RandomizationBias bias;
	CRandomGenerator actual(917), control(917);
	EXPECT_THROW(bias.rollReceipt(actual, std::numeric_limits<int32_t>::max(),
		std::numeric_limits<int32_t>::max(), 100), std::runtime_error);
	const auto receipt = bias.rollReceipt(actual, 37, 250, 60);
	EXPECT_EQ(receipt.draw, control.nextInt(1, 250));
	EXPECT_EQ(receipt.priorBias, 0);
}

TEST(HeroLuckReceiptTest, SettingsAbsentDefaultsPresentCompleteAndUnsafeNumbersReject)
{
	JsonNode absent;
	EXPECT_NO_THROW(newHorizonsHeroLuck::validateRules(absent));
	EXPECT_EQ(newHorizonsHeroLuck::orderPotency(50, absent, true), 100);
	EXPECT_EQ(newHorizonsHeroLuck::orderPotency(100, absent, true), 200);
	EXPECT_EQ(newHorizonsHeroLuck::orderPotency(27, absent, false), 27);
	EXPECT_EQ(newHorizonsHeroLuck::chanceScalePercent(absent, Proc::LuckyDay), 100);
	JsonNode partial;
	partial["heroLuck"]["orderMultiplierPercent"] = JsonNode(200);
	EXPECT_THROW(newHorizonsHeroLuck::validateRules(partial), std::runtime_error);
	for(const double unsafe : {std::numeric_limits<double>::quiet_NaN(),
		std::numeric_limits<double>::infinity(), 1e300, -1., 1001., 1.5})
	{
		JsonNode rules;
		rules["heroLuck"]["orderMultiplierPercent"].Float() = unsafe;
		EXPECT_THROW(newHorizonsHeroLuck::orderMultiplierPercent(rules), std::runtime_error);
	}
}

TEST(HeroLuckReceiptTest, OrderCodecPreservesDrawAndRejectsOldWriterBeforePrefix)
{
	HeroOrderState state;
	state.command = HeroCommand::CHARGE;
	state.issuedRound = 1;
	state.heroLuck = Receipt{1, 100, 4, 1, 0, static_cast<uint8_t>(Proc::LuckyDay)};
	CMemorySerializer wire;
	wire.oser & state;
	HeroOrderState restored;
	wire.iser & restored;
	EXPECT_EQ(restored, state);
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_COMBAT_ACTIVATION_SNAPSHOT;
	EXPECT_THROW(old.oser & state, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
	state.heroLuck->mark(Proc::DruidicLuck);
	CMemorySerializer malformed;
	EXPECT_THROW(malformed.oser & state, std::runtime_error);
	EXPECT_TRUE(malformed.extractBuffer().empty());
}

TEST(HeroLuckReceiptTest, CapturedOrderConsumerDoublesAllEightNumericPotenciesNotTheirBudgets)
{
	JsonNode magicRules;
	for(const auto command : {HeroCommand::CHARGE, HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE,
		HeroCommand::HOLD_THE_LINE, HeroCommand::BRACE, HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::SECOND_WIND})
	{
		HeroOrderState state;
		state.command = command;
		state.issuedRound = 2;
		state.protectInterceptionLimit = 1;
		state.heroLuck = Receipt{1, 100, 100, 1, 0, static_cast<uint8_t>(Proc::LuckyDay)};
		const auto before = state;
		EXPECT_EQ(heroCommands::orderPotency(50, state, magicRules), 100);
		EXPECT_EQ(heroCommands::orderPotency(200, state, magicRules), 400)
			<< "Do not apply the base-formula cap a second time after critical potency";
		EXPECT_EQ(heroCommands::orderPotency(50, 60, state, magicRules), 110)
			<< "Crown's completed +10 companion increment must not become +20";
		EXPECT_EQ(heroCommands::orderPotency(190, 200, state, magicRules), 390)
			<< "Keep the pre-Luck capped companion delta, without recapping the critical intrinsic";
		EXPECT_EQ(heroCommands::orderPotency(50, 100, state, magicRules), 150)
			<< "Second Wind's capped companion uplift remains unchanged";
		EXPECT_EQ(state, before) << "The numeric consumer must not add actions, targets or duration";
		state.heroLuck->procFlags = 0;
		EXPECT_EQ(heroCommands::orderPotency(50, state, magicRules), 50);
		EXPECT_EQ(heroCommands::orderPotency(50, 60, state, magicRules), 60);
	}
}

TEST_P(HeroLuckOrderTest, EachAcceptedCanonicalOrderCapturesOneLuckyDayWithoutChangingScopeOrCount)
{
	ASSERT_NO_FATAL_FAILURE(prepareLuckyOrder());
	const auto action = order(GetParam());
	const auto secondWindBase = battle()->calculateDmgRange(BattleAttackInfo(friendly, enemy, 0, false)).damage;
	std::vector<uint32_t> targets;
	for(const auto & destination : action.target)
		targets.push_back(destination.unitValue);
	const auto normal = battle()->battlePrepareHeroOrderState(BattleSide::ATTACKER, GetParam(), targets);
	ASSERT_TRUE(normal);
	const auto startedBefore = server.startedActions.size();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	const StartAction * accepted = nullptr;
	size_t matchingActions = 0;
	for(size_t index = startedBefore; index < server.startedActions.size(); ++index)
	{
		const auto & started = server.startedActions[index];
		if(started.ba.actionType == EActionType::HERO_COMMAND && started.ba.side == action.side
			&& started.ba.command == GetParam())
		{
			accepted = &started;
			++matchingActions;
		}
	}
	ASSERT_EQ(matchingActions, 1);
	ASSERT_NE(accepted, nullptr);
	ASSERT_TRUE(accepted->orderState);
	auto acceptedWithoutReceipt = *accepted->orderState;
	acceptedWithoutReceipt.heroLuck.reset();
	EXPECT_EQ(acceptedWithoutReceipt, *normal)
		<< "The accepted Order must preserve targets, duration, allowance and trigger counts";
	const auto state = battle()->getHeroOrderState(BattleSide::ATTACKER, GetParam());
	ASSERT_TRUE(state);
	ASSERT_TRUE(state->heroLuck);
	EXPECT_TRUE(state->luckyDay());
	EXPECT_EQ(state->heroLuck->heroValue, 1);
	EXPECT_EQ(state->heroLuck->baseThreshold, 100);
	EXPECT_EQ(state->heroLuck->diceSize, 100);
	auto expectedStored = *accepted->orderState;
	// The synchronous Flow starts Second Wind's one activation after StartAction.
	if(GetParam() == HeroCommand::SECOND_WIND)
		expectedStored.secondWindActive = true;
	EXPECT_EQ(*state, expectedStored)
		<< "Only the ordinary Second Wind activation transition may change the accepted Order";
	if(GetParam() == HeroCommand::FOCUS_FIRE)
	{
		const auto mark = battle()->battleGetFocusFireState(BattleSide::ATTACKER);
		ASSERT_TRUE(mark);
		EXPECT_EQ(battle()->battleTargetedRangedCommandPercent(shooter, enemy, true, false),
			2 * mark->rangedDamagePercent);
	}
	if(GetParam() == HeroCommand::FLANK)
	{
		const int ordinary = heroCommands::coefficient(
			battle()->getHeroCommandRules()["commands"]["flank"]["effects"]["meleeDamagePercent"],
			*attackerSideHero, state->warcastingBonusPercent, state->divineMandateEfficiencyBonusPercent());
		EXPECT_EQ(battle()->battleHeroOrderFlankMeleeDamagePercent(BattleAttackInfo(friendly, enemy, 0, false)),
			2 * ordinary);
	}
	if(GetParam() == HeroCommand::SECOND_WIND)
	{
		EXPECT_TRUE(state->secondWindActive);
		EXPECT_EQ(battle()->getActiveStackID(), friendly->unitId());
		EXPECT_EQ(state->primaryTargetUnitId, friendly->unitId());
		const int ordinaryPercent = newHorizonsDivineMandate::crownSecondWindPercent(
			*attackerSideHero, *state, friendly->unitId());
		const auto actual = battle()->calculateDmgRange(BattleAttackInfo(friendly, enemy, 0, false)).damage;
		ASSERT_LT(secondWindBase.max * 2, enemy->getAvailableHealth()) << "No lethal cap may hide potency";
		EXPECT_NEAR(actual.min, secondWindBase.min * (2.0 * ordinaryPercent / 100), 2.0);
		EXPECT_NEAR(actual.max, secondWindBase.max * (2.0 * ordinaryPercent / 100), 2.0);
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), BattleAction::makeDefend(friendly)));
		const auto completed = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::SECOND_WIND);
		EXPECT_TRUE(!completed || !completed->secondWindActive) << "Critical strength cannot grant another activation";
	}
}

INSTANTIATE_TEST_SUITE_P(AllEight, HeroLuckOrderTest, ::testing::Values(
	HeroCommand::CHARGE, HeroCommand::FOCUS_FIRE, HeroCommand::RIPOSTE, HeroCommand::HOLD_THE_LINE,
	HeroCommand::BRACE, HeroCommand::PROTECT, HeroCommand::FLANK, HeroCommand::SECOND_WIND));

TEST_F(HeroLuckOrderTest, InvalidOrderDoesNotSeedDrawOrChangeSavedRandomizer)
{
	ASSERT_NO_FATAL_FAILURE(prepareLuckyOrder());
	const auto before = savedRandomizer(*gameHandler->randomizer);
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeTargetedHeroCommand(BattleSide::ATTACKER, HeroCommand::FOCUS_FIRE, friendly->unitId())));
	EXPECT_EQ(savedRandomizer(*gameHandler->randomizer), before);
	EXPECT_TRUE(battle()->getHeroOrderStates(BattleSide::ATTACKER).empty());
}

TEST_F(HeroLuckOrderTest, PaidNondamagingHasteWithOnlyDruidicConsumesActionAndManaWithoutLuckDraw)
{
	ASSERT_NO_FATAL_FAILURE(preparePaidHaste(false));
	const auto * spell = SpellID(SpellID::HASTE).toSpell();
	ASSERT_TRUE(spell->isPositive());
	ASSERT_FALSE(spells::heroLuckDamageEligible(*spell, battle()->getMagicRules()));
	const auto * target = battle()->battleActiveUnit();
	const auto marker = Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)));
	ASSERT_TRUE(target->getAllBonuses(marker)->empty());
	const auto health = target->getAvailableHealth();
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell, attackerSideHero);
	ASSERT_GT(cost, 0);
	ASSERT_GE(mana, cost);
	const auto castCount = battle()->getSide(BattleSide::ATTACKER).castSpellsCount;
	const auto before = savedRandomizer(*gameHandler->randomizer);
	GameRandomizer expected(*gameState());
	CMemorySerializer rngWire(before);
	rngWire.iser & expected;
	ASSERT_EQ(savedRandomizer(expected), before);
	// Positive Haste has neither ordinary hostile-resistance nor mirror draws.
	// No Hero Luck stream is seeded, advanced or rebiased for Druidic alone.
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(0)));
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_EQ(casts.size(), 1u);
	EXPECT_FALSE(casts.front().announcement.counterspellNegated);
	EXPECT_FALSE(casts.front().announcement.heroLuck);
	EXPECT_EQ(casts.front().announcement.paidHeroManaCost, cost);
	EXPECT_EQ(casts.front().damage, 0);
	EXPECT_FALSE(target->getAllBonuses(marker)->empty());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).castSpellsCount, castCount + 1);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).heroSpellCastCompleted);
	EXPECT_FALSE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::HASTE));
	EXPECT_EQ(savedRandomizer(*gameHandler->randomizer), savedRandomizer(expected));
}

TEST_F(HeroLuckOrderTest, PaidNondamagingHasteWithOnlyErraticUsesOneLuckDrawAndRefunds)
{
	ASSERT_NO_FATAL_FAILURE(preparePaidHaste(true));
	const auto * spell = SpellID(SpellID::HASTE).toSpell();
	ASSERT_TRUE(spell->isPositive());
	ASSERT_FALSE(spells::heroLuckDamageEligible(*spell, battle()->getMagicRules()));
	const auto mana = attackerSideHero->getManaAvailable();
	const auto cost = battle()->battleGetSpellCost(spell, attackerSideHero);
	const auto refund = cost * newHorizonsHeroLuck::manaRefundPercent(battle()->getMagicRules()) / 100;
	ASSERT_GT(refund, 0);
	ASSERT_GE(mana, cost);
	const auto before = savedRandomizer(*gameHandler->randomizer);
	GameRandomizer expected(*gameState());
	CMemorySerializer rngWire(before);
	rngWire.iser & expected;
	auto receipt = expected.rollHeroLuck(attackerSideHero->id, attackerSideHero->luckVal());
	ASSERT_TRUE(receipt);
	ASSERT_TRUE(receipt->triggered(newHorizonsHeroLuck::chanceScalePercent(battle()->getMagicRules(), Proc::ErraticMana)));
	receipt->mark(Proc::ErraticMana);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), heroAction(0)));
	const auto casts = server.castsOf(SpellID::HASTE);
	ASSERT_EQ(casts.size(), 1u);
	ASSERT_TRUE(casts.front().announcement.heroLuck);
	EXPECT_EQ(*casts.front().announcement.heroLuck, *receipt);
	EXPECT_FALSE(casts.front().announcement.heroLuck->has(Proc::DruidicLuck));
	EXPECT_FALSE(casts.front().announcement.counterspellNegated);
	EXPECT_EQ(casts.front().announcement.paidHeroManaCost, cost);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana - cost + refund);
	EXPECT_TRUE(battle()->getSide(BattleSide::ATTACKER).heroSpellCastCompleted);
	EXPECT_FALSE(battle()->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::HASTE));
	EXPECT_EQ(savedRandomizer(*gameHandler->randomizer), savedRandomizer(expected));
}

TEST_F(HeroLuckOrderTest, DedicatedHeroStreamsRemainIsolatedAndRoundTripTheirNextDraw)
{
	chance = 37; // nonconstant outcomes expose accidental creature-stream reuse
	ASSERT_NO_FATAL_FAILURE(prepareLuckyOrder());
	GameRandomizer source(*gameState());
	GameRandomizer creatureControl(*gameState());
	source.setSeed(3017);
	creatureControl.setSeed(3017);
	EXPECT_EQ(source.rollGoodLuck(attackerSideHero->id, 2), creatureControl.rollGoodLuck(attackerSideHero->id, 2));
	ASSERT_TRUE(source.rollHeroLuck(attackerSideHero->id, 2));
	ASSERT_TRUE(source.rollHeroMorale(attackerSideHero->id, 3));
	for(int index = 0; index < 8; ++index)
		EXPECT_EQ(source.rollGoodLuck(attackerSideHero->id, 2), creatureControl.rollGoodLuck(attackerSideHero->id, 2));
	CMemorySerializer wire;
	wire.oser & source;
	GameRandomizer restored(*gameState());
	wire.iser & restored;
	EXPECT_EQ(source.rollHeroLuck(attackerSideHero->id, 2), restored.rollHeroLuck(attackerSideHero->id, 2));
	EXPECT_EQ(source.rollHeroMorale(attackerSideHero->id, 3), restored.rollHeroMorale(attackerSideHero->id, 3));
	EXPECT_EQ(source.rollGoodLuck(attackerSideHero->id, 2), restored.rollGoodLuck(attackerSideHero->id, 2));
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_COMBAT_ACTIVATION_SNAPSHOT;
	EXPECT_THROW(old.oser & source, std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

TEST_F(HeroLuckOrderTest, CompoundDoubleCommandAndHeroicSpiritCapacityRejectsBeforeEitherDrawOrApply)
{
	compoundCapacity = true;
	ASSERT_NO_FATAL_FAILURE(prepareLuckyOrder());
	ASSERT_TRUE(heroCommands::hasDoubleCommand(attackerSideHero));
	ASSERT_TRUE(newHorizonsHeroicSpirit::hasPerk(attackerSideHero));
	ASSERT_GT(attackerSideHero->moraleVal(), 0);
	// Synthetic identity-space boundary only; all skill selections and the
	// command request use ordinary validated production admission.
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.heroActionAllowances.nextGrantId = std::numeric_limits<uint32_t>::max() - 1;
	ASSERT_NO_THROW(side.heroActionAllowances.validateShape());
	const auto allowances = side.heroActionAllowances;
	const auto doubleCommand = side.doubleCommandState;
	const auto orders = side.orderStates;
	const auto rng = savedRandomizer(*gameHandler->randomizer);
	EXPECT_FALSE(issue(HeroCommand::CHARGE));
	EXPECT_EQ(side.heroActionAllowances, allowances);
	EXPECT_EQ(side.doubleCommandState, doubleCommand);
	EXPECT_EQ(side.orderStates, orders);
	EXPECT_EQ(savedRandomizer(*gameHandler->randomizer), rng);
}

TEST_F(HeroLuckOrderTest, ZeroCreatureLuckChanceDoesNotCreateOrAdvanceActorStreams)
{
	chance = 37;
	startGame();
	JsonNode normal;
	normal.Vector().assign(4, JsonNode(37));
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, normal);
	GameRandomizer source(*gameState()), control(*gameState());
	source.setSeed(3017);
	control.setSeed(3017);
	const auto existing = attackerSideHero->id;
	const auto fresh = defenderSideHero->id;
	EXPECT_EQ(source.rollGoodLuck(existing, 2), control.rollGoodLuck(existing, 2));
	EXPECT_EQ(source.rollBadLuck(existing, 2), control.rollBadLuck(existing, 2));
	const auto before = savedRandomizer(source);
	ASSERT_EQ(before, savedRandomizer(control));
	JsonNode zero;
	zero.Vector().assign(4, JsonNode(0));
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, zero);
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, zero);
	for(const auto actor : {existing, fresh})
	{
		EXPECT_FALSE(source.rollGoodLuck(actor, 2));
		EXPECT_FALSE(source.rollBadLuck(actor, 2));
		EXPECT_EQ(savedRandomizer(source), before);
	}
	for(int positiveChance : {37, 100})
	{
		normal.Vector().assign(4, JsonNode(positiveChance));
		gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, normal);
		gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_BAD_LUCK_CHANCE, normal);
		for(int index = 0; index < 8; ++index)
			for(const auto actor : {existing, fresh})
			{
				EXPECT_EQ(source.rollGoodLuck(actor, 2), control.rollGoodLuck(actor, 2));
				EXPECT_EQ(source.rollBadLuck(actor, 2), control.rollBadLuck(actor, 2));
				EXPECT_EQ(savedRandomizer(source), savedRandomizer(control));
			}
	}
}

TEST_F(HeroLuckOrderTest, EmptyCreatureLuckCurveKeepsItsExistingSeedAllocationOrder)
{
	startGame();
	JsonNode empty;
	empty.setType(JsonNode::JsonType::DATA_VECTOR);
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, empty);
	GameRandomizer source(*gameState()), control(*gameState());
	source.setSeed(3017);
	control.setSeed(3017);
	const auto before = savedRandomizer(source);
	(void)control.getDefault().nextInt(); // Existing empty-curve path allocates one actor seed.
	EXPECT_FALSE(source.rollGoodLuck(attackerSideHero->id, 2));
	EXPECT_NE(savedRandomizer(source), before);
	EXPECT_EQ(source.getDefault().nextInt(), control.getDefault().nextInt());
	const auto initialized = savedRandomizer(source);
	EXPECT_FALSE(source.rollGoodLuck(attackerSideHero->id, 2));
	EXPECT_EQ(savedRandomizer(source), initialized);
}
