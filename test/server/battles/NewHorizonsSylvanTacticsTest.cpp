/* Part of VCMI / New Horizons. GPL v2.0 or later; see license.txt. */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/NewHorizonsSylvanTactics.h"
#include "../../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

class NewHorizonsSylvanTacticsTest : public BattleTestFixture
{
protected:
	CStack * source = nullptr;
	CStack * victim = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
		startGame();
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		rules["sylvanTacticsPolicy"] = JsonNode(1);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	}
	void prepare(bool shooting = false)
	{
		const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		startBattle();
		source = addStack(BattleSide::ATTACKER, creatureByName(shooting ? "core:titan" : "core:angel"), BattleHex(leftHex), 1);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(shooting ? rightHex + 4 : rightHex), 100);
		forceMaximumDamage(source);
		blockRetaliation(source);
		beginCombat();
	}
	SylvanTacticsState & tactics(BattleSide side = BattleSide::ATTACKER)
	{
		return battle()->getSide(side).sylvanLuck.tactics;
	}
	int64_t damage(bool shooting = false, int distance = 0, bool lucky = false,
		bool secondary = false, bool counter = false)
	{
		BattleAttackInfo info(source, victim, distance, shooting);
		info.luckyStrike = lucky;
		info.secondaryAttack = secondary;
		info.retaliation = counter;
		return battle()->calculateDmgRange(info).damage.min;
	}
};
TEST_F(NewHorizonsSylvanTacticsTest, NewRankGivesOnlyFlatInitiativeAndKeepsMovementIndependent)
{
	prepare();
	ASSERT_TRUE(tactics().enabled);
	const int initiative = source->getInitiative();
	const int movement = source->getMovementRange();
	tactics().fleetfoot = true;
	tactics().activated.erase(source->unitId());
	EXPECT_EQ(source->getMovementRange(), movement + 1);
	EXPECT_EQ(source->getInitiative(), initiative);
	EXPECT_EQ(attackerSideHero->valOfBonuses(BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE), 0);
}
TEST_F(NewHorizonsSylvanTacticsTest, PatientHunterUsesExistingFirstPhysicalAttackAfterWaitWindow)
{
	prepare();
	const auto neutral = damage();
	tactics().patientHunter = true;
	EXPECT_EQ(damage(), neutral);
	source->afterWait();
	EXPECT_EQ(damage(), neutral * 115 / 100);
	source->afterAttack(false, false, true);
	EXPECT_EQ(damage(), neutral);
}
TEST_F(NewHorizonsSylvanTacticsTest, DappledAndCoveringFireComposeAndGuardExpiresAtRealActivation)
{
	prepare(true);
	const auto neutral = damage(true);
	auto & guard = tactics(BattleSide::DEFENDER);
	guard.enabled = guard.dappledCover = guard.coveringFire = true;
	victim->afterWait();
	EXPECT_EQ(damage(true), neutral * 80 / 100);
	guard.covered.insert(victim->unitId());
	EXPECT_EQ(damage(true), neutral * 80 * 85 / 10000);
	guard.beginActivation(victim->unitId(), true);
	EXPECT_FALSE(guard.covered.contains(victim->unitId()));
}
TEST_F(NewHorizonsSylvanTacticsTest, MeasuredAdvanceExcludesStationaryLongCounterAndCollateralHits)
{
	prepare();
	tactics().measuredAdvance = true;
	const auto neutral = damage();
	const int halfSpeed = static_cast<int>(source->getMovementRange()) / 2;
	ASSERT_GE(halfSpeed, 1);
	EXPECT_EQ(damage(false, 1), neutral * 110 / 100);
	EXPECT_EQ(damage(false, halfSpeed), neutral * 110 / 100);
	EXPECT_EQ(damage(false, halfSpeed + 1), neutral);
	EXPECT_EQ(damage(false, 1, false, true), neutral);
	EXPECT_EQ(damage(false, 1, false, false, true), neutral);
}
TEST_F(NewHorizonsSylvanTacticsTest, CrossfireAffectsPrimaryOnlyAfterDifferentShooterActuallyHitTarget)
{
	prepare(true);
	tactics().crossfire = true;
	const auto neutral = damage(true);
	tactics().finishStrike(source->unitId(), {victim->unitId()}, {}, {}, false);
	EXPECT_EQ(damage(true), neutral);
	tactics().finishStrike(source->unitId() + 100, {victim->unitId()}, {}, {}, false);
	EXPECT_EQ(damage(true), neutral * 115 / 100);
	EXPECT_EQ(damage(true, 0, false, true), neutral);
}
TEST_F(NewHorizonsSylvanTacticsTest, PerfectMomentUsesTotalThreePointFiveBeforeRoundingAndNeverForcesLuck)
{
	prepare();
	const auto neutral = damage();
	tactics().perfectMoment = true;
	EXPECT_EQ(damage(), neutral);
	EXPECT_EQ(damage(false, 0, true), neutral * 350 / 100);
	EXPECT_FALSE(battle()->battleCanUsePerfectMoment(source, victim, false));
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE, BonusSource::OTHER, 25, BonusSourceID()));
	EXPECT_EQ(damage(false, 0, true), neutral * 350 / 100);
}
TEST_F(NewHorizonsSylvanTacticsTest, DisruptionIsInitiativeOnlyAndNonstackingAcrossSources)
{
	prepare();
	const int movement = victim->getMovementRange();
	const int initiative = victim->getInitiative();
	tactics().disruptiveStrike = true;
	tactics().finishStrike(source->unitId(), {}, {victim->unitId()}, {}, false);
	EXPECT_EQ(victim->getMovementRange(), movement);
	EXPECT_LT(victim->getInitiative(), initiative);
	EXPECT_EQ(battle()->battleSylvanTacticsInitiativePercent(victim), -15);
	tactics().finishStrike(source->unitId(), {}, {victim->unitId()}, {}, false);
	EXPECT_EQ(battle()->battleSylvanTacticsInitiativePercent(victim), -15);
	tactics().beginActivation(victim->unitId(), false);
	EXPECT_EQ(victim->getInitiative(), initiative);
}
TEST_F(NewHorizonsSylvanTacticsTest, AuthoritativeShotCarriesSharedGuardAndDistinctShooterHistory)
{
	prepare(true);
	tactics().coveringFire = tactics().crossfire = true;
	auto * neighbor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 1), 1);
	battle()->activeStack = source->unitId();
	const auto action = BattleAction::makeShotAttack(source, victim);
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(source->unitSide()), action));
	ASSERT_FALSE(server.attacks.empty());
	const auto & packet = server.attacks.front();
	ASSERT_TRUE(packet.fortuneState);
	EXPECT_TRUE(packet.fortuneState->tactics.covered.contains(neighbor->unitId()));
	EXPECT_TRUE(packet.fortuneState->tactics.shootersByTarget.contains(victim->unitId()));
}

TEST_F(NewHorizonsSylvanTacticsTest, AcceptedPerkSelectionsCaptureNewEffectsWithoutHistoricalTokens)
{
	const SecondarySkill skill(SecondarySkill::decode("new-horizons:sylvanLuck"));
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.forestSFavor"});
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.sharedFortune"});
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	attackerSideHero->applyPerkSelection({"new-horizons:sylvanLuck", "new-horizons:sylvanLuck.perfectMoment"});
	startBattle();
	EXPECT_TRUE(tactics().patientHunter);
	EXPECT_TRUE(tactics().coveringFire);
	EXPECT_TRUE(tactics().perfectMoment);
	const auto captured = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_FALSE(captured.forestsFavor);
	EXPECT_FALSE(captured.sharedFortune);
	EXPECT_FALSE(captured.canUsePerfectMoment());
}

TEST_F(NewHorizonsSylvanTacticsTest, RealActivationSnapshotsDefendBeforeClearingStance)
{
	prepare();
	tactics().quickRedeployment = true;
	const auto movement = source->getMovementRange();
	const auto initiative = source->getInitiative();
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::UNTIL_NEXT_CREATURE_ACTIVATION,
		BonusType::UNIT_DEFENDING, BonusSource::OTHER, 1, BonusSourceID()));
	battle()->nextTurn(source->unitId(), BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_FALSE(tactics().redeploymentActivation);
	battle()->nextTurn(source->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(tactics().redeploymentActivation, source->unitId());
	EXPECT_EQ(source->getMovementRange(), movement + 2);
	EXPECT_EQ(source->getInitiative(), initiative);
	EXPECT_FALSE(source->defended());
}
TEST_F(NewHorizonsSylvanTacticsTest, ForgedMissingOrChangedStrikeHistoryFailsBeforeMutation)
{
	prepare();
	BattleAttack packet;
	packet.stackAttacking = source->unitId();
	EXPECT_THROW(newHorizonsSylvanTactics::validateStrikeTransition(*battle(), packet), std::runtime_error);
	packet.fortuneSide = BattleSide::ATTACKER;
	packet.fortuneState = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	packet.fortuneState->tactics.huntTriggered = true;
	const auto before = tactics();
	EXPECT_THROW(newHorizonsSylvanTactics::validateStrikeTransition(*battle(), packet), std::runtime_error);
	EXPECT_EQ(tactics(), before);
}

TEST_F(NewHorizonsSylvanTacticsTest, FutureMovementDropsTransientSpeedButPreservesUnopenedFleetfoot)
{
	prepare();
	const auto current = source->getMovementRange();
	const auto future = source->getMovementRange(1);
	tactics().wildHunt = tactics().quickRedeployment = true;
	tactics().huntTriggered = true;
	tactics().redeploymentActivation = source->unitId();
	EXPECT_EQ(source->getMovementRange(), current + 4);
	EXPECT_EQ(source->getMovementRange(1), future);
	tactics().fleetfoot = true;
	tactics().activated.erase(source->unitId());
	EXPECT_EQ(source->getMovementRange(1), future + 1);
	tactics().beginActivation(source->unitId(), true);
	EXPECT_EQ(source->getMovementRange(), current + 3);
	EXPECT_EQ(source->getMovementRange(1), future);
}
TEST_F(NewHorizonsSylvanTacticsTest, DelayedActivationExpiresDappledBeforeStartActionWithoutClearingLegacyWait)
{
	prepare(true);
	const auto neutral = damage(true);
	auto & guard = tactics(BattleSide::DEFENDER);
	guard.enabled = guard.dappledCover = true;
	victim->afterWait();
	EXPECT_LT(damage(true), neutral);
	battle()->nextTurn(victim->unitId(), BattleUnitTurnReason::ACTION_REJECTED);
	EXPECT_LT(damage(true), neutral);
	battle()->nextTurn(victim->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(victim->waited()); // historical queue state deliberately retained
	EXPECT_TRUE(newHorizonsSylvanTactics::rangedProtection(*battle(), victim).empty());
	EXPECT_EQ(damage(true), neutral);
}
TEST_F(NewHorizonsSylvanTacticsTest, RangedGuardsNeverLowerDamageBeforeDefenseButOffensivePremiumStillRaisesIt)
{
	prepare(true);
	BattleAttackInfo info(source, victim, 0, true);
	const auto neutral = battle()->calculateDmgRange(info);
	auto & guard = tactics(BattleSide::DEFENDER);
	guard.enabled = guard.dappledCover = guard.coveringFire = true;
	victim->afterWait();
	guard.covered.insert(victim->unitId());
	const auto guarded = battle()->calculateDmgRange(info);
	EXPECT_LT(guarded.damage.min, neutral.damage.min);
	EXPECT_EQ(guarded.damageBeforeDefense.min, neutral.damageBeforeDefense.min);
	EXPECT_EQ(guarded.damageBeforeDefense.max, neutral.damageBeforeDefense.max);
	tactics().patientHunter = true;
	source->afterWait();
	const auto premium = battle()->calculateDmgRange(info);
	EXPECT_GT(premium.damageBeforeDefense.min, guarded.damageBeforeDefense.min);
}
TEST_F(NewHorizonsSylvanTacticsTest, MagicalShotWithoutSpellSubtypeCannotTriggerPhysicalTacticsAftermath)
{
	prepare(true);
	tactics().coveringFire = tactics().crossfire = tactics().wildHunt = true;
	auto * neighbor = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 1), 1);
	auto spellLike = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SPELL_LIKE_ATTACK, BonusSource::OTHER, 1, BonusSourceID());
	source->addNewBonus(spellLike); // absent spell subtype is the reviewed trigger
	EXPECT_FALSE(newHorizonsCombatSkills::isPhysicalAttackDamage(source, true));
	EXPECT_TRUE(newHorizonsCombatSkills::isPhysicalAttackDamage(source, false));
	EXPECT_FALSE(BattleAttackInfo(source, victim, 0, true).physicalDamage);
	int64_t wound = victim->getAvailableHealth() - 1;
	victim->damage(wound);
	battle()->activeStack = source->unitId();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(source->unitSide()), BattleAction::makeShotAttack(source, victim)));
	ASSERT_FALSE(server.attacks.empty());
	const auto & packet = server.attacks.front();
	EXPECT_FALSE(packet.spellLike());
	const auto primary = std::ranges::find_if(packet.bsa, [this](const BattleStackAttacked & hit)
	{
		return hit.stackAttacked == victim->unitId();
	});
	ASSERT_NE(primary, packet.bsa.end());
	EXPECT_GT(primary->damageAmount, 0);
	EXPECT_TRUE(primary->killed());
	EXPECT_FALSE(primary->willRebirth());
	ASSERT_TRUE(packet.fortuneState);
	EXPECT_FALSE(packet.fortuneState->tactics.covered.contains(neighbor->unitId()));
	EXPECT_TRUE(packet.fortuneState->tactics.shootersByTarget.empty());
	EXPECT_FALSE(packet.fortuneState->tactics.huntTriggered);
}

TEST_F(NewHorizonsSylvanTacticsTest, RankInitiativeExcludesDynamicSiegeMarkersWithoutRecursiveCacheQueries)
{
	prepare(); // BattleStart must initialize a real stack with the inherited rank bonus.
	EXPECT_EQ(source->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 1);
	const auto siege = std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::SIEGE_WEAPON, BonusSource::OTHER, 1, BonusSourceID());
	source->addNewBonus(siege);
	EXPECT_TRUE(source->hasBonusOfType(BonusType::SIEGE_WEAPON));
	EXPECT_EQ(source->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 0);
	source->removeBonus(siege);
	EXPECT_FALSE(source->hasBonusOfType(BonusType::SIEGE_WEAPON));
	EXPECT_EQ(source->valOfBonuses(BonusType::STACKS_INITIATIVE_FLAT), 1);
}
