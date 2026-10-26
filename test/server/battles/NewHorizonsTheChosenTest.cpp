/*
 * NewHorizonsTheChosenTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "BattleStartSnapshotFixture.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsTheChosen.h"
#include "../../../lib/spells/NewHorizonsMagic.h"

namespace
{
SpellID chosenSpell()
{
	return SpellID(SpellID::decode(std::string(newHorizonsTheChosen::SPELL_KEY)));
}

TConstBonusListPtr chosenBonuses(const CStack * unit)
{
	return unit->getAllBonuses(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(chosenSpell())));
}
}

class NewHorizonsTheChosenTest : public HeroCommandFixture
{
protected:
	CStack * first = nullptr;
	CStack * second = nullptr;
	CStack * enemy = nullptr;
	std::vector<std::byte> preBattleWorld;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void selectExaltedChampion()
	{
		const SecondarySkill light(SecondarySkill::decode("new-horizons:lightMagic"));
		attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.healer"});
		attackerSideHero->setSecSkillLevel(light, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.purifier"});
		attackerSideHero->setSecSkillLevel(light, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:lightMagic", "new-horizons:lightMagic.crusader"});
		ASSERT_TRUE(attackerSideHero->hasActivePerk("new-horizons:lightMagic", "new-horizons:lightMagic.crusader"));
	}

	void prepare(int power = 150, bool captureRestoreContext = false)
	{
		startGame();
		ASSERT_NE(chosenSpell(), SpellID::NONE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(chosenSpell());
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, power, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:lightMagic")),
			MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);
		if(captureRestoreContext)
			preBattleWorld = gameState()->saveToMemory();
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		first = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(3, 5), 1000);
		second = addStack(BattleSide::ATTACKER, creatureByName("core:archer"), BattleHex(5, 5), 1000);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(12, 5), 1000);
		beginCombat();
	}

	bool cast(CStack * target)
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = chosenSpell();
		action.aimToUnit(target);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	void activate(const CStack * unit)
	{
		BattleSetActiveStack active;
		active.battleID = BattleID(0);
		active.stack = unit->unitId();
		active.reason = BattleUnitTurnReason::TURN_QUEUE;
		gameHandler->sendAndApply(active);
	}

	void replaceEnemy(BattleHex position, const std::string & creature = "core:pikeman")
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		remove.changedStacks.emplace_back(enemy->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		enemy = addStack(BattleSide::DEFENDER, creatureByName(creature), position, 1000);
		ASSERT_NE(enemy, nullptr);
	}

	void deterministicCombat(CStack * unit)
	{
		addMarker(unit, BonusType::NO_LUCK, 1);
		addMarker(unit, BonusType::NO_MORALE, 1);
		forceMaximumDamage(unit);
	}

	int attackCount(const CStack * unit, bool retaliation) const
	{
		return std::ranges::count_if(server.attacks, [unit, retaliation](const BattleAttack & attack)
		{
			return attack.stackAttacking == unit->unitId() && attack.counter() == retaliation;
		});
	}

	void addMarker(CStack * unit, BonusType type, int value, BonusSource source = BonusSource::OTHER)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, type,
			source, value, source == BonusSource::SPELL_EFFECT ? BonusSourceID(chosenSpell()) : BonusSourceID()));
	}
};

TEST_F(NewHorizonsTheChosenTest, CastGrantsFourNativeAbilitiesAndSpendsOneAction)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto mana = attackerSideHero->getManaAvailable();
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(mana - attackerSideHero->getManaAvailable(), 25);
	EXPECT_EQ(first->getTotalAttacks(false), 2);
	EXPECT_EQ(first->getTotalAttacks(true), 2);
	EXPECT_TRUE(first->hasBonusOfType(BonusType::FLYING));
	EXPECT_EQ(first->valOfBonuses(BonusType::JOUSTING), 5);
	EXPECT_FALSE(first->counterAttacks.isLimited());
	first->counterAttacks.use(10);
	EXPECT_TRUE(first->counterAttacks.canUse());
	ASSERT_EQ(chosenBonuses(first)->size(), 4u);
	for(const auto & bonus : *chosenBonuses(first))
	{
		EXPECT_EQ(bonus->turnsRemain, 3);
		EXPECT_EQ(bonus->valType, BonusValueType::INDEPENDENT_MAX);
	}
	EXPECT_TRUE(chosenBonuses(second)->empty());
	EXPECT_TRUE(chosenBonuses(enemy)->empty());
	EXPECT_FALSE(cast(second));
	EXPECT_EQ(chosenBonuses(first)->size(), 4u);
}

TEST_F(NewHorizonsTheChosenTest, ExaltedChampionAddsCapturedRoundAfterNominalCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare(1000));
	ASSERT_NO_FATAL_FAILURE(selectExaltedChampion());
	const auto rules = newHorizonsTheChosen::rulesFor(attackerSideHero->getMagicRules());
	ASSERT_TRUE(rules);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*rules, 1000, 145), 4);
	EXPECT_EQ(newHorizonsTheChosen::exaltedChampionDurationBonus(*rules, attackerSideHero), 1);
	ASSERT_TRUE(cast(first));
	ASSERT_EQ(chosenBonuses(first)->size(), 4u);
	for(const auto & bonus : *chosenBonuses(first))
		EXPECT_EQ(bonus->turnsRemain, 5);
}

TEST_F(NewHorizonsTheChosenTest, NaturalDoubleAttackAndJoustingRemainIndependentFloors)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	addMarker(first, BonusType::ADDITIONAL_ATTACK, 1);
	addMarker(first, BonusType::JOUSTING, 10);
	ASSERT_TRUE(cast(first));
	EXPECT_EQ(first->getTotalAttacks(false), 2);
	EXPECT_EQ(first->valOfBonuses(BonusType::JOUSTING), 10);
	addMarker(first, BonusType::NO_RETALIATION, 1);
	EXPECT_EQ(first->counterAttacks.total(), 0);
	EXPECT_FALSE(first->counterAttacks.canUse());
}

TEST_F(NewHorizonsTheChosenTest, AcceptedRecastRemovesEveryOldSiblingOnlyFromOriginalArmy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(first));
	addMarker(first, BonusType::NONE, 9, BonusSource::SPELL_EFFECT);
	addMarker(enemy, BonusType::FLYING, 1, BonusSource::SPELL_EFFECT);
	advanceRound();
	ASSERT_TRUE(cast(second));
	EXPECT_TRUE(chosenBonuses(first)->empty());
	EXPECT_EQ(chosenBonuses(second)->size(), 4u);
	EXPECT_EQ(chosenBonuses(enemy)->size(), 1u);
	EXPECT_EQ(first->getTotalAttacks(false), 1);
	EXPECT_FALSE(first->hasBonusOfType(BonusType::FLYING));
	advanceRound();
	ASSERT_TRUE(cast(second));
	EXPECT_EQ(chosenBonuses(second)->size(), 4u);
	for(int round = 0; round < 3; ++round)
		advanceRound();
	EXPECT_TRUE(chosenBonuses(second)->empty());
}

TEST_F(NewHorizonsTheChosenTest, IneligibleOrImmuneDestinationPreservesOldRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(first));
	advanceRound();
	const auto mana = attackerSideHero->getManaAvailable();
	EXPECT_FALSE(cast(enemy));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	addMarker(second, BonusType::MECHANICAL, 1);
	EXPECT_FALSE(cast(second));
	EXPECT_EQ(chosenBonuses(first)->size(), 4u);
	second->removeBonuses(Selector::type()(BonusType::MECHANICAL));
	// Use the spell subtype, as ordinary immunity does, rather than a global ban.
	auto immunity = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::SPELL_IMMUNITY,
		BonusSource::OTHER, 1, BonusSourceID(), BonusSubtypeID(chosenSpell()));
	second->addNewBonus(immunity);
	(void)cast(second);
	EXPECT_EQ(chosenBonuses(first)->size(), 4u);
	EXPECT_TRUE(chosenBonuses(second)->empty());
}

TEST_F(NewHorizonsTheChosenTest, TimeStoppedOldOrNewRecipientRejectsWithoutSpendingActionOrMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(first));
	advanceRound();
	const auto mana = attackerSideHero->getManaAvailable();
	auto stasis = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::TIME_STOP,
		BonusSource::OTHER, 1, BonusSourceID());
	stasis->parameters = std::make_shared<BonusParameters>(static_cast<int32_t>(BattleSide::DEFENDER));
	first->addNewBonus(stasis);
	ASSERT_TRUE(first->isTimeStopped());
	const auto oldBonuses = chosenBonuses(first);
	ASSERT_EQ(oldBonuses->size(), 4u);
	const int oldTurns = oldBonuses->front()->turnsRemain;
	EXPECT_FALSE(cast(second));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(chosenBonuses(first)->size(), 4u);
	EXPECT_TRUE(chosenBonuses(second)->empty());
	for(const auto & bonus : *chosenBonuses(first))
		EXPECT_EQ(bonus->turnsRemain, oldTurns);

	first->removeBonuses(Selector::type()(BonusType::TIME_STOP));
	second->addNewBonus(stasis);
	EXPECT_FALSE(cast(second));
	EXPECT_EQ(attackerSideHero->getManaAvailable(), mana);
	EXPECT_EQ(chosenBonuses(first)->size(), 4u);
	EXPECT_TRUE(chosenBonuses(second)->empty());
	second->removeBonuses(Selector::type()(BonusType::TIME_STOP));
	ASSERT_TRUE(cast(second)) << "Rejected casts must preserve the available Hero Action";
	EXPECT_TRUE(chosenBonuses(first)->empty());
	EXPECT_EQ(chosenBonuses(second)->size(), 4u);
}

TEST(NewHorizonsTheChosenRulesTest, RawPowerAndSchoolOnlyDurationUsesCapturedKnobs)
{
	const JsonNode saved(JsonPath::builtin("config/newHorizonsMagic"));
	const auto rules = newHorizonsTheChosen::rulesFor(saved);
	ASSERT_TRUE(rules);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*rules, 0, 145), 2);
	EXPECT_EQ(newHorizonsTheChosen::exaltedChampionDurationBonus(*rules, nullptr), 0);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*rules, 103, 145), 2);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*rules, 104, 145), 3);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*rules, 206, 145), 3);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*rules, 207, 145), 4);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*rules, 1000000000, 145), 4);
	JsonNode changed(saved);
	changed["spells"][std::string(newHorizonsTheChosen::SPELL_KEY)]["theChosen"]["spellPowerPerRound"].Integer() = 300;
	const auto altered = newHorizonsTheChosen::rulesFor(changed);
	ASSERT_TRUE(altered);
	EXPECT_EQ(newHorizonsTheChosen::nominalDuration(*altered, 150, 145), 2);
	changed["spells"][std::string(newHorizonsTheChosen::SPELL_KEY)]["theChosen"]["maximumDuration"].Integer() = 1;
	EXPECT_THROW(newHorizonsTheChosen::validate(changed), std::runtime_error);
	JsonNode legacy(saved);
	legacy["spells"].Struct().erase(std::string(newHorizonsTheChosen::SPELL_KEY));
	EXPECT_FALSE(newHorizonsTheChosen::rulesFor(legacy));
}

TEST(NewHorizonsTheChosenRulesTest, CapturedIdentityRejectsOlderWritersAndHasNoCrusadeAlias)
{
	const JsonNode saved(JsonPath::builtin("config/newHorizonsMagic"));
	EXPECT_NO_THROW(newHorizonsTheChosen::validateSerialization(saved, true));
	EXPECT_THROW(newHorizonsTheChosen::validateSerialization(saved, false), std::runtime_error);
	EXPECT_NE(chosenSpell(), SpellID(SpellID::decode("new-horizons:crusade")));
	JsonNode historical(saved);
	historical["spells"].Struct().erase(std::string(newHorizonsTheChosen::SPELL_KEY));
	EXPECT_NO_THROW(newHorizonsTheChosen::validateSerialization(historical, false));
	JsonNode incomplete(saved);
	incomplete["spells"][std::string(newHorizonsTheChosen::SPELL_KEY)].Struct().erase("theChosen");
	EXPECT_THROW(newHorizonsTheChosen::validateSerialization(incomplete, true), std::runtime_error);
	JsonNode malformed(saved);
	malformed["rulesetVersion"].String() = "3";
	EXPECT_THROW(newHorizonsTheChosen::validateSerialization(malformed, true), std::runtime_error);
}

TEST(NewHorizonsTheChosenRulesTest, ExistingBonusSerializationRetainsSourceAndRemainingDuration)
{
	Bonus original(BonusDuration::N_TURNS, BonusType::JOUSTING, BonusSource::SPELL_EFFECT,
		5, BonusSourceID(chosenSpell()));
	original.turnsRemain = 3;
	original.valType = BonusValueType::INDEPENDENT_MAX;
	CMemorySerializer writer;
	writer.oser.version = ESerializationVersion::CURRENT;
	writer.oser & original;
	CMemorySerializer reader(writer.extractBuffer());
	reader.iser.version = ESerializationVersion::CURRENT;
	Bonus loaded;
	reader.iser & loaded;
	EXPECT_EQ(loaded.sid, original.sid);
	EXPECT_EQ(loaded.type, original.type);
	EXPECT_EQ(loaded.turnsRemain, 3);
	EXPECT_EQ(loaded.valType, BonusValueType::INDEPENDENT_MAX);
}

TEST_F(NewHorizonsTheChosenTest, ActualMeleeActionStrikesTwiceAndReceivesOneOrdinaryRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(replaceEnemy(BattleHex(4, 5)));
	deterministicCombat(first);
	deterministicCombat(enemy);
	ASSERT_TRUE(cast(first));
	activate(first);
	const auto firstHealth = first->getAvailableHealth();
	const auto enemyHealth = enemy->getAvailableHealth();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(first, enemy->getPosition(), first->getPosition())));
	EXPECT_EQ(attackCount(first, false), 2);
	EXPECT_EQ(attackCount(enemy, true), 1);
	EXPECT_EQ(attackCount(first, true), 0);
	EXPECT_LT(first->getAvailableHealth(), firstHealth);
	EXPECT_LT(enemy->getAvailableHealth(), enemyHealth);
}

TEST_F(NewHorizonsTheChosenTest, ActualRangedActionFiresTwiceAndConsumesTwoShots)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(replaceEnemy(BattleHex(12, 5)));
	deterministicCombat(second);
	deterministicCombat(enemy);
	ASSERT_TRUE(cast(second));
	activate(second);
	const auto shots = second->shots.available();
	const auto health = enemy->getAvailableHealth();
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeShotAttack(second, enemy)));
	EXPECT_EQ(attackCount(second, false), 2);
	EXPECT_EQ(attackCount(enemy, true), 0);
	EXPECT_EQ(second->shots.available(), shots - 2);
	EXPECT_LT(enemy->getAvailableHealth(), health);
	for(const auto & attack : server.attacks)
	{
		if(attack.stackAttacking == second->unitId())
		{
			EXPECT_TRUE(attack.shot());
		}
	}
}

TEST_F(NewHorizonsTheChosenTest, ActualUnlimitedResponsesRemainSingleAndRespectFrozenAndNoRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	deterministicCombat(first);
	ASSERT_TRUE(cast(first));
	const auto neighbours = first->getSurroundingHexes();
	for(size_t index = 0; index < 4; ++index)
	{
		auto * attacker = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), neighbours.at(index), 100);
		ASSERT_NE(attacker, nullptr);
		deterministicCombat(attacker);
		if(index == 2)
		{
			SetStackEffect freeze;
			freeze.battleID = BattleID(0);
			freeze.toAdd.emplace_back(first->unitId(), std::vector<Bonus>{
				newHorizonsFrozen::makeFrozenMarker(BonusSourceID(creatureByName("core:iceElemental")), battle()->getRound())});
			gameHandler->sendAndApply(freeze);
			ASSERT_TRUE(first->isNewHorizonsFrozen());
		}
		if(index == 3)
			addMarker(first, BonusType::NO_RETALIATION, 1);
		activate(attacker);
		const auto health = first->getAvailableHealth();
		server.attacks.clear();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
			BattleAction::makeMeleeAttack(attacker, first->getPosition(), attacker->getPosition())));
		EXPECT_EQ(attackCount(attacker, false), 1);
		EXPECT_LT(first->getAvailableHealth(), health);
		if(index == 2)
		{
			EXPECT_FALSE(first->isNewHorizonsFrozen());
		}
		EXPECT_EQ(attackCount(first, true), index < 2 ? 1 : 0)
			<< "Double Attack must not double retaliations; ordinary blockers override unlimited responses";
	}
}

TEST_F(NewHorizonsTheChosenTest, ActualDispelRemovesAllChosenSiblingsAndPreservesIntrinsicAbilities)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * native = addStack(BattleSide::ATTACKER, creatureByName("core:crusader"), BattleHex(7, 8), 1000);
	ASSERT_NE(native, nullptr);
	ASSERT_EQ(native->getTotalAttacks(false), 2) << "Use real creature-inherited Double Attack";
	addMarker(native, BonusType::JOUSTING, 10);
	ASSERT_TRUE(cast(native));
	advanceRound();
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode("new-horizons:sorceryMagic")),
		MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = SpellID::DISPEL;
	action.aimToUnit(native);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(chosenBonuses(native)->empty());
	EXPECT_EQ(native->getTotalAttacks(false), 2);
	EXPECT_EQ(native->valOfBonuses(BonusType::JOUSTING), 10);
	EXPECT_FALSE(native->hasBonusOfType(BonusType::FLYING));
	EXPECT_TRUE(native->counterAttacks.isLimited());
}

TEST_F(NewHorizonsTheChosenTest, ActualChargedFirstStrikeUsesIntrinsicJoustingOnceAndFollowupHasNoCharge)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	// Pikemen intentionally negate intrinsic Jousting; this positive witness
	// requires a durable target without that native charge immunity.
	ASSERT_NO_FATAL_FAILURE(replaceEnemy(BattleHex(7, 5), "core:swordsman"));
	ASSERT_FALSE(enemy->hasBonusOfType(BonusType::CHARGE_IMMUNITY));
	deterministicCombat(first);
	deterministicCombat(enemy);
	blockRetaliation(first);
	addMarker(first, BonusType::JOUSTING, 10);
	ASSERT_TRUE(cast(first));
	ASSERT_EQ(first->valOfBonuses(BonusType::JOUSTING), 10);
	EXPECT_TRUE(first->getAllBonuses(Selector::sourceTypeSel(BonusSource::HERO_COMMAND))->empty());
	const BattleHex landing(6, 5);
	const auto [path, distance] = battle()->getPath(first->getPosition(), landing, first);
	ASSERT_FALSE(path.empty());
	ASSERT_GT(distance, 0);
	ASSERT_LE(distance, first->getMovementRange());
	BattleAttackInfo charged(first, enemy, distance, false);
	charged.attackerPos = landing;
	BattleAttackInfo ordinary(first, enemy, 0, false);
	ordinary.attackerPos = landing;
	const auto expectedCharged = battle()->calculateDmgRange(charged).damage.max;
	const auto expectedOrdinary = battle()->calculateDmgRange(ordinary).damage.max;
	ASSERT_GT(expectedCharged, expectedOrdinary);
	activate(first);
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMeleeAttack(first, enemy->getPosition(), landing)));
	EXPECT_EQ(first->getPosition(), landing);
	std::vector<int64_t> hits;
	for(const auto & attack : server.attacks)
		if(attack.stackAttacking == first->unitId() && !attack.counter())
			for(const auto & victim : attack.bsa)
				if(victim.stackAttacked == enemy->unitId())
					hits.push_back(victim.damageAmount);
	ASSERT_EQ(hits.size(), 2u);
	EXPECT_EQ(hits[0], expectedCharged);
	EXPECT_EQ(hits[1], expectedOrdinary);
}

TEST_F(NewHorizonsTheChosenTest, FlyingRejectsOccupiedLandingAndAcceptsOrdinaryLegalLanding)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(cast(first));
	activate(first);
	const auto origin = first->getPosition();
	EXPECT_FALSE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(first, second->getPosition())));
	EXPECT_EQ(first->getPosition(), origin);
	const BattleHex landing(4, 5);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
		BattleAction::makeMove(first, landing)));
	EXPECT_EQ(first->getPosition(), landing);
}

TEST_F(NewHorizonsTheChosenTest, FullBattleDescriptorRoundTripRetainsAllChosenSourcesAndRecastReplacesRestoredRecipient)
{
	ASSERT_NO_FATAL_FAILURE(prepare(150, true));
	ASSERT_TRUE(cast(first));
	advanceRound();
	const auto original = chosenBonuses(first);
	ASSERT_EQ(original->size(), 4u);
	auto replica = std::make_shared<CGameState>();
	replica->preInit(LIBRARY);
	replica->loadFromMemory(preBattleWorld);
	BattleStart outgoing;
	outgoing.battleID = BattleID(0);
	outgoing.info = battleStartFixture::snapshot(*battle(), replica.get());
	CMemorySerializer wire;
	wire.oser & outgoing;
	wire.iser.cb = replica.get();
	BattleStart incoming;
	wire.iser & incoming;
	RecordingGameServer restoredServer;
	restoredServer.gameState = replica;
	auto restoredHandler = std::make_shared<CGameHandler>(restoredServer, replica);
	restoredHandler->sendAndApply(incoming);
	auto * restored = replica->getBattle(BattleID(0));
	ASSERT_NE(restored, nullptr);
	auto * oldRecipient = restored->getStack(first->unitId(), false);
	auto * newRecipient = restored->getStack(second->unitId(), false);
	ASSERT_NE(oldRecipient, nullptr);
	ASSERT_NE(newRecipient, nullptr);
	EXPECT_EQ(oldRecipient->unitSide(), first->unitSide());
	EXPECT_EQ(oldRecipient->unitOwner(), first->unitOwner());
	const auto copied = chosenBonuses(oldRecipient);
	ASSERT_EQ(copied->size(), 4u);
	for(const auto & before : *original)
	{
		const auto found = std::ranges::find_if(*copied, [&](const auto & after) { return after->type == before->type; });
		ASSERT_NE(found, copied->end());
		EXPECT_EQ((*found)->source, before->source);
		EXPECT_EQ((*found)->sid, before->sid);
		EXPECT_EQ((*found)->duration, before->duration);
		EXPECT_EQ((*found)->turnsRemain, before->turnsRemain);
		EXPECT_EQ((*found)->val, before->val);
		EXPECT_EQ((*found)->valType, before->valType);
		EXPECT_EQ((*found)->spellCasterOwner, before->spellCasterOwner);
	}
	BattleAction action;
	action.actionType = EActionType::HERO_SPELL;
	action.side = BattleSide::ATTACKER;
	action.spell = chosenSpell();
	action.aimToUnit(newRecipient);
	ASSERT_TRUE(restoredHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action));
	EXPECT_TRUE(chosenBonuses(oldRecipient)->empty());
	EXPECT_EQ(chosenBonuses(newRecipient)->size(), 4u);
	EXPECT_EQ(chosenBonuses(first)->size(), 4u) << "Replica continuation must not mutate the live source graph";
}
