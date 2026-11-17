/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "HeroCommandFixture.h"
#include "../../../lib/CRandomGenerator.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/ConsecratedArmsOrder.h"
#include "../../../lib/battle/NewHorizonsCreatureBurn.h"
#include "../../../lib/battle/NewHorizonsCoordinatedMarks.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/bonuses/BonusParameters.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/battles/BattleCreatureBurn.h"
#include "../../../server/battles/BattlePrimaryStrikeAftermath.h"

namespace
{
namespace burn = newHorizonsCreatureBurn;
namespace marks = newHorizonsCoordinatedMarks;
class ConsecratedArmsBurnMarkTest : public HeroCommandFixture
{
protected:
	bool specialtiesEnabled = true;
	CStack * source = nullptr;
	CStack * recipient = nullptr;
	void mapLoaded(CMap * map) override
	{
		HeroCommandFixture::mapLoaded(map);
		// These interaction tests admit explicit captured abilities; shipped defaults
		// omit these optional coefficients and correctly produce no effect.
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		auto & policy = rules["additionalSpecialties"]["ordinaryCreatureSpecialties"];
		for(const char * key : {"emberbrandDamagePercent", "consecratedTargetDefenseIgnorePercent",
			"featheredFeintPhysicalReductionIgnorePercent"})
			if(specialtiesEnabled) policy[key].Integer() = 20;
			else policy.Struct().erase(key);
		rules.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}
	void prepare(const char * hero = "core:ignissa", const char * creature = "core:fireElemental", bool unshakable = true)
	{
		useRevisedOrders = true;
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0)
			.heroGarrison({{creatureByName(creature), 50}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1)).heroExperience(0)
			.heroGarrison({{creatureByName("core:pikeman"), 1000}});
		startWithMap(std::move(builder));
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		attackerSideHero = findHeroByOwner(PlayerColor(0));
		defenderSideHero = findHeroByOwner(PlayerColor(1));
		for(auto * actor : {attackerSideHero, defenderSideHero})
		{
			const auto skills = actor->secSkills;
			for(const auto & [skill, rank] : skills) actor->setSecSkillLevel(skill, 0, ChangeValueMode::ABSOLUTE);
		}
		const auto skill = SecondarySkill(SecondarySkill::decode(consecratedArms::SKILL));
		defenderSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		if(unshakable)
			defenderSideHero->applyPerkSelection({consecratedArms::SKILL,
				std::string(consecratedArms::SKILL) + ".unshakableFaith"});
		defenderSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 300, ChangeValueMode::ABSOLUTE);
		defenderSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 300, ChangeValueMode::ABSOLUTE);
		startBattle();
		for(const auto & unit : battle()->stacks)
			if(unit->unitSlot() == SlotID(0))
				(unit->unitSide() == BattleSide::ATTACKER ? source : recipient) = unit.get();
		ASSERT_NE(source, nullptr);
		ASSERT_NE(recipient, nullptr);
		source->setPosition(BattleHex(3, 5));
		recipient->setPosition(BattleHex(4, 5));
		beginCombat();
		auto prayer = consecratedArms::orderEffects(*battle(), BattleSide::DEFENDER,
			{recipient->unitId()}, {consecratedArms::Prayer::STEADFAST_SOUL},
			consecratedArms::capturePolicy(defenderSideHero), consecratedArms::actualFaith(*defenderSideHero));
		gameHandler->sendAndApply(prayer);
	}
	int64_t physicalHit(int64_t amount)
	{
		const auto before = recipient->getAvailableHealth();
		BattleStackAttacked hit;
		hit.attackerID = source->unitId();
		hit.stackAttacked = recipient->unitId();
		hit.damageAmount = amount;
		CStack::prepareAttacked(hit, gameHandler->getRandomGenerator(), recipient->acquireState(),
			false, false, battle::DamageProvenance::PHYSICAL_CREATURE);
		StacksInjured injury;
		injury.battleID = battle()->getBattleID();
		injury.stacks.push_back(hit);
		gameHandler->sendAndApply(injury);
		return before - recipient->getAvailableHealth();
	}
	std::vector<std::byte> randomBytes(CRandomGenerator & generator)
	{
		CMemorySerializer stream;
		stream.oser & generator;
		return stream.extractBuffer();
	}
	bool publish(const newHorizonsOrdinaryStrike::EffectPlan & plan, int seed)
	{
		CRandomGenerator expected(seed);
		const bool resisted = expected.nextInt(9999) < 4000;
		auto & actual = dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator());
		actual.setSeed(seed);
		battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), plan);
		EXPECT_EQ(randomBytes(actual), randomBytes(expected));
		return resisted;
	}
};

TEST_F(ConsecratedArmsBurnMarkTest, MissingCapturedBurnCoefficientProducesNoAffliction)
{
	specialtiesEnabled = false;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto loss = physicalHit(100);
	ASSERT_GT(loss, 0);
	const auto plan = burn::plan(*battle(), BattleAttackInfo(source, recipient, 0, false), loss);
	EXPECT_TRUE(plan.add.empty());
	EXPECT_TRUE(plan.remove.empty());
}

TEST_F(ConsecratedArmsBurnMarkTest, ActualBurnApplicationIsPreventedOnceWithoutUndoingItsPhysicalHit)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int64_t loss = physicalHit(100);
	ASSERT_GT(loss, 0);
	const auto afterHit = recipient->getAvailableHealth();
	const auto plan = burn::plan(*battle(), BattleAttackInfo(source, recipient, 0, false), loss);
	ASSERT_EQ(plan.add.size(), 1);
	const auto & descriptor = plan.add.front().abilityDescriptor;
	ASSERT_TRUE(descriptor);
	ASSERT_TRUE(std::holds_alternative<creatureAbility::Native>(descriptor->identity));
	EXPECT_EQ(std::get<creatureAbility::Native>(descriptor->identity), creatureAbility::Native::ORDINARY_BURN);
	EXPECT_EQ(descriptor->categories, creatureAbility::DAMAGE | creatureAbility::HOSTILE_AFFLICTION);
	EXPECT_TRUE(descriptor->interactions & creatureAbility::PREVENTABLE);
	EXPECT_TRUE(publish(plan, 400));
	EXPECT_FALSE(recipient->hasBonus(CSelector(burn::isBurn)));
	EXPECT_EQ(recipient->getAvailableHealth(), afterHit);
}

TEST_F(ConsecratedArmsBurnMarkTest, AdmittedBurnTicksWithoutAnotherPreventionRoll)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int64_t loss = physicalHit(100);
	const auto plan = burn::plan(*battle(), BattleAttackInfo(source, recipient, 0, false), loss);
	ASSERT_EQ(plan.add.size(), 1);
	EXPECT_FALSE(publish(plan, 30000));
	ASSERT_TRUE(recipient->hasBonus(CSelector(burn::isBurn)));
	const auto tick = burn::activationTick(*battle(), *recipient, BattleUnitTurnReason::TURN_QUEUE);
	ASSERT_GT(tick.damage, 0);
	const auto beforeHealth = recipient->getAvailableHealth();
	auto & random = dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator());
	const auto beforeRandom = randomBytes(random);
	ASSERT_TRUE(battleCreatureBurn::onActivation(*gameHandler, *battle(), *recipient, BattleUnitTurnReason::TURN_QUEUE));
	EXPECT_EQ(randomBytes(random), beforeRandom);
	EXPECT_EQ(recipient->getAvailableHealth(), beforeHealth - tick.damage);
	EXPECT_FALSE(recipient->hasBonus(CSelector(burn::isBurn)));
}

TEST_F(ConsecratedArmsBurnMarkTest, BurnWithoutUnshakableRetainsItsApplicationWithoutAnExtraDraw)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:ignissa", "core:fireElemental", false));
	const int64_t loss = physicalHit(100);
	const auto plan = burn::plan(*battle(), BattleAttackInfo(source, recipient, 0, false), loss);
	ASSERT_EQ(plan.add.size(), 1);
	auto & random = dynamic_cast<CRandomGenerator &>(gameHandler->getRandomGenerator());
	const auto before = randomBytes(random);
	battlePrimaryStrikeAftermath::applyOrdinaryEffect(*gameHandler, *battle(), plan);
	EXPECT_EQ(randomBytes(random), before);
	EXPECT_TRUE(recipient->hasBonus(CSelector(burn::isBurn)));
}

TEST_F(ConsecratedArmsBurnMarkTest, ResistedStrongerBurnPreservesAnExistingDifferentSupplierAndItsSource)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	auto previous = burn::marker(HeroTypeID(HeroTypeID::decode("core:rashka")), 999999, 1);
	previous.abilityDescriptor = creatureAbility::nativeAffliction(creatureAbility::Native::ORDINARY_BURN,
		creatureAbility::Family::CREATURE_ABILITY, PlayerColor(0));
	// The saved source may already be absent. Loading an existing status is not
	// a fresh application and does not repeat its original resistance check.
	recipient->addNewBonus(std::make_shared<Bonus>(previous));
	const int64_t loss = physicalHit(100);
	const auto plan = burn::plan(*battle(), BattleAttackInfo(source, recipient, 0, false), loss);
	ASSERT_EQ(plan.add.size(), 1);
	ASSERT_EQ(plan.remove.size(), 1);
	ASSERT_NE(plan.add.front().sid, previous.sid);
	ASSERT_GT(plan.add.front().val, previous.val);
	EXPECT_TRUE(publish(plan, 400));
	const auto preserved = recipient->getUnstackedBonuses(CSelector(burn::isBurn));
	ASSERT_EQ(preserved->size(), 1);
	EXPECT_EQ(preserved->front()->toJsonNode(), previous.toJsonNode());
}

class ConsecratedArmsCoordinatedMarkTest : public ConsecratedArmsBurnMarkTest,
	public ::testing::WithParamInterface<bool>
{
protected:
	void setupMark()
	{
		prepare(GetParam() ? "core:ingham" : "core:lorelei", GetParam() ? "core:monk" : "core:harpy");
	}
	newHorizonsOrdinaryStrike::EffectPlan mark()
	{
		if(!GetParam()) return marks::completedReturn(*battle(), *source, *recipient, true);
		return newHorizonsOrdinaryStrike::plan(*battle(), BattleAttackInfo(source, recipient, 0, true), 1);
	}
};

TEST_P(ConsecratedArmsCoordinatedMarkTest, MissingCapturedCoefficientProducesNoMark)
{
	specialtiesEnabled = false;
	ASSERT_NO_FATAL_FAILURE(setupMark());
	const auto plan = mark();
	EXPECT_TRUE(plan.add.empty());
	EXPECT_TRUE(plan.remove.empty());
}

TEST_P(ConsecratedArmsCoordinatedMarkTest, RealMarkProducerHasOnePreventionCheckAndLeavesInitialDamageIntact)
{
	ASSERT_NO_FATAL_FAILURE(setupMark());
	ASSERT_GT(physicalHit(100), 0);
	const auto afterHit = recipient->getAvailableHealth();
	const auto plan = mark();
	ASSERT_EQ(plan.add.size(), 1);
	const auto & descriptor = plan.add.front().abilityDescriptor;
	ASSERT_TRUE(descriptor);
	EXPECT_TRUE(descriptor->interactions & creatureAbility::PREVENTABLE);
	EXPECT_EQ(descriptor->sourceOwner, PlayerColor(0));
	EXPECT_TRUE(publish(plan, 400));
	EXPECT_FALSE(marks::hasMark(*recipient));
	EXPECT_EQ(recipient->getAvailableHealth(), afterHit);
}

TEST_P(ConsecratedArmsCoordinatedMarkTest, ResistedReplacementPreservesTheExistingMark)
{
	ASSERT_NO_FATAL_FAILURE(setupMark());
	const auto initial = mark();
	ASSERT_EQ(initial.add.size(), 1);
	EXPECT_FALSE(publish(initial, 30000));
	const auto before = recipient->getUnstackedBonuses(CSelector(marks::isMark));
	ASSERT_EQ(before->size(), 1);
	const auto original = before->front()->toJsonNode();
	const auto replacement = mark();
	ASSERT_EQ(replacement.add.size(), 1);
	EXPECT_TRUE(publish(replacement, 400));
	const auto after = recipient->getUnstackedBonuses(CSelector(marks::isMark));
	ASSERT_EQ(after->size(), 1);
	EXPECT_EQ(after->front()->toJsonNode(), original);
}
INSTANTIATE_TEST_SUITE_P(ConsecratedTargetAndFeatheredFeint, ConsecratedArmsCoordinatedMarkTest, ::testing::Bool());
}
