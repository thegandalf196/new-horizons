/*
 * Part of VCMI / New Horizons.
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "NewHorizonsParalyzingGazeFixture.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include <tuple>
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsFrozen.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/battle/NewHorizonsPuppetMaster.h"
#include "../../../lib/bonuses/BonusSelector.h"
TEST_F(NewHorizonsParalyzingGazeBattleTest, DamagingBeholderShotAppliesOnePendingTokenAndRehitDoesNotStack)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->moveUnit(melee->unitId(), BattleHex(3, 8)); // Clear target's ranged obstruction.
	ASSERT_TRUE(battle()->battleCanShootAction(source, target->getPosition()));
	const auto before = target->getAvailableHealth();
	ASSERT_TRUE(shoot());
	ASSERT_LT(target->getAvailableHealth(), before);
	ASSERT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_FALSE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & b)
		{ return b && b->stacking.find("creature-line-specialty") != std::string::npos; }));
	ASSERT_TRUE(shoot());
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target)); // Duplicate check inside pending().
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, EvilEyeAndHistoricalCaptureUseDistinctSpecialtyPolicies)
{
	captured = false;
	ASSERT_NO_FATAL_FAILURE(prepare(true));
	battle()->moveUnit(melee->unitId(), BattleHex(3, 8));
	EXPECT_FALSE(newHorizonsHeroes::hasParalyzingGaze(attackerSideHero));
	EXPECT_TRUE(std::ranges::any_of(attackerSideHero->getExportedBonusList(), [](const auto & b)
		{ return b && b->stacking.find("creature-line-specialty") != std::string::npos; }));
	ASSERT_TRUE(shoot());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, OrdinaryMeleeCancellationUsesCounterOnlyAndEmitsNoCounterStrike)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(target);
	const auto shots = target->shots.available();
	const auto health = melee->getAvailableHealth();
	const auto attacks = server.attacks.size();
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_EQ(melee->getAvailableHealth(), health);
	EXPECT_EQ(target->shots.available(), shots);
	EXPECT_FALSE(target->counterAttacks.canUse());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	ASSERT_EQ(server.attacks.size(), attacks + 1);
	EXPECT_FALSE(server.attacks.back().counter());
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, OtherDenialRetainsTokenAndDefiantBudget)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(target);
	give(melee, BonusType::BLOCKS_RETALIATION);
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_TRUE(target->counterAttacks.canUse());
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, UnlimitedRetaliationCancelsOneOpportunityAndThenRemainsUsable)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	give(target, BonusType::UNLIMITED_RETALIATIONS);
	install(target);
	ASSERT_TRUE(newHorizonsParalyzingGaze::consumeRetaliation(*gameHandler, *battle(), *target, *melee, false));
	EXPECT_TRUE(target->counterAttacks.canUse());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_FALSE(newHorizonsParalyzingGaze::consumeRetaliation(*gameHandler, *battle(), *target, *melee, false));
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, FirstStrikeCancellationDoesNotManufacturePostHitRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	give(target, BonusType::FIRST_STRIKE);
	give(target, BonusType::UNLIMITED_RETALIATIONS);
	install(target);
	const auto health = melee->getAvailableHealth();
	const auto attacks = server.attacks.size();
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_EQ(melee->getAvailableHealth(), health);
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(server.attacks.size(), attacks + 1);
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, DoubleStrikeConsumesOnePendingCounterWithoutAReplacementOpportunity)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	give(melee, BonusType::ADDITIONAL_ATTACK, 1);
	give(target, BonusType::UNLIMITED_RETALIATIONS);
	install(target);
	const auto health = melee->getAvailableHealth();
	const auto attacks = server.attacks.size();
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_EQ(melee->getAvailableHealth(), health);
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(server.attacks.size(), attacks + 2);
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, AvailableDefiantConsumesPendingTokenAndAllowsActualCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	const auto health = melee->getAvailableHealth();
	const auto attacks = server.attacks.size();
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_LT(melee->getAvailableHealth(), health);
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
	EXPECT_EQ(server.attacks.size(), attacks + 2);
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, FrozenBeforeShatterRetainsPendingGazeAndUnspentCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(target);
	SetStackEffect frozen;
	frozen.battleID = battle()->getBattleID();
	frozen.toAdd.emplace_back(target->unitId(), std::vector<Bonus>{newHorizonsFrozen::marker(BonusSourceID(SpellID(SpellID::ICE_BOLT)), battle()->getRound())});
	gameHandler->sendAndApply(frozen);
	ASSERT_TRUE(newHorizonsFrozen::isFrozen(*target));
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_TRUE(target->counterAttacks.canUse());
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, LethalPrimaryDoesNotConsumeDeadRecipientsPendingCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	install(target);
	battle()->removeUnit(melee->unitId());
	melee = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(11, 5), 10000);
	forceMaximumDamage(melee);
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	ASSERT_FALSE(target->alive());
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, ActualRangedCounterCancellationDoesNotSpendAmmunition)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->moveUnit(melee->unitId(), BattleHex(3, 8));
	battle()->removeUnit(target->unitId());
	target = addStack(BattleSide::DEFENDER, creatureByName("core:archMage"), BattleHex(12, 5), 100);
	give(target, BonusType::RANGED_RETALIATION);
	give(target, BonusType::NO_MORALE);
	ASSERT_TRUE(battle()->battleCanShoot(target, source->getPosition()));
	const auto shots = target->shots.available();
	const auto health = source->getAvailableHealth();
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(shoot());
	EXPECT_EQ(source->getAvailableHealth(), health);
	EXPECT_EQ(target->shots.available(), shots);
	EXPECT_FALSE(target->counterAttacks.canUse());
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(server.attacks.size(), attacks + 1);
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, ActualRangedCounterDefiantConsumesBudgetAndFiresOneShot)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	battle()->moveUnit(melee->unitId(), BattleHex(3, 8));
	battle()->removeUnit(target->unitId());
	target = addStack(BattleSide::DEFENDER, creatureByName("core:archMage"), BattleHex(12, 5), 100);
	give(target, BonusType::RANGED_RETALIATION);
	give(target, BonusType::NO_MORALE);
	ASSERT_TRUE(battle()->battleCanShoot(target, source->getPosition()));
	const auto shots = target->shots.available();
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(shoot());
	EXPECT_EQ(target->shots.available(), shots - 1);
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
	EXPECT_EQ(server.attacks.size(), attacks + 2);
}
namespace
{
class GazePacketObserver final : public RecordingGameServer
{
public:
	const CStack * target = nullptr;
	int originalShots = 0;
	std::vector<std::string> phases;
	void applyPack(CPackForClient & packet) override
	{
		RecordingGameServer::applyPack(packet);
		if(dynamic_cast<BattleUnitsChanged *>(&packet))
		{
			phases.push_back("counter-spent-token-present");
			EXPECT_FALSE(target->counterAttacks.canUse());
			EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
			EXPECT_EQ(target->shots.available(), originalShots);
		}
		if(dynamic_cast<SetStackEffect *>(&packet))
		{
			phases.push_back("token-removed");
			EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
		}
		EXPECT_EQ(dynamic_cast<StartAction *>(&packet), nullptr);
		EXPECT_EQ(dynamic_cast<EndAction *>(&packet), nullptr);
		EXPECT_EQ(dynamic_cast<BattleAttack *>(&packet), nullptr);
	}
};
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, PreparedCancellationPublishesRestrictiveCounterStateBeforeExactRemoval)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto waiting = target->acquireState();
	waiting->afterWait();
	BattleUnitsChanged preparedWait;
	preparedWait.battleID = battle()->getBattleID();
	UnitChanges waitState(target->unitId(), UnitChanges::EOperation::UPDATE);
	waitState.data = waiting->save();
	preparedWait.changedStacks.push_back(std::move(waitState));
	gameHandler->sendAndApply(preparedWait);
	install(target);
	GazePacketObserver observer;
	observer.gameState = gameState();
	observer.target = target;
	observer.originalShots = target->shots.available();
	CGameHandler isolatedHandler(observer, gameState());
	ASSERT_TRUE(newHorizonsParalyzingGaze::consumeRetaliation(isolatedHandler, *battle(), *target, *melee, false));
	EXPECT_EQ(observer.phases, (std::vector<std::string>{"counter-spent-token-present", "token-removed"}));
	EXPECT_TRUE(target->waiting);
	EXPECT_TRUE(target->waitedThisTurn);
	EXPECT_FALSE(target->battlecraftWaitBonusUsed);
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, ZeroActualGuardianDamageDoesNotApplyToken)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto state = target->acquireState();
	state->guardianSpiritHitPoints = 10000;
	state->guardianSpiritRoundsRemaining = 3;
	BattleUnitsChanged shield;
	shield.battleID = battle()->getBattleID();
	UnitChanges update(target->unitId(), UnitChanges::EOperation::UPDATE);
	update.data = state->save();
	shield.changedStacks.push_back(std::move(update));
	gameHandler->sendAndApply(shield);
	const auto health = target->getAvailableHealth();
	ASSERT_TRUE(shoot());
	EXPECT_EQ(target->getAvailableHealth(), health);
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, HiddenAjitAndPuppetFriendlyTargetDoNotAcquireEntitlement)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	CPlayerBattleCallback hidden(battle(), PlayerColor(1));
	ASSERT_EQ(hidden.battleGetFightingHero(BattleSide::ATTACKER), nullptr);
	EXPECT_FALSE(newHorizonsParalyzingGaze::qualifiesAttack(hidden, source, target, true, true, false));
	const SpellID puppet(SpellID::decode("new-horizons:puppetMaster"));
	source->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(1))));
	ASSERT_EQ(battle()->battleGetOwner(source), PlayerColor(0));
	ASSERT_EQ(battle()->battleGetActionController(source), PlayerColor(1));
	EXPECT_FALSE(newHorizonsParalyzingGaze::qualifiesAttack(*battle(), source, target, true, true, false));
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, MagicalOtherBlockerPreservesPendingTokenAndAvailableDefiant)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	melee->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::BLOCKS_RETALIATION,
		BonusSource::SPELL_EFFECT, 1, BonusSourceID(SpellID(SpellID::BLIND))));
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, EarlierInnateDefiantHasPriorityThenGazeCancelsRemainingCounter)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	give(melee, BonusType::BLOCKS_RETALIATION);
	const auto health = melee->getAvailableHealth();
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	EXPECT_EQ(melee->getAvailableHealth(), health);
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, PendingOldTokenUsesCurrentRoundDefiantAvailability)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	ArmorerDefiantState spent;
	spent.consumeAt(battle()->getRound());
	battle()->setArmorerDefiantState(BattleSide::DEFENDER, spent); // Explicit prepared history.
	ASSERT_TRUE(newHorizonsParalyzingGaze::consumeRetaliation(*gameHandler, *battle(), *target, *melee, false));
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	install(target);
	BattleNextRound next;
	next.battleID = battle()->getBattleID();
	gameHandler->sendAndApply(next);
	ASSERT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	const auto used = target->counterAttacks.available();
	EXPECT_FALSE(newHorizonsParalyzingGaze::consumeRetaliation(*gameHandler, *battle(), *target, *melee, false));
	EXPECT_EQ(target->counterAttacks.available(), used); // Ignored cancellation emits no counter itself.
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, battle()->getRound());
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, CureAndDispelKeepPhysicalPendingTokenUnderExistingSourceGates)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	if(!defenderSideHero->hasSpellbook())
		giveArtifact(defenderSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	defenderSideHero->addSpellToSpellbook(SpellID::CURE);
	defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	defenderSideHero->initializeSpellPoints(1000);
	install(target);
	StacksInjured wound;
	wound.battleID = battle()->getBattleID();
	auto & injury = wound.stacks.emplace_back();
	injury.stackAttacked = target->unitId();
	injury.damageAmount = 1;
	target->prepareAttacked(injury, gameHandler->getRandomGenerator());
	ASSERT_EQ(injury.killedAmount, 0u);
	gameHandler->sendAndApply(wound);
	battle()->activeStack = target->unitId();
	ASSERT_GT(target->getSurvivingMissingHealth(), 0);
	ASSERT_TRUE(castOn(defenderSideHero, SpellID::CURE, target));
	EXPECT_EQ(target->getSurvivingMissingHealth(), 0);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	if(!attackerSideHero->hasSpellbook())
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
	attackerSideHero->addSpellToSpellbook(SpellID::DISPEL);
	attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	attackerSideHero->initializeSpellPoints(1000);
	Bonus haste(BonusDuration::N_TURNS, BonusType::STACKS_SPEED,
		BonusSource::SPELL_EFFECT, 2, BonusSourceID(SpellID(SpellID::HASTE)));
	haste.turnsRemain = 1;
	SetStackEffect magic;
	magic.battleID = battle()->getBattleID();
	magic.toUpdate.emplace_back(target->unitId(), std::vector<Bonus>{haste});
	gameHandler->sendAndApply(magic);
	battle()->activeStack = source->unitId();
	ASSERT_TRUE(castOn(attackerSideHero, SpellID::DISPEL, target));
	EXPECT_FALSE(target->hasBonus(Selector::source(BonusSource::SPELL_EFFECT, BonusSourceID(SpellID(SpellID::HASTE)))));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
}
TEST_F(NewHorizonsParalyzingGazeBattleTest, MissingRecipientAndMalformedPendingRejectBeforeAnyCounterMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto rngBytes = [&]()
	{
		CMemorySerializer authority, fixture, defaults;
		authority.oser & *gameHandler->randomizer;
		fixture.oser & dynamic_cast<CRandomGenerator &>(*getRNG());
		defaults.oser & CRandomGenerator::getDefault();
		return std::tuple{authority.extractBuffer(), fixture.extractBuffer(), defaults.extractBuffer()};
	};
	const auto rngBefore = rngBytes();
	const auto sourceBefore = source->save(), meleeBefore = melee->save();
	const auto missingBefore = target->save();
	const auto missing = static_cast<uint32_t>(std::numeric_limits<int32_t>::max());
	ASSERT_EQ(battle()->battleGetUnitByID(missing), nullptr);
	EXPECT_NO_THROW(newHorizonsParalyzingGaze::applyAfterShot(*gameHandler, *battle(), source->unitId(), missing, true));
	EXPECT_EQ(target->save(), missingBefore);
	EXPECT_EQ(source->save(), sourceBefore);
	EXPECT_EQ(melee->save(), meleeBefore);
	EXPECT_EQ(rngBytes(), rngBefore);
	auto bad = newHorizonsParalyzingGaze::marker();
	bad.duration = BonusDuration::N_TURNS;
	target->addNewBonus(std::make_shared<Bonus>(bad)); // Deliberate malformed negative fixture.
	const auto before = target->save();
	ASSERT_TRUE(before["state"]["counterAttacks"]["totalCache"].isNull())
		<< before.toCompactString(); // Preserve the cold-cache rejection premise.
	const auto tokenBefore = bad.toJsonNode();
	const auto defiantBefore = battle()->getArmorerDefiantState(BattleSide::DEFENDER);
	const auto attacks = server.attacks.size();
	EXPECT_THROW(newHorizonsParalyzingGaze::consumeRetaliation(*gameHandler, *battle(), *target, *melee, false), std::runtime_error);
	const auto after = target->save();
	EXPECT_EQ(after, before)
		<< "before=" << before.toCompactString() << "\nafter=" << after.toCompactString();
	EXPECT_EQ(source->save(), sourceBefore);
	EXPECT_EQ(melee->save(), meleeBefore);
	EXPECT_EQ(rngBytes(), rngBefore);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER), defiantBefore);
	EXPECT_TRUE(std::ranges::any_of(target->getExportedBonusList(), [&](const auto & bonus)
		{ return bonus && bonus->toJsonNode() == tokenBefore; }));
	EXPECT_EQ(server.attacks.size(), attacks);
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, MultipleCountersSpendOneThenNextAttackerReceivesRealRetaliation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	give(target, BonusType::ADDITIONAL_RETALIATION, 1);
	auto * second = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(12, 4), 1);
	give(second, BonusType::NO_MORALE);
	install(target);
	const auto counters = target->counterAttacks.available();
	battle()->activeStack = melee->unitId();
	ASSERT_TRUE(attack(melee, target->getPosition()));
	ASSERT_FALSE(newHorizonsParalyzingGaze::pending(*target));
	ASSERT_TRUE(target->counterAttacks.canUse());
	EXPECT_EQ(target->counterAttacks.available(), counters - 1);
	battle()->activeStack = second->unitId();
	const auto attacks = server.attacks.size();
	ASSERT_TRUE(attack(second, target->getPosition()));
	ASSERT_EQ(server.attacks.size(), attacks + 2);
	EXPECT_TRUE(server.attacks.back().counter());
	EXPECT_EQ(target->counterAttacks.available(), counters - 2);
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, NewGazeDenialQueryDoesNotReadHiddenOpposingDefiantPolicy)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	CPlayerBattleCallback hidden(battle(), PlayerColor(0));
	ASSERT_EQ(hidden.battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	EXPECT_FALSE(hidden.battleCanUseDefiant(BattleAttackInfo(melee, target, 0, false),
		newHorizonsArmorer::DefiantDenialCause::PARALYZING_GAZE));
	EXPECT_TRUE(battle()->battleCanUseDefiant(BattleAttackInfo(melee, target, 0, false),
		newHorizonsArmorer::DefiantDenialCause::PARALYZING_GAZE));
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, GazeDenialUsesPhysicalTokenRatherThanIncomingDamageClassAndRejectsFriendlyController)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	install(target);
	BattleAttackInfo incoming(melee, target, 0, false);
	incoming.physicalDamage = false;
	EXPECT_TRUE(battle()->battleCanUseDefiant(incoming, newHorizonsArmorer::DefiantDenialCause::PARALYZING_GAZE));
	EXPECT_FALSE(battle()->battleCanUseDefiant(incoming, newHorizonsArmorer::DefiantDenialCause::INNATE_BLOCK));
	const SpellID puppet(SpellID::decode("new-horizons:puppetMaster"));
	melee->addNewBonus(std::make_shared<Bonus>(newHorizonsPuppetMaster::controlMarker(puppet, PlayerColor(1))));
	ASSERT_EQ(battle()->battleGetActionController(melee), PlayerColor(1));
	EXPECT_FALSE(battle()->battleCanUseGazeDefiant(melee, target, false));
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, PureMeleeEstimateAccountsForPendingGazeAndAvailableDefiantWithoutSpending)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const BattleAttackInfo incoming(melee, target, 0, false);
	DamageEstimation before;
	battle()->battleEstimateDamage(incoming, &before);
	ASSERT_GT(before.damage.max, 0);
	install(target);
	const auto state = target->save();
	DamageEstimation canceled;
	battle()->battleEstimateDamage(incoming, &canceled);
	EXPECT_EQ(canceled.damage.max, 0);
	EXPECT_EQ(target->save(), state);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	ASSERT_NO_FATAL_FAILURE(acquireDefiant());
	DamageEstimation ignored;
	battle()->battleEstimateDamage(incoming, &ignored);
	EXPECT_GT(ignored.damage.max, 0);
	EXPECT_EQ(battle()->getArmorerDefiantState(BattleSide::DEFENDER).lastConsumedRound, -1);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, OrdinaryGrantedRangedCounterAppliesGazeToItsPrimaryVictim)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->moveUnit(melee->unitId(), BattleHex(3, 8));
	battle()->removeUnit(target->unitId());
	target = addStack(BattleSide::DEFENDER, creatureByName("core:archMage"), BattleHex(12, 5), 1);
	give(source, BonusType::RANGED_RETALIATION);
	give(target, BonusType::NO_MORALE);

	ASSERT_TRUE(battle()->battleCanShootAction(target, source->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(source, target->getPosition()));
	const auto health = target->getAvailableHealth();
	const auto attacks = server.attacks.size();
	battle()->activeStack = target->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(1),
		BattleAction::makeShotAttack(target, source)));
	ASSERT_TRUE(target->alive());
	ASSERT_TRUE(source->alive());
	EXPECT_LT(target->getAvailableHealth(), health);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*source));
	ASSERT_EQ(server.attacks.size(), attacks + 2);
	EXPECT_TRUE(server.attacks.at(attacks + 1).counter());
}

TEST_F(NewHorizonsParalyzingGazeBattleTest, ResourceFirstStrikeRangedCounterAppliesGazeToItsPrimaryVictim)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->moveUnit(melee->unitId(), BattleHex(3, 8));
	battle()->removeUnit(target->unitId());
	target = addStack(BattleSide::DEFENDER, creatureByName("core:archMage"), BattleHex(12, 5), 1);
	give(source, BonusType::RANGED_RETALIATION);
	give(target, BonusType::NO_MORALE);
	give(source, BonusType::FIRST_STRIKE);

	ASSERT_TRUE(battle()->battleCanShootAction(target, source->getPosition()));
	ASSERT_TRUE(battle()->battleCanShoot(source, target->getPosition()));
	const auto health = target->getAvailableHealth();
	const auto attacks = server.attacks.size();
	battle()->activeStack = target->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(battle()->getBattleID(), PlayerColor(1),
		BattleAction::makeShotAttack(target, source)));
	ASSERT_TRUE(target->alive());
	ASSERT_TRUE(source->alive());
	EXPECT_LT(target->getAvailableHealth(), health);
	EXPECT_TRUE(newHorizonsParalyzingGaze::pending(*target));
	EXPECT_FALSE(newHorizonsParalyzingGaze::pending(*source));
	ASSERT_EQ(server.attacks.size(), attacks + 2);
	EXPECT_TRUE(server.attacks.at(attacks + 0).counter());
}
