/*
 * NewHorizonsSylvanLuckAITest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "ProjectedTargetHitFixture.h"
#include "../../lib/CRandomGenerator.h"
#include "../server/battles/HeroCommandFixture.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/NewHorizonsCombatSkills.h"
#include "../../AI/BattleAI/HypotheticBattle.h"
#include "../../AI/BattleAI/HypotheticBattleTypes.h"

namespace
{
JsonNode legacySylvanRegistry()
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
	rules.Struct().erase("sylvanTacticsPolicy");
	rules.setOverrideFlag(true);
	return rules;
}

JsonNode certainLuck()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(100);
	return result;
}

JsonNode noGoodLuck()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(0);
	return result;
}

// Count the exact virtual query performed by the shared ordinary-creature
// classifier. Other ownership/Luck queries retain their real implementation.
class EligibilityCountingStack final : public StackWithBonuses
{
public:
	mutable unsigned eligibilityQueries = 0;
	EligibilityCountingStack(const HypotheticBattle * battle, const battle::Unit * source)
		: StackWithBonuses(battle, source) {}
	TConstBonusListPtr getAllBonuses(const CSelector & selector, const std::string & key = "") const override
	{
		if(key == "type_" + std::to_string(static_cast<int>(BonusType::SIEGE_WEAPON)))
			++eligibilityQueries;
		return StackWithBonuses::getAllBonuses(selector, key);
	}
};

class SylvanEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit SylvanEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsSylvanLuckAITest : public HeroCommandFixture
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, legacySylvanRegistry());
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, certainLuck());
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS, JsonNode(true));
	}

	static void luck(CStack * unit, int value)
	{
		unit->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::LUCK,
			BonusSource::OTHER, value, BonusSourceID()));
	}
};
}

class NewHorizonsGenericLuckyRecoveryAITest : public NewHorizonsSylvanLuckAITest
{
protected:
	CStack * source = nullptr;
	CStack * victim = nullptr;

	void mapLoaded(CMap * map) override
	{
		NewHorizonsSylvanLuckAITest::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacySylvanRegistry());
	}

	void prepareRecovery(bool sylvan = false, bool opposingSerendipity = false)
	{
		startGame();
		const std::string skillKey(newHorizonsCombatSkills::LUCK_SKILL_ID);
		const SecondarySkill skill(SecondarySkill::decode(skillKey));
		ASSERT_TRUE(skill.hasValue());
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({skillKey, "new-horizons:luck.fortuneSFavor"});
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({skillKey, std::string(newHorizonsCombatSkills::LUCKY_RECOVERY_PERK_ID)});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(skillKey,
			std::string(newHorizonsCombatSkills::LUCKY_RECOVERY_PERK_ID)));
		if(sylvan)
		{
			const std::string sylvanSkill = "new-horizons:sylvanLuck";
			attackerSideHero->setSecSkillLevel(SecondarySkill(SecondarySkill::decode(sylvanSkill)),
				MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			attackerSideHero->applyPerkSelection({sylvanSkill, "new-horizons:sylvanLuck.luckyRecovery"});
		}
		if(opposingSerendipity)
		{
			// Lucky Day and Serendipity share the Advanced Luck tier. Select
			// Serendipity on the other hero instead of inventing an enabled token.
			defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({skillKey, "new-horizons:luck.fortuneSFavor"});
			defenderSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
			defenderSideHero->applyPerkSelection({skillKey, "new-horizons:luck.serendipity"});
			ASSERT_TRUE(defenderSideHero->hasActivePerk(skillKey, "new-horizons:luck.serendipity"));
		}
		startBattle();
		beginCombat();
		source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
		victim = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
		blockRetaliation(victim);
		forceMaximumDamage(source);
		int64_t wound = source->getMaxHealth() + 50;
		source->damage(wound);
		battle()->activeStack = source->unitId();
	}

	void verifyForecast(bool sylvan)
	{
		ASSERT_NO_FATAL_FAILURE(prepareRecovery(sylvan));
		const auto health = source->getAvailableHealth();
		const auto victimHealth = victim->getAvailableHealth();
		SylvanEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto model = std::make_shared<HypotheticBattle>(&environment, callback);
		DamageCache cache;
		const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source, victim, 0, false),
			source->getPosition(), cache, model);
		ASSERT_TRUE(preview.attackerState);
		ASSERT_TRUE(preview.defenderDead);
		ASSERT_FALSE(preview.fortuneStrikes.empty());
		EXPECT_EQ(preview.attackerState->getAvailableHealth(),
			health + (sylvan ? std::min<int64_t>(50, victimHealth / 10) : 0));
		EXPECT_EQ(model->getForUpdate(source->unitId())->getAvailableHealth(), health);
		EXPECT_EQ(source->getAvailableHealth(), health);
		BattleExchangeVariant exchange;
		exchange.trackAttack(preview, model, cache);
		EXPECT_EQ(model->getForUpdate(source->unitId())->getAvailableHealth(),
			preview.attackerState->getAvailableHealth());
		EXPECT_EQ(model->getForUpdate(source->unitId())->getCount(), 2);
		EXPECT_EQ(source->getAvailableHealth(), health);
	}
};

TEST_F(NewHorizonsGenericLuckyRecoveryAITest, LuckyDayDoesNotHealCreatureForecastOrSelectedReplay)
{
	ASSERT_NO_FATAL_FAILURE(verifyForecast(false));
}

TEST_F(NewHorizonsGenericLuckyRecoveryAITest, LuckyDayDoesNotDoubleSylvanRecovery)
{
	ASSERT_NO_FATAL_FAILURE(verifyForecast(true));
}

TEST_F(NewHorizonsGenericLuckyRecoveryAITest, NeutralUnknownRangedDeadAndRecordOnlyBranchesDoNotHeal)
{
	ASSERT_NO_FATAL_FAILURE(prepareRecovery());
	SylvanEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	for(int scenario = 0; scenario < 6; ++scenario)
	{
		auto model = std::make_shared<HypotheticBattle>(&environment, parent);
		auto striker = model->getForUpdate(source->unitId());
		auto target = model->getForUpdate(victim->unitId());
		if(scenario == 4)
		{
			int64_t damage = striker->getAvailableHealth();
			striker->damage(damage);
		}
		const auto before = striker->getAvailableHealth();
		BattleAttackInfo attack(striker.get(), target.get(), 0, scenario == 3);
		const auto outcome = scenario == 0 ? ProjectedLuckOutcome::NEUTRAL
			: scenario == 1 ? ProjectedLuckOutcome::NEGATIVE
			: scenario == 2 ? ProjectedLuckOutcome::UNKNOWN : ProjectedLuckOutcome::POSITIVE;
		model->projectFortuneStrike(attack, {{target->unitId(), 200}}, striker.get(), false,
			outcome, scenario != 5);
		EXPECT_EQ(striker->getAvailableHealth(), before) << "scenario " << scenario;
		EXPECT_EQ(parent->getForUpdate(source->unitId())->getAvailableHealth(), source->getAvailableHealth());
	}
}

TEST_F(NewHorizonsSylvanLuckAITest, CertainStrikeCommitsRecoveryAndSharedCascadeOnlyToTheModel)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex - 1), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	luck(source, 1);

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.forestsFavor = true;
	fortune.luckyRecovery = true;
	fortune.sharedFortune = true;
	fortune.cascadingFortune = true;
	model->setSylvanLuckState(BattleSide::ATTACKER, fortune);

	auto projectedSource = model->getForUpdate(source->unitId());
	auto projectedTarget = model->getForUpdate(target->unitId());
	int64_t wound = 40;
	projectedSource->damage(wound);
	const auto before = projectedSource->getAvailableHealth();
	BattleAttackInfo attack(projectedSource.get(), projectedTarget.get(), 0, false);
	ASSERT_TRUE(model->fortuneStrikeIsCertain(attack));

	// A child made before the commit is a stable snapshot; later parent
	// transitions must not leak backwards into it.
	HypotheticBattle pristine(environment.get(), model);
	model->projectFortuneStrike(attack, {{target->unitId(), 100}}, projectedSource.get(), true);
	const auto & committed = model->getSylvanLuckState(BattleSide::ATTACKER);
	EXPECT_TRUE(committed.positiveLuckUnits.contains(source->unitId()));
	EXPECT_EQ(committed.speedBonus(source->unitId()), 2);
	EXPECT_TRUE(committed.cascadingPending);
	EXPECT_TRUE(committed.sharedUnits.contains(ally->unitId()));
	EXPECT_EQ(model->battleGetAttackLuck(model->getForUpdate(ally->unitId()).get(), projectedTarget.get(), false), 1);
	EXPECT_EQ(projectedSource->getAvailableHealth(), before + 10);

	EXPECT_FALSE(pristine.getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.contains(source->unitId()));
	EXPECT_FALSE(pristine.getSylvanLuckState(BattleSide::ATTACKER).cascadingPending);
	EXPECT_EQ(source->getAvailableHealth(), static_cast<int64_t>(source->getMaxHealth()) * source->getCount());

	// A child made after the commit receives the pending gift by value.  Its
	// activation consumes only the child copy, leaving the parent armed.
	HypotheticBattle child(environment.get(), model);
	child.nextTurn(source->unitId(), BattleUnitTurnReason::TURN_QUEUE);
	EXPECT_EQ(child.getSylvanLuckState(BattleSide::ATTACKER).temporaryLuck(source->unitId()), 3);
	EXPECT_EQ(child.battleGetAttackLuck(child.getForUpdate(source->unitId()).get(),
		child.getForUpdate(target->unitId()).get(), false), 4);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).cascadingPending);
}

TEST_F(NewHorizonsGenericLuckyRecoveryAITest, CapturedLuckAndPerfectFortuneOverrideCurrentFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepareRecovery(false, true));
	SylvanEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	for(const bool capturedPerfect : {false, true})
	{
		auto model = std::make_shared<HypotheticBattle>(&environment, parent);
		PerfectFortuneState perfect;
		perfect.enabled = true;
		model->setPerfectFortuneState(BattleSide::ATTACKER, perfect);
		const auto serendipity = model->getLuckSerendipityState(BattleSide::DEFENDER);
		ASSERT_TRUE(serendipity.enabled);
		ASSERT_EQ(serendipity.round, model->battleGetRound());
		ASSERT_FALSE(serendipity.currentRoundPositiveLuck);
		auto striker = model->getForUpdate(source->unitId());
		auto target = model->getForUpdate(victim->unitId());
		BattleAttackInfo attack(striker.get(), target.get(), 0, false);
		// Replay carries pre-hit eligibility even if the current attack no
		// longer qualifies; conversely, captured false must not consume a token.
		attack.secondaryAttack = capturedPerfect;
		ASSERT_EQ(model->battleCanTriggerPerfectFortune(attack), !capturedPerfect);
		if(!capturedPerfect)
		{
			ASSERT_EQ(model->captureFortuneStrikeOutcome(attack), ProjectedLuckOutcome::POSITIVE);
		}
		const auto health = striker->getAvailableHealth();
		model->projectFortuneStrike(attack, {{target->unitId(), 200}}, striker.get(), false,
			ProjectedLuckOutcome::UNKNOWN, true, capturedPerfect, BattleSide::ATTACKER,
			BattleSide::DEFENDER);
		EXPECT_EQ(model->getPerfectFortuneState(BattleSide::ATTACKER).used, capturedPerfect);
		// A selected replay may carry a pre-hit controller distinct from the
		// present allegiance. UNKNOWN stays unknown on that captured side.
		EXPECT_FALSE(model->getLuckSerendipityState(BattleSide::DEFENDER).currentRoundPositiveLuck);
		EXPECT_EQ(model->getLuckSerendipityState(BattleSide::DEFENDER).firstAttackUsed, !capturedPerfect);
		EXPECT_EQ(striker->getAvailableHealth(), health);
	}
	EXPECT_EQ(parent->getForUpdate(source->unitId())->getAvailableHealth(), source->getAvailableHealth());
	EXPECT_FALSE(parent->getPerfectFortuneState(BattleSide::ATTACKER).used);
}

TEST_F(NewHorizonsGenericLuckyRecoveryAITest, AbsentSnapshotsMatchExplicitlyCapturedFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepareRecovery());
	SylvanEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
	for(const bool enabled : {false, true})
	{
		auto fallback = std::make_shared<HypotheticBattle>(&environment, parent);
		auto captured = std::make_shared<HypotheticBattle>(&environment, parent);
		PerfectFortuneState perfect;
		perfect.enabled = enabled;
		fallback->setPerfectFortuneState(BattleSide::ATTACKER, perfect);
		captured->setPerfectFortuneState(BattleSide::ATTACKER, perfect);
		auto fallbackSource = fallback->getForUpdate(source->unitId());
		auto capturedSource = captured->getForUpdate(source->unitId());
		BattleAttackInfo fallbackAttack(fallbackSource.get(), fallback->getForUpdate(victim->unitId()).get(), 0, false);
		BattleAttackInfo capturedAttack(capturedSource.get(), captured->getForUpdate(victim->unitId()).get(), 0, false);
		const auto outcome = captured->captureFortuneStrikeOutcome(capturedAttack);
		const auto eligibility = captured->battleCanTriggerPerfectFortune(capturedAttack);
		fallback->projectFortuneStrike(fallbackAttack, {{victim->unitId(), 200}}, fallbackSource.get(), false);
		captured->projectFortuneStrike(capturedAttack, {{victim->unitId(), 200}}, capturedSource.get(), false,
			outcome, true, eligibility, BattleSide::ATTACKER);
		EXPECT_EQ(fallbackSource->getAvailableHealth(), capturedSource->getAvailableHealth());
		EXPECT_EQ(fallback->getPerfectFortuneState(BattleSide::ATTACKER),
			captured->getPerfectFortuneState(BattleSide::ATTACKER));
		EXPECT_EQ(fallback->getLuckSerendipityState(BattleSide::ATTACKER),
			captured->getLuckSerendipityState(BattleSide::ATTACKER));
	}
	EXPECT_EQ(parent->getForUpdate(source->unitId())->getAvailableHealth(), source->getAvailableHealth());
}

TEST_F(NewHorizonsSylvanLuckAITest, MasterGateContinuationPreservesProjectedActivationState)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::STACK_GETS_TURN,
		BonusType::PRIMARY_SKILL, BonusSource::OTHER, 3, BonusSourceID(),
		BonusSubtypeID(PrimarySkill::DEFENSE)));

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.forestsFavor = true;
	fortune.positiveLuckUnits.insert(source->unitId());
	fortune.speedUnits.insert(source->unitId());
	model.setSylvanLuckState(BattleSide::ATTACKER, fortune);
	auto projected = model.getForUpdate(source->unitId());
	projected->defending = true;
	projected->defensiveStanceMeleeBonus = 2;
	ASSERT_FALSE(projected->getAllBonuses(Bonus::UntilGetsTurn)->empty());

	model.nextTurn(source->unitId(), BattleUnitTurnReason::MASTER_GATE_CONTINUATION);

	EXPECT_FALSE(projected->getAllBonuses(Bonus::UntilGetsTurn)->empty());
	EXPECT_TRUE(projected->defending);
	EXPECT_EQ(projected->defensiveStanceMeleeBonus, 2);
	EXPECT_TRUE(model.getSylvanLuckState(BattleSide::ATTACKER).speedUnits.contains(source->unitId()));
}

TEST_F(NewHorizonsSylvanLuckAITest, PerfectMomentProjectsFirstShotOnlyAndCommitsUseOnlyToSelectedModel)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, noGoodLuck());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	luck(source, 5);
	battle()->activeStack = source->unitId();
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.perfectMoment = true;
	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	BattleAttackInfo attack(source, target, 0, true);
	const auto projected = AttackPossibility::evaluate(attack, BattleHex::INVALID, cache, model);
	ASSERT_TRUE(projected.perfectMoment);
	ASSERT_EQ(projected.fortuneStrikes.size(), 2u);
	EXPECT_TRUE(projected.fortuneStrikes[0].perfectMoment);
	EXPECT_FALSE(projected.fortuneStrikes[1].perfectMoment);
	EXPECT_FALSE(model->getSylvanLuckState(BattleSide::ATTACKER).perfectMomentUsed);
	PotentialTargets candidates(source, cache, model);
	EXPECT_TRUE(candidates.bestAction().perfectMoment);
	BattleExchangeVariant exchange;
	exchange.trackAttack(projected, model, cache);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).perfectMomentUsed);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.contains(source->unitId()));
	EXPECT_FALSE(battle()->getSylvanLuckState(BattleSide::ATTACKER).perfectMomentUsed);
}

TEST_F(NewHorizonsSylvanLuckAITest, PerfectMomentLethalPreviewAvoidsRetaliationAndDoesNotForceEnemy)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 3);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	luck(source, 5);
	battle()->activeStack = source->unitId();
	battle()->getSide(BattleSide::ATTACKER).sylvanLuck.perfectMoment = true;
	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	const auto preview = AttackPossibility::evaluate(BattleAttackInfo(source, target, 0, false), source->getPosition(), cache, model);
	ASSERT_TRUE(preview.perfectMoment);
	EXPECT_TRUE(preview.defenderDead);
	EXPECT_EQ(preview.attackerState->getAvailableHealth(), source->getAvailableHealth());
	for(const auto & strike : preview.fortuneStrikes)
	{
		if(strike.retaliation)
		{
			EXPECT_FALSE(strike.perfectMoment);
		}
	}
}

TEST_F(NewHorizonsSylvanLuckAITest, SerendipityChanceOnlyDoesNotQualifyForProjectedPerfectMoment)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, noGoodLuck());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:marksman"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 5), 100);
	luck(source, 4);
	battle()->activeStack = source->unitId();
	auto & fortune = battle()->getSide(BattleSide::ATTACKER).sylvanLuck;
	fortune.perfectMoment = fortune.serendipity = true;
	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	DamageCache cache;
	BattleAttackInfo attack(source, target, 0, true);
	EXPECT_EQ(model->battleGetAttackLuck(source, target, true), 5);
	EXPECT_FALSE(model->battleCanUsePerfectMoment(source, target, true));
	const auto preview = AttackPossibility::evaluate(attack, BattleHex::INVALID, cache, model);
	ASSERT_FALSE(preview.perfectMoment);
	ASSERT_EQ(preview.fortuneStrikes.size(), 2u);
	EXPECT_FALSE(preview.fortuneStrikes[0].perfectMoment);
	EXPECT_FALSE(preview.fortuneStrikes[1].perfectMoment);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).positiveLuckUnits.empty());
	EXPECT_FALSE(fortune.perfectMomentUsed);
}

TEST_F(NewHorizonsSylvanLuckAITest, RecoveryUsesOnlyPrimaryNonOverkillDamageInClassicMode)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	gameState()->getMap().overrideGameSetting(EGameSettings::COMBAT_LUCKY_STRIKE_AFFECTS_ALL_TARGETS, JsonNode(false));
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	auto * collateral = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex + 1), 1);
	luck(source, 1);

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	HypotheticBattle model(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.luckyRecovery = true;
	fortune.cascadingFortune = true;
	model.setSylvanLuckState(BattleSide::ATTACKER, fortune);
	auto projectedSource = model.getForUpdate(source->unitId());
	auto projectedTarget = model.getForUpdate(target->unitId());
	auto projectedCollateral = model.getForUpdate(collateral->unitId());
	int64_t wound = 40;
	projectedSource->damage(wound);
	const auto before = projectedSource->getAvailableHealth();
	BattleAttackInfo attack(projectedSource.get(), projectedTarget.get(), 0, false);
	model.projectFortuneStrike(attack,
		{{target->unitId(), 19}, {collateral->unitId(), 1000}}, projectedSource.get(), false);
	EXPECT_EQ(projectedSource->getAvailableHealth(), before + 1);
	EXPECT_FALSE(model.getSylvanLuckState(BattleSide::ATTACKER).cascadingPending);
}

TEST_F(NewHorizonsSylvanLuckAITest, SharedFortuneUsesProjectedAttackPosition)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * ally = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex + 7), 1);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 1);
	luck(source, 1);
	source->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::RETURN_AFTER_STRIKE, BonusSource::OTHER, 1, BonusSourceID()));

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	SylvanLuckState fortune;
	fortune.sharedFortune = true;
	model->setSylvanLuckState(BattleSide::ATTACKER, fortune);

	const BattleHex movedFrom(leftHex + 6);
	model->getForUpdate(ally->unitId())->setPosition(leftHex + 7);
	AttackPossibility ap(movedFrom, target->getPosition(), BattleAttackInfo(source, target, 0, false));
	ap.attackerState = model->getForUpdate(source->unitId())->acquireState();
	ap.attackerState->setPosition(movedFrom);
	ap.affectedUnits.push_back(model->getForUpdate(target->unitId())->acquireState());
	FortuneStrikeProjection strike;
	strike.attackerId = source->unitId();
	strike.defenderId = target->unitId();
	strike.hits.emplace_back(target->unitId(), 10);
	ap.fortuneStrikes.push_back(strike);

	BattleExchangeVariant exchange;
	DamageCache cache;
	exchange.trackAttack(ap, model, cache);
	EXPECT_TRUE(model->getSylvanLuckState(BattleSide::ATTACKER).sharedUnits.contains(ally->unitId()));
	EXPECT_EQ(model->getForUpdate(source->unitId())->getPosition(), source->getPosition());
}

TEST_F(NewHorizonsSylvanLuckAITest, AttackPreviewReplaysDefenderRetaliationFortunePerStrike)
{
	ASSERT_NO_FATAL_FAILURE(startGame());
	ASSERT_NO_FATAL_FAILURE(startBattle());
	ASSERT_NO_FATAL_FAILURE(beginCombat());
	auto * source = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 10);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 10);
	luck(source, 1);
	luck(target, 1);

	auto environment = std::make_shared<SylvanEnvironment>(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(environment.get(), callback);
	SylvanLuckState attackerFortune;
	attackerFortune.forestsFavor = true;
	model->setSylvanLuckState(BattleSide::ATTACKER, attackerFortune);
	SylvanLuckState defenderFortune;
	defenderFortune.forestsFavor = true;
	model->setSylvanLuckState(BattleSide::DEFENDER, defenderFortune);

	AttackPossibility ap(source->getPosition(), target->getPosition(), BattleAttackInfo(source, target, 0, false));
	ap.attackerState = model->getForUpdate(source->unitId())->acquireState();
	const auto sourceBefore = ap.attackerState->getAvailableHealth();
	const auto targetBefore = model->getForUpdate(target->unitId())->getAvailableHealth();
	int64_t attackerDamage = 10;
	ap.attackerState->damage(attackerDamage);
	auto targetState = model->getForUpdate(target->unitId())->acquireState();
	int64_t targetDamage = 10;
	targetState->damage(targetDamage);
	ap.affectedUnits.push_back(targetState);
	FortuneStrikeProjection first;
	first.attackerId = source->unitId();
	first.defenderId = target->unitId();
	first.hits.emplace_back(target->unitId(), 10);
	FortuneStrikeProjection retaliation;
	retaliation.attackerId = target->unitId();
	retaliation.defenderId = source->unitId();
	retaliation.retaliation = true;
	retaliation.hits.emplace_back(source->unitId(), 10);
	ap.fortuneStrikes = {first, retaliation};

	BattleExchangeVariant exchange;
	DamageCache cache;
	exchange.trackAttack(ap, model, cache);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::ATTACKER).speedBonus(source->unitId()), 2);
	EXPECT_EQ(model->getSylvanLuckState(BattleSide::DEFENDER).speedBonus(target->unitId()), 2);
	EXPECT_EQ(model->getForUpdate(source->unitId())->getAvailableHealth(), sourceBefore - 10);
	EXPECT_EQ(model->getForUpdate(target->unitId())->getAvailableHealth(), targetBefore - 10);
}

// The reference performs the former eager public classification, then supplies
// that result (or the captured override) to the real projection. No copied
// aftermath, Luck probability or score formula is used as the oracle.
class CapturedFortuneEligibilityTest : public NewHorizonsGenericLuckyRecoveryAITest
{
protected:
	void verify(std::optional<bool> captured, bool secondary = false, bool siege = false)
	{
		ASSERT_NO_FATAL_FAILURE(prepareRecovery(false, true));
		SylvanEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
		auto parent = std::make_shared<HypotheticBattle>(&environment, callback);
		auto observed = std::make_shared<HypotheticBattle>(&environment, parent);
		auto reference = std::make_shared<HypotheticBattle>(&environment, parent);
		EligibilityCountingStack actor(observed.get(), victim), oracle(reference.get(), victim);
		if(siege)
		{
			const Bonus machine(BonusDuration::PERMANENT, BonusType::SIEGE_WEAPON,
				BonusSource::OTHER, 1, BonusSourceID());
			actor.addUnitBonus({machine});
			oracle.addUnitBonus({machine});
		}
		BattleAttackInfo attack(&actor, observed->getForUpdate(source->unitId()).get(), 0, false);
		BattleAttackInfo oracleAttack(&oracle, reference->getForUpdate(source->unitId()).get(), 0, false);
		attack.secondaryAttack = oracleAttack.secondaryAttack = secondary;
		const auto side = BattleSide::DEFENDER;
		ASSERT_TRUE(observed->getLuckSerendipityState(side).enabled);
		const auto before = observed->getLuckSerendipityState(side);
		ASSERT_FALSE(before.firstAttackUsed);
		const auto actorBefore = projectedHitTest::serialized(actor.save());
		const auto parentBefore = projectedHitTest::serialized(parent->getForUpdate(victim->unitId())->save());
		const auto nativeBefore = gameState()->saveToMemory();
		const auto randomizerBefore = projectedHitTest::serialized(*gameHandler->randomizer);
		const auto rngBefore = projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG()));
		const auto defaultBefore = projectedHitTest::serialized(CRandomGenerator::getDefault());
		actor.eligibilityQueries = oracle.eligibilityQueries = 0;

		const bool live = !secondary && newHorizonsCombatSkills::isPhysicalCreatureLuckAttack(&oracle, true);
		const bool expected = captured ? *captured : live;
		reference->projectFortuneStrike(oracleAttack, {}, nullptr, false,
			ProjectedLuckOutcome::NEUTRAL, false, false, side, side, expected);
		observed->projectFortuneStrike(attack, {}, nullptr, false,
			ProjectedLuckOutcome::NEUTRAL, false, false, side, side, captured);
		EXPECT_EQ(actor.eligibilityQueries, !captured && !secondary ? 1u : 0u);
		EXPECT_EQ(oracle.eligibilityQueries, secondary ? 0u : 1u);
		const auto actual = observed->getLuckSerendipityState(side);
		EXPECT_EQ(actual, reference->getLuckSerendipityState(side));
		EXPECT_EQ(actual.firstAttackUsed, before.firstAttackUsed || expected);
		EXPECT_EQ(actual.currentRoundPositiveLuck, before.currentRoundPositiveLuck);
		EXPECT_EQ(projectedHitTest::serialized(actor.save()), actorBefore);
		EXPECT_EQ(projectedHitTest::serialized(oracle.save()), actorBefore);
		EXPECT_EQ(projectedHitTest::serialized(parent->getForUpdate(victim->unitId())->save()), parentBefore);
		for(const auto battleSide : {BattleSide::ATTACKER, BattleSide::DEFENDER})
		{
			EXPECT_EQ(observed->getSylvanLuckState(battleSide), reference->getSylvanLuckState(battleSide));
			EXPECT_EQ(observed->getPerfectFortuneState(battleSide), reference->getPerfectFortuneState(battleSide));
			for(const auto * unit : observed->battleGetAllUnits(false))
				EXPECT_EQ(projectedHitTest::serialized(observed->getForUpdate(unit->unitId())->save()),
					projectedHitTest::serialized(reference->getForUpdate(unit->unitId())->save()));
		}
		EXPECT_EQ(gameState()->saveToMemory(), nativeBefore);
		EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), randomizerBefore);
		EXPECT_EQ(projectedHitTest::serialized(dynamic_cast<CRandomGenerator &>(*getRNG())), rngBefore);
		EXPECT_EQ(projectedHitTest::serialized(CRandomGenerator::getDefault()), defaultBefore);
	}
};

TEST_F(CapturedFortuneEligibilityTest, CapturedTrueOverridesNowIneligibleActorWithoutRequery)
{
	ASSERT_NO_FATAL_FAILURE(verify(true, false, true));
}

TEST_F(CapturedFortuneEligibilityTest, CapturedFalseOverridesCurrentlyOrdinaryActorWithoutRequery)
{
	ASSERT_NO_FATAL_FAILURE(verify(false));
}

TEST_F(CapturedFortuneEligibilityTest, AbsentSnapshotUsesOneRealOrdinaryCreatureQuery)
{
	ASSERT_NO_FATAL_FAILURE(verify(std::nullopt));
}

TEST_F(CapturedFortuneEligibilityTest, AbsentSecondarySnapshotRetainsShortCircuitAndNoQuery)
{
	ASSERT_NO_FATAL_FAILURE(verify(std::nullopt, true));
}
