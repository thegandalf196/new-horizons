/*
 * NewHorizonsHeroicSpiritTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsHeroicSpirit.h"
#include "../../../lib/battle/NewHorizonsHeroLuck.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/spells/CSpell.h"
#include "../../../lib/spells/ISpellMechanics.h"
#include "../../../lib/spells/Problem.h"
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
	bool selectLuckyDay = false;
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
		if(selectLuckyDay)
		{
			JsonNode luckChance;
			luckChance.Vector().assign(4, JsonNode(100));
			loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, luckChance);
			loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
			loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_BIAS, JsonNode(0));
		}
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
		if(selectLuckyDay)
		{
			ASSERT_NO_FATAL_FAILURE(select("new-horizons:luck.fortuneSFavor", MasteryLevel::BASIC, "new-horizons:luck"));
			ASSERT_NO_FATAL_FAILURE(select("new-horizons:luck.luckyRecovery", MasteryLevel::ADVANCED, "new-horizons:luck"));
			attackerSideHero->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::LUCK, BonusSource::OTHER, 1 - attackerSideHero->luckVal(), BonusSourceID()));
			attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
			giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
			attackerSideHero->addSpellToSpellbook(SpellID::HASTE);
			attackerSideHero->initializeSpellPoints(20, 0);
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

TEST_F(NewHorizonsHeroicSpiritTest, ComposedBattleAndRngReconstructionConsumesSavedOrderOnlyGrantWithoutRecursion)
{
	selectLuckyDay = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(setHeroMorale(3));
	ASSERT_EQ(attackerSideHero->luckVal(), 1);
	ASSERT_TRUE(newHorizonsHeroLuck::hasLuckyDay(attackerSideHero));
	const auto * haste = SpellID(SpellID::HASTE).toSpell();
	spells::BattleCast validSpell(battle(), attackerSideHero, spells::Mode::HERO, haste);
	auto spellMechanics = haste->battleMechanics(&validSpell);
	spells::detail::ProblemImpl problem;
	ASSERT_TRUE(spellMechanics->canBeCastAt(spells::Target{spells::Destination(actor)}, problem));
	ASSERT_TRUE(issue(HeroCommand::CHARGE));
	const auto sourceOrder = battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE);
	ASSERT_TRUE(sourceOrder);
	ASSERT_TRUE(sourceOrder->heroLuck);
	ASSERT_TRUE(sourceOrder->heroLuck->has(newHorizonsHeroLuck::Proc::LuckyDay));
	ASSERT_TRUE(sourceOrder->heroMorale);
	ASSERT_TRUE(sourceOrder->heroMorale->has(newHorizonsHeroLuck::Proc::HeroicSpirit));
	const auto sourceLedger = battle()->getHeroActionAllowances(BattleSide::ATTACKER);
	const auto round = battle()->getRound();
	ASSERT_EQ(sourceLedger.remainingCounts(round), (HeroActionAllowanceState::Counts{0, 1, 0}));
	ASSERT_EQ(sourceLedger.heroicSpiritProcessedReceipts.size(), 1u);
	const auto actorId = actor->unitId();
	const auto enemyId = enemy->unitId();
	const auto actorState = actor->acquireState()->save();
	const auto enemyState = enemy->acquireState()->save();
	const auto initialNormal = battle()->getSide(BattleSide::ATTACKER).initialNormalSpellPoints;
	const auto initialBuffer = battle()->getSide(BattleSide::ATTACKER).initialBufferSpellPoints;
	const auto savedRandomizer = [](GameRandomizer & randomizer)
	{
		CMemorySerializer bytes;
		bytes.oser & randomizer;
		return bytes.extractBuffer();
	};
	const auto sourceRandomizer = savedRandomizer(*gameHandler->randomizer);

	// Active battles are NOT in world saves. Reconstruct a separate world plus
	// the established detached BattleStart/unit-state packets, not an ordinary
	// mid-battle save/resume protocol; the original graph remains untouched.
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(gameState()->saveToMemory());
	ASSERT_TRUE(replica->currentBattles.empty());
	const auto restoredId = replica->nextBattleID;
	BattleStart outgoing;
	outgoing.battleID = restoredId;
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	BattleUnitsChanged outgoingUnits;
	outgoingUnits.battleID = restoredId;
	for(const auto * unit : battle()->getStacksIf([](const CStack *) { return true; }))
	{
		outgoingUnits.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::UPDATE);
		outgoingUnits.changedStacks.back().data = unit->acquireState()->save();
	}
	CMemorySerializer wire;
	wire.oser & outgoing & outgoingUnits;
	wire.iser.cb = replica.get();
	BattleStart incoming;
	BattleUnitsChanged incomingUnits;
	wire.iser & incoming & incomingUnits;
	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto restoredHandler = std::make_shared<CGameHandler>(restoredServer, replica);
	restoredHandler->sendAndApply(incoming);
	restoredHandler->sendAndApply(incomingUnits);
	CMemorySerializer rngWire(sourceRandomizer);
	rngWire.iser & *restoredHandler->randomizer;
	ASSERT_EQ(savedRandomizer(*restoredHandler->randomizer), sourceRandomizer);
	auto * restoredBattle = replica->getBattle(restoredId);
	ASSERT_NE(restoredBattle, nullptr);
	const auto * restoredHero = restoredBattle->getSideHero(BattleSide::ATTACKER);
	const auto * restoredActor = restoredBattle->battleGetUnitByID(actorId);
	const auto * restoredEnemy = restoredBattle->battleGetUnitByID(enemyId);
	ASSERT_NE(restoredHero, nullptr);
	ASSERT_NE(restoredActor, nullptr);
	ASSERT_NE(restoredEnemy, nullptr);
	ASSERT_EQ(restoredHero->luckVal(), attackerSideHero->luckVal());
	ASSERT_EQ(restoredHero->moraleVal(), attackerSideHero->moraleVal());
	ASSERT_TRUE(newHorizonsHeroLuck::hasLuckyDay(restoredHero));
	ASSERT_TRUE(newHorizonsHeroicSpirit::hasPerk(restoredHero));
	ASSERT_EQ(restoredBattle->getRound(), round);
	ASSERT_NE(restoredBattle->battleActiveUnit(), nullptr);
	ASSERT_EQ(restoredBattle->battleActiveUnit()->unitId(), actorId);
	ASSERT_EQ(restoredBattle->battleGetActionController(restoredActor), PlayerColor(0));
	EXPECT_EQ(restoredActor->acquireState()->save(), actorState);
	EXPECT_EQ(restoredEnemy->acquireState()->save(), enemyState);
	EXPECT_EQ(restoredBattle->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE), sourceOrder);
	EXPECT_EQ(restoredBattle->getHeroActionAllowances(BattleSide::ATTACKER), sourceLedger);
	// BattleStart intentionally restores INITIAL pools; no paid-mana resume claim.
	EXPECT_EQ(restoredHero->getNormalSpellPoints(), initialNormal);
	EXPECT_EQ(restoredHero->getBufferSpellPoints(), initialBuffer);
	ASSERT_TRUE(restoredHero->hasSpellbook());
	ASSERT_TRUE(restoredHero->canCastThisSpell(haste));
	ASSERT_TRUE(restoredActor->alive());
	ASSERT_EQ(restoredBattle->battleGetOwner(restoredActor), restoredHero->getCasterOwner());
	ASSERT_GE(restoredHero->getManaAvailable(), restoredBattle->battleGetSpellCost(haste, restoredHero));
	ASSERT_FALSE(restoredBattle->battleGetSpellActionAllowance(BattleSide::ATTACKER, SpellID::HASTE));
	ASSERT_EQ(restoredBattle->battleCanCastSpell(restoredHero, spells::Mode::HERO), ESpellCastProblem::CASTS_PER_TURN_LIMIT);
	BattleAction rejectedSpell;
	rejectedSpell.actionType = EActionType::HERO_SPELL;
	rejectedSpell.side = BattleSide::ATTACKER;
	rejectedSpell.spell = SpellID::HASTE;
	rejectedSpell.aimToUnit(restoredActor);
	EXPECT_FALSE(restoredHandler->battles->makePlayerBattleAction(restoredId, PlayerColor(0), rejectedSpell));
	EXPECT_EQ(savedRandomizer(*restoredHandler->randomizer), sourceRandomizer);
	EXPECT_EQ(restoredBattle->getHeroActionAllowances(BattleSide::ATTACKER), sourceLedger);
	EXPECT_EQ(restoredActor->acquireState()->save(), actorState);

	CMemorySerializer expectedWire(sourceRandomizer);
	GameRandomizer expected(*replica);
	expectedWire.iser & expected;
	auto expectedLuck = expected.rollHeroLuck(restoredHero->id, restoredHero->luckVal());
	ASSERT_TRUE(expectedLuck);
	expectedLuck->mark(newHorizonsHeroLuck::Proc::LuckyDay);
	ASSERT_TRUE(restoredBattle->battleCanBeginHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE));
	ASSERT_TRUE(restoredHandler->battles->makePlayerBattleAction(restoredId, PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE)));
	EXPECT_EQ(savedRandomizer(*restoredHandler->randomizer), savedRandomizer(expected));
	const auto continued = restoredBattle->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::HOLD_THE_LINE);
	ASSERT_TRUE(continued);
	EXPECT_EQ(continued->heroLuck, expectedLuck);
	EXPECT_FALSE(continued->heroMorale);
	EXPECT_EQ(continued->heroMoraleActionGrantId, 0u);
	EXPECT_EQ(restoredBattle->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE), sourceOrder);
	EXPECT_EQ(restoredBattle->getHeroOrderStates(BattleSide::ATTACKER).size(), 2u);
	const auto & spent = restoredBattle->getHeroActionAllowances(BattleSide::ATTACKER);
	EXPECT_EQ(spent.remainingCounts(round), (HeroActionAllowanceState::Counts{}));
	EXPECT_EQ(spent.heroicSpiritProcessedReceipts, sourceLedger.heroicSpiritProcessedReceipts);
	EXPECT_EQ(spent.nextGrantId, sourceLedger.nextGrantId);
	EXPECT_FALSE(restoredHandler->battles->makePlayerBattleAction(restoredId, PlayerColor(0),
		BattleAction::makeHeroCommand(BattleSide::ATTACKER, HeroCommand::CHARGE)));
	EXPECT_EQ(savedRandomizer(*restoredHandler->randomizer), savedRandomizer(expected));
	EXPECT_EQ(battle()->getHeroOrderState(BattleSide::ATTACKER, HeroCommand::CHARGE), sourceOrder);
	EXPECT_EQ(battle()->getHeroActionAllowances(BattleSide::ATTACKER), sourceLedger);
	EXPECT_EQ(savedRandomizer(*gameHandler->randomizer), sourceRandomizer);
	EXPECT_EQ(actor->acquireState()->save(), actorState);
	EXPECT_EQ(enemy->acquireState()->save(), enemyState);
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
