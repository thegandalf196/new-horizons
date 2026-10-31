/*
 * NewHorizonsSylvanTacticsAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../AI/BattleAI/SylvanTacticsProjection.h"
#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/callback/GameRandomizer.h"
#include "../../lib/CRandomGenerator.h"
#include "../../lib/battle/NewHorizonsArchery.h"
#include <functional>

class NewHorizonsSylvanTacticsAITest : public HeroCommandFixture,
	public testing::WithParamInterface<bool>
{
protected:
	std::unique_ptr<projectedHitTest::Environment> environment;
	std::shared_ptr<HypotheticBattle> model;
	CStack * source = nullptr;
	CStack * target = nullptr;
	CStack * ally = nullptr;
	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		if(GetParam()) rules["sylvanTacticsPolicy"].Integer() = 1;
		else rules.Struct().erase("sylvanTacticsPolicy");
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
	}
	void prepare(bool shooting = false, BattleSide sourceSide = BattleSide::ATTACKER)
	{
		ASSERT_NO_FATAL_FAILURE(prepareCommands());
		source = addStack(sourceSide, creatureByName(shooting ? "core:titan" : "core:angel"),
			shooting ? BattleHex(3, 5) : BattleHex(leftHex), 10);
		ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 1), 1);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
		blockRetaliation(target);
		battle()->activeStack = source->unitId();
		environment = std::make_unique<projectedHitTest::Environment>(gameState());
		auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		model = std::make_shared<HypotheticBattle>(environment.get(), live);
	}
	void enable(const std::function<void(SylvanTacticsState &)> & configure)
	{
		auto fortune = model->getSylvanLuckState(BattleSide::ATTACKER);
		fortune.tactics.enabled = GetParam();
		if(GetParam()) configure(fortune.tactics);
		model->setSylvanLuckState(BattleSide::ATTACKER, fortune);
	}
};

TEST_P(NewHorizonsSylvanTacticsAITest, BasePolicyDoesNotAllocateStrikeBranch)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto &) {});
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*model));
	EXPECT_FALSE(sylvanTacticsAI::dynamicDamage(*model, source, target));
	enable([](auto & state) { state.fleetfoot = true; state.quickRedeployment = true; });
	EXPECT_FALSE(sylvanTacticsAI::needsProjection(*model));
}

TEST_P(NewHorizonsSylvanTacticsAITest, FleetfootWaitAndRedeploymentNeverChangeInitiative)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.fleetfoot = true; state.quickRedeployment = true; });
	auto actor = model->getForUpdate(source->unitId());
	const auto initiative = actor->getInitiative();
	const auto speed = actor->getMovementRange();
	model->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(actor->getMovementRange(), speed);
	EXPECT_EQ(actor->getMovementRange(1), speed - (GetParam() ? 1 : 0));
	model->makeWait(actor.get());
	EXPECT_EQ(actor->getMovementRange(), speed - (GetParam() ? 1 : 0));
	model->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(actor->getMovementRange(), speed - (GetParam() ? 1 : 0));
	EXPECT_EQ(actor->getInitiative(), initiative);
	actor->defending = true;
	model->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(actor->getMovementRange(), speed + (GetParam() ? 1 : 0));
	EXPECT_EQ(actor->getMovementRange(1), speed - (GetParam() ? 1 : 0));
	EXPECT_EQ(actor->getInitiative(), initiative);
	model->completeSwiftNormalActivation(actor->unitId());
	EXPECT_EQ(actor->getMovementRange(), speed - (GetParam() ? 1 : 0));
}

TEST_P(NewHorizonsSylvanTacticsAITest, LastStandDefendCannotGrantRedeployment)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.quickRedeployment = true; });
	auto actor = model->getForUpdate(source->unitId());
	const auto speed = actor->getMovementRange();
	model->applyArmorerLastStandDefend(actor->unitId());
	model->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(actor->getMovementRange(), speed);
}

TEST_P(NewHorizonsSylvanTacticsAITest, NeutralRangedStrikeCommitsSharedEffectsOnlyAtBoundary)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.coveringFire = true; state.crossfire = true; });
	const auto before = model->getSylvanLuckState(BattleSide::ATTACKER);
	auto child = std::make_shared<HypotheticBattle>(environment.get(), model);
	auto actor = child->getForUpdate(source->unitId());
	auto victim = child->getForUpdate(target->unitId());
	BattleAttackInfo hit(actor.get(), victim.get(), 0, true);
	sylvanTacticsAI::StrikeProjection projection(child.get());
	int64_t damage = 10;
	projection.damage(*child, hit, *victim, damage, battle::DamageProvenance::PHYSICAL_CREATURE);
	EXPECT_EQ(child->getSylvanLuckState(BattleSide::ATTACKER), before);
	projection.finish(actor->unitId());
	const auto tactics = child->getSylvanLuckState(BattleSide::ATTACKER).tactics;
	EXPECT_EQ(tactics.covered.contains(ally->unitId()), GetParam());
	EXPECT_EQ(tactics.crossfireBonus(ally->unitId(), victim->unitId()), GetParam() ? 15 : 0);
	EXPECT_EQ(tactics.crossfireBonus(actor->unitId(), victim->unitId()), 0);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER), before);
	EXPECT_EQ(target->getAvailableHealth(), model->getForUpdate(target->unitId())->getAvailableHealth());
	child->nextRound();
	EXPECT_TRUE(child->getSylvanLuckState(BattleSide::ATTACKER).tactics.shootersByTarget.empty());
}

TEST_P(NewHorizonsSylvanTacticsAITest, PhysicalKillGrantsWildHuntWithoutInitiativeAndExpiresNextRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.wildHunt = true; });
	auto actor = model->getForUpdate(source->unitId());
	auto victim = model->getForUpdate(target->unitId());
	const auto speed = actor->getMovementRange();
	const auto initiative = actor->getInitiative();
	BattleAttackInfo hit(actor.get(), victim.get(), 0, false);
	sylvanTacticsAI::StrikeProjection projection(model.get());
	int64_t damage = victim->getAvailableHealth();
	projection.damage(*model, hit, *victim, damage, battle::DamageProvenance::PHYSICAL_CREATURE);
	projection.finish(actor->unitId());
	EXPECT_EQ(actor->getMovementRange(), speed + (GetParam() ? 2 : 0));
	EXPECT_EQ(actor->getMovementRange(1), speed);
	EXPECT_EQ(actor->getInitiative(), initiative);
	model->nextRound();
	EXPECT_EQ(actor->getMovementRange(), speed);
}

TEST_P(NewHorizonsSylvanTacticsAITest, NativeRebirthAndSpellDamageCannotTriggerWildHunt)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.wildHunt = true; });
	for(bool physical : {false, true})
	{
		auto child = std::make_shared<HypotheticBattle>(environment.get(), model);
		auto actor = child->getForUpdate(source->unitId());
		auto victim = child->getForUpdate(target->unitId());
		if(physical)
		{
			Bonus rebirth(BonusDuration::ONE_BATTLE, BonusType::REBIRTH, BonusSource::OTHER, 100, BonusSourceID());
			Bonus casts(BonusDuration::ONE_BATTLE, BonusType::CASTS, BonusSource::OTHER, 1, BonusSourceID());
			child->addUnitBonus(victim->unitId(), {rebirth, casts});
			victim->casts.reset();
			ASSERT_TRUE(child->hasReadyNativeRebirth(victim.get()));
		}
		BattleAttackInfo hit(actor.get(), victim.get(), 0, false);
		sylvanTacticsAI::StrikeProjection projection(child.get());
		int64_t damage = victim->getAvailableHealth();
		projection.damage(*child, hit, *victim, damage,
			physical ? battle::DamageProvenance::PHYSICAL_CREATURE : battle::DamageProvenance::SPELL);
		projection.finish(actor->unitId());
		EXPECT_FALSE(child->getSylvanLuckState(BattleSide::ATTACKER).tactics.huntTriggered);
	}
}

TEST_P(NewHorizonsSylvanTacticsAITest, MeleePreviewAndSelectedReplayAgreeWithoutChangingParentOrRng)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.disruptiveStrike = true; state.wildHunt = true; });
	const auto parentState = model->getSylvanLuckState(BattleSide::ATTACKER);
	const auto rng = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
	const auto defaultRng = projectedHitTest::serialized(CRandomGenerator::getDefault());
	const auto liveFortune = battle()->getSylvanLuckState(BattleSide::ATTACKER);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false),
		source->getPosition(), cache, model);
	ASSERT_TRUE(preview.attackerState);
	if(GetParam()) ASSERT_TRUE(preview.effectPreview);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER), parentState);
	if(GetParam()) EXPECT_TRUE(preview.effectPreview->getSylvanLuckState(BattleSide::ATTACKER).tactics.disrupted.contains(target->unitId()));
	BattleExchangeVariant exchange;
	exchange.trackAttack(preview, model, cache);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER).tactics.disrupted.contains(target->unitId()), GetParam());
	EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), rng);
	EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultRng);
	EXPECT_EQ(battle()->getSylvanLuckState(BattleSide::ATTACKER), liveFortune);
}

TEST_P(NewHorizonsSylvanTacticsAITest, FriendlyAndZeroDamageHitsDoNotArmStrikeHistory)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.coveringFire = true; state.crossfire = true; state.wildHunt = true; });
	auto actor = model->getForUpdate(source->unitId());
	auto friendly = model->getForUpdate(ally->unitId());
	auto victim = model->getForUpdate(target->unitId());
	const auto before = model->getSylvanLuckState(BattleSide::ATTACKER);
	sylvanTacticsAI::StrikeProjection projection(model.get());
	BattleAttackInfo friendlyHit(actor.get(), friendly.get(), 0, true);
	int64_t friendlyDamage = friendly->getAvailableHealth();
	projection.damage(*model, friendlyHit, *friendly, friendlyDamage, battle::DamageProvenance::PHYSICAL_CREATURE);
	projection.finish(actor->unitId());
	BattleAttackInfo zeroHit(actor.get(), victim.get(), 0, true);
	int64_t zeroDamage = 0;
	projection.damage(*model, zeroHit, *victim, zeroDamage, battle::DamageProvenance::PHYSICAL_CREATURE);
	projection.finish(actor->unitId());
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER), before);
}

TEST_P(NewHorizonsSylvanTacticsAITest, DynamicDamageTracksPatientCrossfireAndCoverWindows)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.patientHunter = true; state.crossfire = true; });
	EXPECT_EQ(sylvanTacticsAI::dynamicDamage(*model, source, target), GetParam());
	auto fortune = model->getSylvanLuckState(BattleSide::ATTACKER);
	fortune.tactics = {};
	model->setSylvanLuckState(BattleSide::ATTACKER, fortune);
	auto defense = model->getSylvanLuckState(BattleSide::DEFENDER);
	defense.tactics.enabled = GetParam();
	defense.tactics.dappledCover = GetParam();
	model->setSylvanLuckState(BattleSide::DEFENDER, defense);
	EXPECT_EQ(sylvanTacticsAI::dynamicDamage(*model, source, target), GetParam());
}

TEST_P(NewHorizonsSylvanTacticsAITest, RainSecondaryReceiptPreservesNewTacticsAndLegacyReplay)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	const auto skill = SecondarySkill(SecondarySkill::decode(std::string(newHorizonsArchery::SKILL)));
	ASSERT_TRUE(skill.hasValue());
	attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
	// Preserve Orrin's authored Basic choice and fill the required Advanced slot.
	// Archery Crossfire is inert here: the fixture has only one friendly shooter.
	ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::CROSSFIRE), MasteryLevel::EXPERT));
	attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL), std::string(newHorizonsArchery::CROSSFIRE)});
	ASSERT_TRUE(attackerSideHero->getPerkState().canAcquirePerk(std::string(newHorizonsArchery::SKILL),
		std::string(newHorizonsArchery::RAIN_OF_ARROWS), MasteryLevel::EXPERT));
	attackerSideHero->applyPerkSelection({std::string(newHorizonsArchery::SKILL), std::string(newHorizonsArchery::RAIN_OF_ARROWS)});
	ASSERT_NO_FATAL_FAILURE(startBattle());
	source = addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(3, 5), 10);
	ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(4, 5), 1);
	target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(12, 5), 100);
	auto * secondary = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(13, 5), 1);
	BattleUnitsChanged remove;
	remove.battleID = BattleID(0);
	for(const auto * unit : battle()->battleGetAllUnits(false))
		if(unit != source && unit != ally && unit != target && unit != secondary)
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
	gameHandler->sendAndApply(remove);
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	environment = std::make_unique<projectedHitTest::Environment>(gameState());
	auto live = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	model = std::make_shared<HypotheticBattle>(environment.get(), live);
	enable([](auto & state) { state.wildHunt = true; state.crossfire = true; state.coveringFire = true; });
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, true), BattleHex::INVALID, cache, model);
	ASSERT_TRUE(preview.effectPreview);
	ASSERT_FALSE(preview.fortuneStrikes.empty());
	EXPECT_EQ(preview.tacticsSecondaryHits.size(), GetParam() ? 1u : 0u);
	EXPECT_FALSE(preview.effectPreview->getForUpdate(secondary->unitId())->alive());
	if(GetParam())
	{
		const auto state = preview.effectPreview->getSylvanLuckState(BattleSide::ATTACKER).tactics;
		EXPECT_TRUE(state.huntTriggered);
		EXPECT_TRUE(state.covered.contains(ally->unitId()));
		EXPECT_EQ(state.crossfireBonus(ally->unitId(), secondary->unitId()), 15);
	}
	BattleExchangeVariant exchange;
	exchange.trackAttack(preview, model, cache);
	EXPECT_EQ(model->getForUpdate(secondary->unitId())->alive(), !GetParam());
	if(GetParam())
		EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER), preview.effectPreview->getSylvanLuckState(BattleSide::ATTACKER));
	EXPECT_TRUE(secondary->alive());
}

TEST_P(NewHorizonsSylvanTacticsAITest, GuardCacheFollowsProviderAcrossHypnotizeAndActivationExpiry)
{
	// Target matching compares the shooter's effective owner with the target's
	// initial side. Use the opposing army's shooter so this transferred guard
	// remains a legal target under that existing asymmetric targeting rule.
	ASSERT_NO_FATAL_FAILURE(prepare(true, BattleSide::DEFENDER));
	enable([this](auto & state) { state.coveringFire = true; state.covered.insert(ally->unitId()); });
	auto shooter = model->getForUpdate(source->unitId());
	ASSERT_TRUE(shooter->canShoot());
	auto guarded = model->getForUpdate(ally->unitId());
	model->addUnitBonus(guarded->unitId(), {Bonus(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID())});
	ASSERT_EQ(guarded->unitSide(), BattleSide::ATTACKER);
	ASSERT_EQ(model->battleGetOwner(guarded.get()), PlayerColor(1));
	ASSERT_EQ(model->battleGetOwner(shooter.get()), PlayerColor(1));
	ASSERT_TRUE(model->battleMatchOwner(shooter.get(), guarded.get()));
	const auto * occupant = model->battleGetUnitByPos(guarded->getPosition());
	ASSERT_NE(occupant, nullptr);
	ASSERT_EQ(occupant->unitId(), guarded->unitId());
	ASSERT_TRUE(model->battleCanShoot(shooter.get(), guarded->getPosition()));
	ASSERT_FALSE(model->getSylvanLuckState(BattleSide::DEFENDER).tactics.coveringFire);
	EXPECT_EQ(sylvanTacticsAI::dynamicDamage(*model, shooter.get(), guarded.get()), GetParam());
	BattleAttackInfo hit(shooter.get(), guarded.get(), 0, true);
	const auto guardedDamage = model->battleExpectedLuckDamage(hit);
	DamageCache cache;
	cache.cacheDamage(shooter.get(), guarded.get(), model);
	EXPECT_EQ(cache.getDamage(shooter.get(), guarded.get(), model), guardedDamage);
	model->nextTurn(guarded->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_FALSE(model->getSylvanLuckState(BattleSide::ATTACKER).tactics.covered.contains(guarded->unitId()));
	EXPECT_EQ(sylvanTacticsAI::dynamicDamage(*model, shooter.get(), guarded.get()), GetParam());
	const auto expiredDamage = model->battleExpectedLuckDamage(hit);
	if(GetParam()) EXPECT_GT(expiredDamage, guardedDamage);
	else EXPECT_EQ(expiredDamage, guardedDamage);
	EXPECT_EQ(cache.getDamage(shooter.get(), guarded.get(), model), expiredDamage);
}

TEST_P(NewHorizonsSylvanTacticsAITest, DappledCoverEndsAtDelayedActivationWithoutChangingLegacyWait)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	enable([](auto & state) { state.dappledCover = true; });
	auto actor = model->getForUpdate(source->unitId());
	model->makeWait(actor.get());
	EXPECT_EQ(newHorizonsSylvanTactics::rangedProtection(*model, actor.get()).size(), GetParam() ? 1u : 0u);
	model->nextTurn(actor->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_TRUE(actor->waited());
	EXPECT_TRUE(newHorizonsSylvanTactics::rangedProtection(*model, actor.get()).empty());
}

INSTANTIATE_TEST_SUITE_P(CapturedPolicy, NewHorizonsSylvanTacticsAITest, testing::Values(false, true));
