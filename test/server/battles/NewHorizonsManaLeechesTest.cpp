/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/battle/NewHorizonsManaLeeches.h"
#include "../../../lib/battle/NewHorizonsBattlecraft.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/entities/hero/NewHorizonsManaLeeches.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/gameState/NewHorizonsBattleSpellPoints.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/AttackPossibility.h"
#include "../../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../AI/BattleAI/HypotheticBattle.h"
#endif
namespace
{
class NewHorizonsManaLeechesTest : public BattleTestFixture
{
protected:
	bool captured = true;
	bool opposingLeeches = false;
	bool legacyMagic = false;
	CStack * leech = nullptr;
	CStack * target = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires curated New Horizons preset";
	}
	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!captured)
			rules["additionalSpecialties"].Struct().erase("manaLeeches");
		if(legacyMagic)
			rules["startingSkills"].Struct().erase("startingBookReplacements");
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		if(legacyMagic)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get()); hero && (hero->getOwner() == PlayerColor(0)
				|| (opposingLeeches && hero->getOwner() == PlayerColor(1))))
				hero->setHeroType(HeroTypeID(HeroTypeID::decode("core:ignatius")));
	}
	void prepare(bool familiar = true, int own = 5, int enemy = 5, int buffer = 0)
	{
		startGame();
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		attackerSideHero->initializeSpellPoints(own);
		defenderSideHero->initializeSpellPoints(enemy, buffer);
		startBattle();
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
		leech = addStack(BattleSide::ATTACKER, CreatureID(familiar ? CreatureID::FAMILIAR : CreatureID::IMP), BattleHex(leftHex), 100);
		target = addStack(BattleSide::DEFENDER,
			creatureByName(opposingLeeches ? "core:familiar" : "core:pikeman"), BattleHex(rightHex), 100);
		forceMaximumDamage(leech);
		forceMaximumDamage(target);
		beginCombat();
		attackerSideHero->initializeSpellPoints(own);
		defenderSideHero->initializeSpellPoints(enemy, buffer);
		battle()->activeStack = leech->unitId();
	}
	void preparePreemptive(bool blockOrdinaryRetaliation)
	{
		ASSERT_NO_FATAL_FAILURE(prepare());
		const auto skill = SecondarySkill(SecondarySkill::decode("new-horizons:battlecraft"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({"new-horizons:battlecraft", "new-horizons:battlecraft.preEmptiveStrike"});
		ASSERT_TRUE(newHorizonsBattlecraft::hasPreemptiveStrike(attackerSideHero));
		if(blockOrdinaryRetaliation)
			target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
				BonusType::BLOCKS_RETALIATION, BonusSource::OTHER, 1, BonusSourceID()));
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0),
			BattleAction::makeDefend(leech)));
		ASSERT_TRUE(leech->defended());
		server.attacks.clear();
	}
};
#ifdef ENABLE_BATTLE_AI
class LeechEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit LeechEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
	std::shared_ptr<HypotheticBattle> model(CBattleInfoCallback * source)
	{
		return std::make_shared<HypotheticBattle>(this,
			std::shared_ptr<CBattleInfoCallback>(source, [](CBattleInfoCallback *) {}));
	}
};
void futureAction(const std::shared_ptr<HypotheticBattle> & branch, uint32_t source, uint32_t victim)
{
	auto actor = branch->getForUpdate(source);
	auto target = branch->getForUpdate(victim);
	ASSERT_TRUE(actor && target);
	DamageCache cache;
	cache.buildDamageCache(branch, actor->unitSide());
	BattleExchangeVariant exchange;
	bool sequenceSpent = false;
	const int blows = AttackPossibility::getAttackCount(*actor, false, *branch);
	for(int i = 0; i < blows && actor->alive() && target->alive(); ++i)
	{
		BattleExchangeAttackResult result;
		exchange.trackAttack(actor, target, false, actor->unitSide() == BattleSide::ATTACKER,
			cache, branch, false, i == 0, nullptr, nullptr, nullptr, &result, &sequenceSpent);
		if(result.completedAction)
			break;
	}
}
#endif
}
TEST_F(NewHorizonsManaLeechesTest, FamiliarRestoresOnlyActualTheftAndSpendsEnemyBufferFirst)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5, 2));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 6);
	EXPECT_EQ(defenderSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(defenderSideHero->getBufferSpellPoints(), 1);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const auto & line)
		{ return line.find("Mana Leeches steals 1 enemy Mana and restores 1 Normal Mana") != std::string::npos; }));
}
TEST_F(NewHorizonsManaLeechesTest, LegacyCompletionRetainsOnlyActualFamiliarRecovery)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaLeechesRecoveredNormal, 1);
	// Unrelated restoration still obeys the pre-existing legacy ceiling.
	SetMana unrelated(attackerSideHero->id, SetMana::Operation::RESTORE_NORMAL, 20);
	gameHandler->sendAndApply(unrelated);
	BattleResultsApplied done;
	done.battleID = battle()->getBattleID();
	gameState()->apply(done);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 6);
}
TEST_F(NewHorizonsManaLeechesTest, LegacyAbsentFeatureDoesNotRetainUnrelatedRecovery)
{
	legacyMagic = true;
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare(false, 5, 5));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaLeechesRecoveredNormal, 0);
	SetMana unrelated(attackerSideHero->id, SetMana::Operation::RESTORE_NORMAL, 20);
	gameHandler->sendAndApply(unrelated);
	BattleResultsApplied done;
	done.battleID = battle()->getBattleID();
	gameState()->apply(done);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
}
TEST_F(NewHorizonsManaLeechesTest, LegacyRecoveryThenSpendUsesExistingSettlementRules)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	SetMana spend(attackerSideHero->id, SetMana::Operation::SPEND, 3);
	gameHandler->sendAndApply(spend);
	BattleResultsApplied done;
	done.battleID = battle()->getBattleID();
	gameState()->apply(done);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 3);
}
TEST_F(NewHorizonsManaLeechesTest, LegacyCancellationRollsBackRecoveryToEntrySnapshot)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	BattleCancelled cancel;
	cancel.battleID = battle()->getBattleID();
	gameState()->apply(cancel);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(defenderSideHero->getNormalSpellPoints(), 5);
}
TEST_F(NewHorizonsManaLeechesTest, TypedRecoveryRejectsAbsentPolicyBeforeMutation)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	SetMana restore(attackerSideHero->id, SetMana::Operation::RESTORE_NORMAL_MANA_LEECHES, 1);
	EXPECT_THROW(gameState()->apply(restore), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaLeechesRecoveredNormal, 0);
}
TEST_F(NewHorizonsManaLeechesTest, LegacyAdmittedImpHasNoRecoveryReceipt)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare(false, 5, 5));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaLeechesRecoveredNormal, 0);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
}
TEST_F(NewHorizonsManaLeechesTest, LegacyFullNormalRecoveryDoesNotMintReceiptOrChangeBuffer)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->initializeSpellPoints(attackerSideHero->manaLimit(), 9);
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaLeechesRecoveredNormal, 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 9);
	EXPECT_EQ(defenderSideHero->getNormalSpellPoints(), 4);
	const auto excess = attackerSideHero->manaLimit() + 7;
	attackerSideHero->initializeSpellPoints(excess, 9);
	for(const int amount : {0, 1})
	{
		SetMana restore(attackerSideHero->id, SetMana::Operation::RESTORE_NORMAL_MANA_LEECHES, amount);
		ASSERT_NO_THROW(gameState()->apply(restore));
		EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), excess);
		EXPECT_EQ(battle()->getSide(BattleSide::ATTACKER).manaLeechesRecoveredNormal, 0);
	}
}
TEST_F(NewHorizonsManaLeechesTest, TypedRecoverySaturatesReceiptAndRejectsOverPolicyBeforeMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & side = battle()->getSide(BattleSide::ATTACKER);
	side.manaLeechesRecoveredNormal = std::numeric_limits<int32_t>::max();
	EXPECT_THROW(newHorizonsHeroes::validateBattleSpellPointEntry(*gameState(), *battle()), std::runtime_error);
	SetMana restore(attackerSideHero->id, SetMana::Operation::RESTORE_NORMAL_MANA_LEECHES, 1);
	ASSERT_NO_THROW(gameState()->apply(restore));
	EXPECT_EQ(side.manaLeechesRecoveredNormal, std::numeric_limits<int32_t>::max());
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 6);
	SetMana excessive(attackerSideHero->id, SetMana::Operation::RESTORE_NORMAL_MANA_LEECHES, 2);
	EXPECT_THROW(gameState()->apply(excessive), std::runtime_error);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 6);
}
TEST_F(NewHorizonsManaLeechesTest, ImpDrainsWithoutRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 4);
}
TEST_F(NewHorizonsManaLeechesTest, EmptyEnemyPoolDoesNotGenerateMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 0));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 0);
}
TEST_F(NewHorizonsManaLeechesTest, FullOwnNormalPoolStillDrainsEnemy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	attackerSideHero->initializeSpellPoints(attackerSideHero->manaLimit());
	const auto maximum = attackerSideHero->manaLimit();
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), maximum);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 4);
}
TEST_F(NewHorizonsManaLeechesTest, DoubleAttackStealsOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	leech->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 6);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 4);
}
TEST_F(NewHorizonsManaLeechesTest, RetaliationAlsoSteals)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->activeStack = target->unitId();
	ASSERT_TRUE(attack(target, leech->getPosition()));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 6);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 4);
}
TEST_F(NewHorizonsManaLeechesTest, BattlecraftPreemptiveHitWithoutOrdinaryRetaliationDoesNotSteal)
{
	ASSERT_NO_FATAL_FAILURE(preparePreemptive(true));
	const auto round = battle()->battleGetRound();
	ASSERT_TRUE(attack(target, leech->getPosition()));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks[0].stackAttacking, leech->unitId());
	ASSERT_FALSE(server.attacks[0].bsa.empty());
	EXPECT_GT(server.attacks[0].bsa.front().damageAmount, 0);
	EXPECT_EQ(leech->battlecraftPreemptiveStrikeRound, round);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 5);
}
TEST_F(NewHorizonsManaLeechesTest, BattlecraftPreemptiveHitDoesNotAddTheftToOrdinaryRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(preparePreemptive(false));
	ASSERT_TRUE(attack(target, leech->getPosition()));
	ASSERT_EQ(server.attacks.size(), 3u);
	EXPECT_EQ(server.attacks[0].stackAttacking, leech->unitId());
	EXPECT_EQ(server.attacks[1].stackAttacking, target->unitId());
	EXPECT_EQ(server.attacks[2].stackAttacking, leech->unitId());
	EXPECT_TRUE(server.attacks[2].counter());
	ASSERT_FALSE(server.attacks[2].bsa.empty());
	EXPECT_GT(server.attacks[2].bsa.front().damageAmount, 0);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 6);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 4);
}
TEST_F(NewHorizonsManaLeechesTest, LiveNativeFirstStrikeUsesActualStrikeOrderForAsymmetricEmptyPools)
{
	opposingLeeches = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true, 1, 0));
	target->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::FIRST_STRIKE, BonusSource::OTHER, 1, BonusSourceID(),
		BonusSubtypeID(BonusCustomSubtype::damageTypeAll)));
	server.attacks.clear();
	ASSERT_TRUE(attack(leech, target->getPosition()));
	ASSERT_EQ(server.attacks.size(), 2u);
	EXPECT_EQ(server.attacks[0].stackAttacking, target->unitId());
	EXPECT_EQ(server.attacks[1].stackAttacking, leech->unitId());
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 1);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 0);
	EXPECT_EQ(std::ranges::count_if(server.battleLogLines, [](const auto & line)
		{ return line.find("Mana Leeches steals 1") != std::string::npos; }), 2);
}
TEST_F(NewHorizonsManaLeechesTest, HistoricalCaptureRetainsNoTheftAndGenericLineBonuses)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_FALSE(newHorizonsHeroes::hasManaLeeches(attackerSideHero));
	EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & b)
		{ return b && b->stacking.find("creature-line-specialty") != std::string::npos; }));
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 5);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 5);
}
TEST_F(NewHorizonsManaLeechesTest, ReplacementDoesNotStackGenericRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_TRUE(newHorizonsHeroes::hasManaLeeches(attackerSideHero));
	EXPECT_FALSE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & b)
		{ return b && b->stacking.find("creature-line-specialty") != std::string::npos; }));
}
TEST_F(NewHorizonsManaLeechesTest, PuppetControllerHostilityIsIndependentOfOwnerEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const SpellID puppet(SpellID::decode("new-horizons:puppetMaster"));
	leech->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(1))));
	ASSERT_EQ(battle()->battleGetOwner(leech), PlayerColor(0));
	ASSERT_EQ(battle()->battleGetActionController(leech), PlayerColor(1));
	EXPECT_EQ(newHorizonsManaLeeches::entitlement(*battle(), leech, target, false, true, false).amount, 0);
}
TEST_F(NewHorizonsManaLeechesTest, NoHeroNeutralsHaveNoInventedMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto & side = battle()->getSide(BattleSide::DEFENDER);
	const auto originalHero = side.heroID;
	side.heroID = ObjectInstanceID();
	EXPECT_EQ(newHorizonsManaLeeches::entitlement(*battle(), leech, target, false, true, false).amount, 0);
	side.heroID = originalHero;
}
TEST_F(NewHorizonsManaLeechesTest, HypnotizedEnemyNowUnderIgnatiusUsesEffectiveOwner)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto * controlled = addStack(BattleSide::DEFENDER, CreatureID(CreatureID::FAMILIAR), BattleHex(12, 7), 10);
	controlled->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	ASSERT_EQ(battle()->battleGetOwner(controlled), PlayerColor(0));
	const auto rule = newHorizonsManaLeeches::entitlement(*battle(), controlled, target, false, true, false);
	EXPECT_EQ(rule.beneficiary, BattleSide::ATTACKER);
	EXPECT_EQ(rule.amount, 1);
	// Native allegiance transfer removes entitlement from an original Ignatius stack.
	leech->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(newHorizonsManaLeeches::entitlement(*battle(), leech, controlled, false, true, false).amount, 0);
}
TEST_F(NewHorizonsManaLeechesTest, RangedSpellLikeAndCollateralHaveNoEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(newHorizonsManaLeeches::entitlement(*battle(), leech, target, true, true, false).amount, 0);
	EXPECT_EQ(newHorizonsManaLeeches::entitlement(*battle(), leech, target, false, false, false).amount, 0);
	EXPECT_EQ(newHorizonsManaLeeches::entitlement(*battle(), leech, target, false, true, true).amount, 0);
}
TEST(NewHorizonsManaLeechesTransfer, ZeroDamageReceiptAndOverNormalCapacityDoNotGenerateOrDestroyMana)
{
	const newHorizonsManaLeeches::Entitlement rule{BattleSide::ATTACKER, BattleSide::DEFENDER, 1, true};
	EXPECT_EQ(newHorizonsManaLeeches::transfer(rule, 0, false, 20, 5, 10).stolen, 0);
	EXPECT_EQ(newHorizonsManaLeeches::transfer(rule, 1, true, 20, 5, 10).stolen, 0);
	const auto over = newHorizonsManaLeeches::transfer(rule, 1, false, 20, 20, 10);
	EXPECT_EQ(over.stolen, 1);
	EXPECT_EQ(over.restored, 0);
	const newHorizonsManaLeeches::Entitlement configurable{BattleSide::ATTACKER, BattleSide::DEFENDER, 7, true};
	const auto partial = newHorizonsManaLeeches::transfer(configurable, 1, false, 3, 9, 10);
	EXPECT_EQ(partial.stolen, 3);
	EXPECT_EQ(partial.restored, 1);
}
#ifdef ENABLE_BATTLE_AI
TEST_F(NewHorizonsManaLeechesTest, DetachedAttackMatchesLivePoolsAndBranchesStayIndependent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5, 2));
	LeechEnvironment environment(gameState());
	const auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, model);
	DamageCache cache;
	cache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto projected = AttackPossibility::evaluate(BattleAttackInfo(leech, target, 0, false),
		leech->getPosition(), cache, model);
	ASSERT_NE(projected.effectPreview, nullptr);
	EXPECT_EQ(projected.effectPreview->getProjectedManaAvailable(BattleSide::ATTACKER), 6);
	EXPECT_EQ(projected.effectPreview->getProjectedManaAvailable(BattleSide::DEFENDER), 6);
	EXPECT_EQ(model->getProjectedManaAvailable(BattleSide::ATTACKER), 5);
	EXPECT_EQ(sibling->getProjectedManaAvailable(BattleSide::DEFENDER), 7);
	EXPECT_EQ(attackerSideHero->getManaAvailable(), 5);
	EXPECT_EQ(defenderSideHero->getManaAvailable(), 7);
	auto replay = std::make_shared<HypotheticBattle>(&environment, model);
	BattleExchangeVariant exchange;
	DamageCache replayCache;
	exchange.trackAttack(projected, replay, replayCache);
	EXPECT_EQ(replay->getProjectedManaAvailable(BattleSide::ATTACKER), 6);
	EXPECT_EQ(replay->getProjectedManaAvailable(BattleSide::DEFENDER), 6);
	EXPECT_EQ(sibling->getProjectedManaAvailable(BattleSide::DEFENDER), 7);
	const auto replaySourceBefore = replay->getForUpdate(leech->unitId())->save();
	const auto replayTargetBefore = replay->getForUpdate(target->unitId())->save();
	const auto scoreBefore = exchange.getScore();
	const auto cacheBefore = replayCache.captureSnapshot();
	const auto activeBefore = replay->getActiveStackID();
	const auto activationBefore = replay->getActivationSerial();
	const auto roundBefore = replay->battleGetRound();
	EXPECT_FALSE(replay->prepareManaLeechReplay(*projected.effectPreview).has_value());
	EXPECT_EQ(exchange.trackAttack(projected, replay, replayCache), 0);
	EXPECT_EQ(replay->getForUpdate(leech->unitId())->save(), replaySourceBefore);
	EXPECT_EQ(replay->getForUpdate(target->unitId())->save(), replayTargetBefore);
	EXPECT_EQ(exchange.getScore().ourDamageReduce, scoreBefore.ourDamageReduce);
	EXPECT_EQ(exchange.getScore().enemyDamageReduce, scoreBefore.enemyDamageReduce);
	EXPECT_TRUE(replayCache.matchesSnapshot(cacheBefore));
	EXPECT_EQ(replay->getActiveStackID(), activeBefore);
	EXPECT_EQ(replay->getActivationSerial(), activationBefore);
	EXPECT_EQ(replay->battleGetRound(), roundBefore);
	EXPECT_EQ(replay->getProjectedManaAvailable(BattleSide::DEFENDER), 6);
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(projected.effectPreview->getProjectedManaAvailable(BattleSide::ATTACKER), attackerSideHero->getManaAvailable());
	EXPECT_EQ(projected.effectPreview->getProjectedManaAvailable(BattleSide::DEFENDER), defenderSideHero->getManaAvailable());
	auto next = std::make_shared<HypotheticBattle>(&environment, projected.effectPreview);
	EXPECT_EQ(next->getProjectedManaAvailable(BattleSide::DEFENDER), 6);
}
TEST_F(NewHorizonsManaLeechesTest, HistoricalProjectedManaReadRetainsVisibleHeroPools)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	LeechEnvironment environment(gameState());
	const auto callback = std::shared_ptr<CBattleInfoCallback>(battle(), [](CBattleInfoCallback *) {});
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	EXPECT_EQ(model->getProjectedManaAvailable(BattleSide::ATTACKER), attackerSideHero->getManaAvailable());
	EXPECT_EQ(model->getProjectedManaAvailable(BattleSide::DEFENDER), defenderSideHero->getManaAvailable());
	battle()->getSide(BattleSide::DEFENDER).counterspellArmed = true;
	auto armed = std::make_shared<HypotheticBattle>(&environment, callback);
	const auto ward = armed->resolveProjectedCounterspell(BattleSide::ATTACKER,
		SpellID(SpellID::MAGIC_ARROW).toSpell());
	ASSERT_TRUE(ward.resolutionKnown);
	ASSERT_TRUE(ward.manaCost.has_value());
	EXPECT_EQ(ward.negated, defenderSideHero->getManaAvailable() >= *ward.manaCost);
}
TEST_F(NewHorizonsManaLeechesTest, HiddenOpponentPoolsRemainUnknown)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	LeechEnvironment environment(gameState());
	auto player = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, player);
	EXPECT_EQ(model->getProjectedManaAvailable(BattleSide::ATTACKER), 5);
	EXPECT_FALSE(model->getProjectedManaAvailable(BattleSide::DEFENDER).has_value());
}
TEST_F(NewHorizonsManaLeechesTest, HiddenIgnatiusFamiliarHasNoPlayerScopedEntitlement)
{
	opposingLeeches = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	CPlayerBattleCallback player(battle(), PlayerColor(0));
	ASSERT_EQ(player.battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	EXPECT_EQ(newHorizonsManaLeeches::entitlement(player, target, leech,
		false, true, false).amount, 0);
}
TEST_F(NewHorizonsManaLeechesTest, FutureDoubleAttackMatchesPreviewSelectedAndLiveBufferFirstTransfer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5, 2));
	leech->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	LeechEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto future = std::make_shared<HypotheticBattle>(&environment, parent);
	auto sibling = std::make_shared<HypotheticBattle>(&environment, parent);
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(leech, target, 0, false),
		leech->getPosition(), cache, parent);
	ASSERT_NE(preview.effectPreview, nullptr);
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(preview, selected, cache);
	ASSERT_NO_FATAL_FAILURE(futureAction(future, leech->unitId(), target->unitId()));
	EXPECT_EQ(future->getProjectedMana(BattleSide::DEFENDER)->getBuffer(), 1);
	EXPECT_EQ(future->getProjectedMana(BattleSide::DEFENDER)->getNormal(), 5);
	EXPECT_EQ(future->getProjectedMana(BattleSide::ATTACKER)->getNormal(), 6);
	for(const auto side : {BattleSide::ATTACKER, BattleSide::DEFENDER})
	{
		EXPECT_EQ(future->getProjectedManaAvailable(side), preview.effectPreview->getProjectedManaAvailable(side));
		EXPECT_EQ(future->getProjectedManaAvailable(side), selected->getProjectedManaAvailable(side));
	}
	EXPECT_EQ(parent->getProjectedManaAvailable(BattleSide::DEFENDER), 7);
	EXPECT_EQ(sibling->getProjectedManaAvailable(BattleSide::ATTACKER), 5);
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(future->getProjectedManaAvailable(BattleSide::ATTACKER), attackerSideHero->getManaAvailable());
	EXPECT_EQ(future->getProjectedManaAvailable(BattleSide::DEFENDER), defenderSideHero->getManaAvailable());
	auto descendant = std::make_shared<HypotheticBattle>(&environment, future);
	EXPECT_EQ(descendant->getProjectedManaAvailable(BattleSide::DEFENDER), 6);
}
TEST_F(NewHorizonsManaLeechesTest, FutureOrdinaryRetaliationHasItsOwnTransfer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5, 2));
	LeechEnvironment environment(gameState());
	auto branch = environment.model(battle());
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, target->unitId(), leech->unitId()));
	EXPECT_EQ(branch->getProjectedMana(BattleSide::ATTACKER)->getNormal(), 6);
	EXPECT_EQ(branch->getProjectedMana(BattleSide::DEFENDER)->getBuffer(), 1);
	ASSERT_TRUE(attack(target, leech->getPosition()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), attackerSideHero->getManaAvailable());
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), defenderSideHero->getManaAvailable());
}
TEST_F(NewHorizonsManaLeechesTest, FutureActualTheftCanWaitForLaterBlowWhenCounterRestoresInitiallyEmptyVictim)
{
	opposingLeeches = true;
	ASSERT_NO_FATAL_FAILURE(prepare(true, 1, 0));
	leech->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
	LeechEnvironment environment(gameState());
	auto branch = environment.model(battle());
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), 1);
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), 0);
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), attackerSideHero->getManaAvailable());
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), defenderSideHero->getManaAvailable());
}
TEST_F(NewHorizonsManaLeechesTest, FutureRestorationChangesNormalOnlyAndFullNormalDoesNotPreventTheft)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto maximum = attackerSideHero->manaLimit();
	attackerSideHero->initializeSpellPoints(maximum, 9);
	LeechEnvironment environment(gameState());
	auto branch = environment.model(battle());
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedMana(BattleSide::ATTACKER)->getNormal(), maximum);
	EXPECT_EQ(branch->getProjectedMana(BattleSide::ATTACKER)->getBuffer(), 9);
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), 4);
}
TEST_F(NewHorizonsManaLeechesTest, FutureImpDoesNotReceiveFamiliarRestoration)
{
	ASSERT_NO_FATAL_FAILURE(prepare(false));
	LeechEnvironment environment(gameState());
	auto branch = environment.model(battle());
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), 5);
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), 4);
}
TEST_F(NewHorizonsManaLeechesTest, FutureHistoricalCaptureAndTargetProbeDoNotCreateTransferState)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	LeechEnvironment environment(gameState());
	auto branch = environment.model(battle());
	const auto before = branch->getForUpdate(leech->unitId())->save();
	DamageCache cache;
	cache.buildDamageCache(branch, BattleSide::ATTACKER);
	BattleExchangeVariant probe;
	probe.trackAttack(branch->getForUpdate(leech->unitId()), branch->getForUpdate(target->unitId()),
		false, true, cache, branch, true);
	EXPECT_EQ(branch->getForUpdate(leech->unitId())->save(), before);
	EXPECT_FALSE(branch->hasProjectedManaLeech());
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), 5);
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), 5);
	EXPECT_FALSE(branch->hasProjectedManaLeech());
}
TEST_F(NewHorizonsManaLeechesTest, FutureCurrentPolicyProbeAndZeroDamageDoNotSpendSequenceReceipt)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 0));
	LeechEnvironment environment(gameState());
	auto branch = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(branch, BattleSide::ATTACKER);
	BattleExchangeVariant probe;
	bool spent = false;
	probe.trackAttack(branch->getForUpdate(leech->unitId()), branch->getForUpdate(target->unitId()),
		false, true, cache, branch, true, true, nullptr, nullptr, nullptr, nullptr, &spent);
	EXPECT_FALSE(spent);
	EXPECT_FALSE(branch->hasProjectedManaLeech());
	const auto rule = newHorizonsManaLeeches::entitlement(*branch,
		branch->getForUpdate(leech->unitId()).get(), branch->getForUpdate(target->unitId()).get(), false, true, false);
	branch->projectManaLeech(rule, 0, &spent);
	EXPECT_FALSE(spent);
	EXPECT_FALSE(branch->hasProjectedManaLeech());
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), 5);
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), 0);
}
TEST_F(NewHorizonsManaLeechesTest, FutureAndSelectedTransfersPreserveTemporaryBufferProvenance)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5, 2));
	battle()->getSide(BattleSide::DEFENDER).temporaryBufferRemaining = 2;
	LeechEnvironment environment(gameState());
	auto parent = environment.model(battle());
	auto branch = std::make_shared<HypotheticBattle>(&environment, parent);
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedMana(BattleSide::DEFENDER)->getNormal(), 5);
	EXPECT_EQ(branch->getProjectedMana(BattleSide::DEFENDER)->getBuffer(), 1);
	EXPECT_EQ(branch->getTemporaryBufferRemaining(BattleSide::DEFENDER), 1);
	EXPECT_EQ(parent->getTemporaryBufferRemaining(BattleSide::DEFENDER), 2);
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(leech, target, 0, false),
		leech->getPosition(), cache, parent);
	ASSERT_NE(preview.effectPreview, nullptr);
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	BattleExchangeVariant replay;
	replay.trackAttack(preview, selected, cache);
	EXPECT_EQ(selected->getTemporaryBufferRemaining(BattleSide::DEFENDER), 1);
	auto child = std::make_shared<HypotheticBattle>(&environment, branch);
	EXPECT_EQ(child->getTemporaryBufferRemaining(BattleSide::DEFENDER), 1);
	ASSERT_TRUE(attack(leech, target->getPosition()));
	EXPECT_EQ(branch->getTemporaryBufferRemaining(BattleSide::DEFENDER),
		battle()->getTemporaryBufferRemaining(BattleSide::DEFENDER));
}
TEST_F(NewHorizonsManaLeechesTest, TemporaryProvenanceOnlyChangeRejectsStaleSelectedReceiptBeforeDamage)
{
	ASSERT_NO_FATAL_FAILURE(prepare(true, 5, 5, 2));
	battle()->getSide(BattleSide::DEFENDER).temporaryBufferRemaining = 2;
	LeechEnvironment environment(gameState());
	auto parent = environment.model(battle());
	DamageCache cache;
	cache.buildDamageCache(parent, BattleSide::ATTACKER);
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(leech, target, 0, false),
		leech->getPosition(), cache, parent);
	ASSERT_NE(preview.effectPreview, nullptr);
	auto selected = std::make_shared<HypotheticBattle>(&environment, parent);
	const auto before = selected->getForUpdate(target->unitId())->save();
	battle()->getSide(BattleSide::DEFENDER).temporaryBufferRemaining = 1;
	BattleExchangeVariant replay;
	EXPECT_FALSE(selected->prepareManaLeechReplay(*preview.effectPreview).has_value());
	EXPECT_EQ(replay.trackAttack(preview, selected, cache), 0);
	EXPECT_EQ(selected->getForUpdate(target->unitId())->save(), before);
	EXPECT_EQ(selected->getProjectedManaAvailable(BattleSide::DEFENDER), 7);
	EXPECT_EQ(selected->getTemporaryBufferRemaining(BattleSide::DEFENDER), 1);
	battle()->getSide(BattleSide::DEFENDER).temporaryBufferRemaining = 2;
}
TEST_F(NewHorizonsManaLeechesTest, FutureNewActivationStartsANewTheftSequence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	LeechEnvironment environment(gameState());
	auto branch = environment.model(battle());
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), 6);
	branch->nextRound();
	branch->nextTurn(leech->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_NO_FATAL_FAILURE(futureAction(branch, leech->unitId(), target->unitId()));
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::ATTACKER), 7);
	EXPECT_EQ(branch->getProjectedManaAvailable(BattleSide::DEFENDER), 3);
}
#endif
