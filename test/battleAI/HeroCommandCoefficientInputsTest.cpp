/*
 * HeroCommandCoefficientInputsTest.cpp, part of VCMI engine
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt.
 */
#include "StdInc.h"
#include "OwnedExportMeleeFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"
#include "../../lib/battle/HeroCommandCoefficient.h"
#include "../../lib/battle/HeroCommandCoefficientMath.h"
#include "../../lib/battle/NewHorizonsDivineMandate.h"
#include "../../lib/bonuses/BonusList.h"
#include "../../lib/bonuses/PrimarySkillPolicy.h"
#include <limits>

namespace
{
class CoefficientProbe final : public CPlayerBattleCallback
{
public:
	using CPlayerBattleCallback::CPlayerBattleCallback;
	int order(const JsonNode & formula, const CGHeroInstance * hero, const HeroOrderState & state, uint32_t recipient) const
	{
		return heroOrderCoefficient(formula, hero, &state, recipient);
	}
};

JsonNode formula(double base, double attack, double defense)
{
	JsonNode result;
	result["base"].Float() = base;
	result["attack"].Float() = attack;
	result["defense"].Float() = defense;
	return result;
}
}

class HeroCommandCoefficientInputsTest : public OwnedExportMeleeTest
{
protected:
	void commandPolicy(CGHeroInstance * hero, const char * perk)
	{
		const SecondarySkill command(SecondarySkill::decode("new-horizons:command"));
		ASSERT_GE(command.getNum(), 0);
		hero->setSecSkillLevel(command, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(hero->getPerkState().canAcquirePerk("new-horizons:command", perk,
			hero->getPerkSkillRank("new-horizons:command")));
		hero->applyPerkSelection({"new-horizons:command", perk});
		ASSERT_TRUE(hero->hasActivePerk("new-horizons:command", perk));
	}

	TConstBonusListPtr primary(const CGHeroInstance & hero)
	{
		return hero.getAllBonuses(Selector::type()(BonusType::PRIMARY_SKILL));
	}

	BattleAttackInfo braceStrike(const CBattleInfoCallback & state)
	{
		BattleAttackInfo strike(state.battleGetUnitByID(braced->unitId()),
			state.battleGetUnitByID(incoming->unitId()), 0, false);
		strike.bracePreemptive = true;
		return strike;
	}
};

TEST_F(HeroCommandCoefficientInputsTest, NativeAndExplicitPrimaryInputsPreserveRankPerkAndEfficiencyArithmetic)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	releaseKnown();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 10, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(commandPolicy(attackerSideHero, "new-horizons:command.aggressiveCommander"));
	ASSERT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK), 10);
	ASSERT_EQ(attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE), 20);
	EXPECT_EQ(heroCommands::coefficient(formula(11, 1, 1), *attackerSideHero), 46);
	const SecondarySkill command(SecondarySkill::decode("new-horizons:command"));
	for(const int rank : {1, 2, 3})
	{
		attackerSideHero->setSecSkillLevel(command, rank, ChangeValueMode::ABSOLUTE);
		const auto inputs = heroCommands::CoefficientInputs::projected(*attackerSideHero, primary(*attackerSideHero));
		for(const int warcasting : {-1, 0, 10, 150})
			for(const int divine : {-1, 0, 15, 100})
				EXPECT_EQ(heroCommands::coefficient(formula(11, 1, 1), inputs, warcasting, divine),
					heroCommands::coefficient(formula(11, 1, 1), *attackerSideHero, warcasting, divine));
	}
}

TEST_F(HeroCommandCoefficientInputsTest, DefensivePerkLegacyExclusionAndOverflowKeepOneCanonicalFormula)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	releaseKnown();
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 10, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(commandPolicy(attackerSideHero, "new-horizons:command.defensiveCommander"));
	const auto inputs = heroCommands::CoefficientInputs::projected(*attackerSideHero, primary(*attackerSideHero));
	EXPECT_EQ(heroCommands::coefficient(formula(11, 1, 1), inputs, 0, 0), 48);
	EXPECT_EQ(heroCommands::coefficient(formula(11, 1, 1), inputs, 0, 0, false), 44);
	for(const auto & value : {formula(0.5, 0, 0), formula(-0.5, 0, 0)})
		EXPECT_EQ(heroCommands::coefficient(value, inputs, 0, 0), heroCommands::coefficient(value, *attackerSideHero));
	const double maximum = std::numeric_limits<double>::max();
	const auto nonfinite = formula(1, maximum, -maximum);
	// Canonical coefficient * efficiency / 100 overflows BEFORE division.
	EXPECT_THROW(heroCommands::coefficient(nonfinite, inputs, 0, 0), std::runtime_error);
	EXPECT_THROW(heroCommands::coefficient(nonfinite, *attackerSideHero), std::runtime_error);
	// Finite efficiency-scaled terms; only the weighted SUM overflows here.
	for(const int sign : {-1, 1})
	{
		const auto finite = formula(sign * maximum, sign * (maximum / 256), 0);
		const int expected = sign > 0 ? heroCommands::MAX_EFFECT_PERCENT : heroCommands::MIN_EFFECT_PERCENT;
		EXPECT_EQ(heroCommands::coefficient(finite, inputs, 0, 0), expected);
		EXPECT_EQ(heroCommands::coefficient(finite, *attackerSideHero), expected);
	}
	// Finite terms whose separate products overflow; exact cancellation is 1.
	EXPECT_EQ(heroCommands::coefficientMath::boundedCoefficient({1, maximum / 2, -maximum / 2}, {1, 4, 4}), 1);
	EXPECT_EQ(heroCommands::coefficientMath::boundedCoefficient({0, maximum / 2, 0}, {1, 4, 0}), heroCommands::MAX_EFFECT_PERCENT);
	EXPECT_EQ(heroCommands::coefficientMath::boundedCoefficient({0, -maximum / 2, 0}, {1, 4, 0}), heroCommands::MIN_EFFECT_PERCENT);
	EXPECT_EQ(heroCommands::coefficient(formula(0.5, 0, 0), inputs, 0, 0), 1);
	EXPECT_EQ(heroCommands::coefficient(formula(-0.5, 0, 0), inputs, 0, 0), -1);
	EXPECT_THROW(heroCommands::coefficient(formula(std::numeric_limits<double>::quiet_NaN(), 0, 0), inputs, 0, 0), std::runtime_error);
	EXPECT_THROW(heroCommands::CoefficientInputs::projected(*attackerSideHero, nullptr), std::invalid_argument);
	auto wrongDomain = std::make_shared<BonusList>();
	wrongDomain->push_back(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_THROW(heroCommands::CoefficientInputs::projected(*attackerSideHero, wrongDomain), std::invalid_argument);
}

TEST_F(HeroCommandCoefficientInputsTest, RawPlayerDamageAuthorityAndHiddenProxyRemainDistinctWithoutLeakingInputs)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, false));
	releaseKnown();
	CPlayerBattleCallback raw(battle(), PlayerColor(1));
	ASSERT_TRUE(raw.battleGetOwnerHero(braced));
	ASSERT_FALSE(raw.battleGetFightingHero(BattleSide::ATTACKER));
	EXPECT_TRUE(raw.battleGetHeroBonuses(BattleSide::ATTACKER, Selector::all)->empty());
	const auto native = battle()->calculateDmgRange(braceStrike(*battle()));
	const auto player = raw.calculateDmgRange(braceStrike(raw));
	EXPECT_EQ(player.damage.min, native.damage.min);
	EXPECT_EQ(player.damage.max, native.damage.max);
	EXPECT_THAT(player.attackerOrderCauses, ::testing::Contains(HeroCommand::BRACE));
	auto perspective = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(1));
	auto proxy = std::make_shared<HypotheticBattle>(environment.get(), perspective);
	// Mechanical owner resolution is raw; public hero inputs remain visibility-gated.
	ASSERT_EQ(proxy->battleGetOwnerHero(proxy->battleGetUnitByID(braced->unitId())), attackerSideHero);
	ASSERT_FALSE(proxy->battleGetFightingHero(BattleSide::ATTACKER));
	for(const bool unstacked : {false, true})
		EXPECT_TRUE(proxy->battleGetHeroBonuses(BattleSide::ATTACKER, Selector::all, unstacked)->empty());
	const auto projected = proxy->calculateDmgRange(braceStrike(*proxy));
	EXPECT_EQ(projected.damage.min, native.damage.min);
	EXPECT_EQ(projected.damage.max, native.damage.max);
	EXPECT_EQ(projected.attackerOrderCauses, native.attackerOrderCauses);
	EXPECT_THAT(projected.attackerOrderCauses, ::testing::Contains(HeroCommand::BRACE));
}

TEST_F(HeroCommandCoefficientInputsTest, OwnedRatingsReachCrownWarcastingAndPotencyCompositionWithoutNativeFallback)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, false));
	releaseKnown();
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(captureOwned());
	Bonus rating = aura(BonusDuration::ONE_BATTLE);
	rating.subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
	rating.val = 20;
	rating.limiter.reset(); // This witness deliberately reaches the HERO rating node.
	addOrigin(*braced, rating);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	const auto rng = projectedHitTest::serialized(*gameHandler->randomizer);
	CoefficientProbe probe(known.get(), PlayerColor(1));
	const auto * owner = probe.battleGetOwnerHero(known->battleGetUnitByID(braced->unitId()));
	ASSERT_EQ(owner, attackerSideHero);
	EXPECT_TRUE(probe.battleGetHeroBonuses(BattleSide::ATTACKER, Selector::all)->empty());
	const auto order = known->battleGetHeroOrderState(BattleSide::ATTACKER, HeroCommand::BRACE);
	ASSERT_TRUE(order);
	auto state = *order;
	state.warcastingBonusPercent = 10;
	state.sacredCommandEfficiencyBonusPercent = 5;
	state.crownAndAltarRecipientUnitIds = {braced->unitId()};
	const auto source = known->initialExportSubject()->world().projectedHeroBonuses(BattleSide::ATTACKER, false);
	auto ratings = std::make_shared<BonusList>();
	source->getBonuses(*ratings, Selector::type()(BonusType::PRIMARY_SKILL));
	const auto inputs = heroCommands::CoefficientInputs::projected(*owner, ratings);
	ASSERT_GT(inputs.primary(PrimarySkill::DEFENSE), owner->getPrimSkillLevel(PrimarySkill::DEFENSE));
	const auto value = formula(10, 0.2, 0.6);
	const int intrinsic = heroCommands::coefficient(value, inputs, 10, 5);
	const int companions = heroCommands::coefficient(newHorizonsDivineMandate::crownOrderFormula(value, true), inputs, 10, 5);
	EXPECT_EQ(probe.order(value, owner, state, braced->unitId()), heroCommands::orderPotency(intrinsic, companions, state, known->getMagicRules()));
	EXPECT_NE(probe.order(value, owner, state, braced->unitId()),
		heroCommands::orderPotency(heroCommands::coefficient(value, *owner, 10, 5),
			heroCommands::coefficient(newHorizonsDivineMandate::crownOrderFormula(value, true), *owner, 10, 5), state, known->getMagicRules()));
	EXPECT_EQ(projectedHitTest::serialized(*gameHandler->randomizer), rng);
	EXPECT_FALSE(heroOrderValue::prepareBraceStage(environment.get(), known, action())); // Full witness retained.
}

TEST_F(HeroCommandCoefficientInputsTest, ActualBraceDamageMatchesNativeAfterOwnedProviderRetirementAndSameOriginatingAdd)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(true, false));
	releaseKnown();
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 20, ChangeValueMode::ABSOLUTE);
	ASSERT_NO_FATAL_FAILURE(captureOwned());
	Bonus rating = aura(BonusDuration::ONE_BATTLE);
	rating.subtype = BonusSubtypeID(PrimarySkill::DEFENSE);
	rating.val = 20;
	rating.limiter.reset();
	addOrigin(*braced, rating);
	ASSERT_NO_FATAL_FAILURE(startOwned());
	known->moveUnit(incoming->unitId(), BattleHex(92));
	const auto predicted = known->calculateDmgRange(braceStrike(*known));
	ASSERT_EQ(predicted.damage.min, predicted.damage.max);
	ASSERT_THAT(predicted.attackerOrderCauses, ::testing::Contains(HeroCommand::BRACE));
	known.reset();
	seed.reset(); // Frozen scalar range only; no source/branch provider survives.
	braced->addNewBonus(std::make_shared<Bonus>(rating));
	server.attacks.clear();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(1),
		BattleAction::makeMeleeAttack(incoming, braced, BattleHex(92), false)));
	ASSERT_FALSE(server.attacks.empty());
	ASSERT_EQ(server.attacks.front().stackAttacking, braced->unitId());
	ASSERT_EQ(server.attacks.front().bsa.size(), 1);
	EXPECT_EQ(server.attacks.front().bsa.front().damageAmount, predicted.damage.min);
}

class LegacyHeroCoefficientInputsTest : public HeroCommandCoefficientInputsTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		useCommands = false; // Existing legacy-primary fixture policy, not a new clamp.
		ProjectedTargetHitFixture::mapLoaded(loaded);
		// Pair empty legacy development with its historical perk-offer capture.
		captureLegacyPerkOfferPolicy(*loaded);
	}
};

TEST_F(LegacyHeroCoefficientInputsTest, ExplicitLegacyRatingsMatchTheActualNativePrimaryCache)
{
	ASSERT_NO_FATAL_FAILURE(prepareSequence(false, false));
	releaseKnown();
	ASSERT_FALSE(attackerSideHero->usesPrimaryGrowth());
	const auto & perks = attackerSideHero->getPerkState().rules;
	const JsonNode current(JsonPath::builtin("config/newHorizonsPerks"));
	ASSERT_TRUE(newHorizonsHeroes::usesPerkRules(perks));
	ASSERT_FALSE(newHorizonsHeroes::usesWeightedPerkOffers(perks));
	EXPECT_EQ(perks["skills"], current["skills"]); // Definitions/status/ranks are unchanged.
	EXPECT_THROW(newHorizonsHeroes::validateWeightedPerkCapture(JsonNode(), current), std::runtime_error);
	EXPECT_NO_THROW(newHorizonsHeroes::validateWeightedPerkCapture(JsonNode(), perks));
	attackerSideHero->setPrimarySkill(PrimarySkill::ATTACK, 10000, ChangeValueMode::ABSOLUTE);
	attackerSideHero->setPrimarySkill(PrimarySkill::DEFENSE, 0, ChangeValueMode::ABSOLUTE);
	const auto inputs = heroCommands::CoefficientInputs::projected(*attackerSideHero, primary(*attackerSideHero));
	EXPECT_EQ(inputs.primary(PrimarySkill::ATTACK), attackerSideHero->getPrimSkillLevel(PrimarySkill::ATTACK));
	EXPECT_EQ(inputs.primary(PrimarySkill::DEFENSE), attackerSideHero->getPrimSkillLevel(PrimarySkill::DEFENSE));
	EXPECT_EQ(heroCommands::coefficient(formula(5, 0.1, 0.2), inputs, 10, 5),
		heroCommands::coefficient(formula(5, 0.1, 0.2), *attackerSideHero, 10, 5));
}

TEST(PrimarySkillPolicyTest, GrowthAndLegacyBoundsRetainTheirDifferentClampContracts)
{
	JsonNode growth;
	growth["maxPrimary"].Integer() = 3;
	EXPECT_EQ(primarySkillPolicy::growthValue(-20, growth), 0);
	EXPECT_EQ(primarySkillPolicy::growthValue(20, growth), 3);
	primarySkillPolicy::LegacyBounds bounds{{2, 1, 0, 0}, {1, 9, 9, 9}};
	EXPECT_EQ(bounds.value(PrimarySkill::ATTACK, -20), 2);
	EXPECT_EQ(bounds.value(PrimarySkill::ATTACK, 20), 2); // Existing max(min,max) policy.
	EXPECT_EQ(bounds.value(PrimarySkill::DEFENSE, 20), 9);
}
