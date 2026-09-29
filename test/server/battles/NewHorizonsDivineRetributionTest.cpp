/*
 * NewHorizonsDivineRetributionTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "BattleTestFixture.h"
#include "../../SpellPointTestUtils.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/modding/ModScope.h"
#include "../../../lib/networkPacks/SetStackEffect.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/spells/NewHorizonsMagic.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto divineRetributionKey = "new-horizons:divineRetribution";
constexpr auto lightMagicSkill = "new-horizons:lightMagic";
constexpr auto healerPerk = "new-horizons:lightMagic.healer";
constexpr auto retributionistPerk = "new-horizons:lightMagic.retributionist";

SpellID divineRetributionSpell()
{
	return SpellID(SpellID::decode(divineRetributionKey));
}
}

class NewHorizonsDivineRetributionTest : public BattleTestFixture
{
protected:
	CStack * friendlyAnchor = nullptr;
	CStack * protectedStack = nullptr;
	CStack * enemyAttacker = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires separate native curated preset";
	}

	void mapLoaded(CMap * map) override
	{
		BattleTestFixture::mapLoaded(map);
		map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsMagic")));
	}

	void prepare(int32_t attackerCount = 4, int32_t protectedCount = 1000, int32_t spellPower = 100)
	{
		startGame();
		const auto spell = divineRetributionSpell();
		ASSERT_NE(spell, SpellID::NONE);
		giveArtifact(attackerSideHero, ArtifactID::SPELLBOOK, ArtifactPosition::SPELLBOOK);
		attackerSideHero->addSpellToSpellbook(spell);
		attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, spellPower, ChangeValueMode::ABSOLUTE);
		attackerSideHero->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
		setTestSpellPointTotal(attackerSideHero, 1000);

		startBattle();
		removeDeployedUnits();
		friendlyAnchor = addStack(BattleSide::ATTACKER, creatureByName("core:phoenix"), BattleHex(3, 5), 1);
		protectedStack = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), protectedCount);
		enemyAttacker = addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), attackerCount);
		blockRetaliation(enemyAttacker);
		forceMaximumDamage(enemyAttacker);
		beginCombat();

		ASSERT_NE(friendlyAnchor, nullptr);
		ASSERT_NE(protectedStack, nullptr);
		ASSERT_NE(enemyAttacker, nullptr);
		ASSERT_NE(battle()->battleActiveUnit(), nullptr);
		ASSERT_EQ(battle()->battleActiveUnit()->unitSide(), BattleSide::ATTACKER)
			<< "The fast friendly Phoenix keeps the casting side active before the Angel stack";
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		gameHandler->sendAndApply(remove);
	}

	bool castRetribution()
	{
		BattleAction action;
		action.actionType = EActionType::HERO_SPELL;
		action.side = BattleSide::ATTACKER;
		action.spell = divineRetributionSpell();
		action.aimToUnit(protectedStack);
		return gameHandler->battles->makePlayerBattleAction(BattleID(0), PlayerColor(0), action);
	}

	TConstBonusListPtr protectionBonuses() const
	{
		return protectedStack->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(divineRetributionSpell())).And(Selector::type()(BonusType::DIVINE_RETRIBUTION)));
	}

	TConstBonusListPtr judgmentBonuses() const
	{
		const auto protectedSubtype = BonusSubtypeID(
			BonusCustomSubtype(static_cast<int32_t>(protectedStack->unitId())));
		return enemyAttacker->getBonuses(Selector::source(BonusSource::SPELL_EFFECT,
			BonusSourceID(divineRetributionSpell())).And(Selector::typeSubtype(
				BonusType::DIVINE_RETRIBUTION_JUDGED, protectedSubtype)));
	}

	void selectAdvancedRetributionist()
	{
		const auto light = SecondarySkill(SecondarySkill::decode(lightMagicSkill));
		ASSERT_TRUE(light.hasValue());
		attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, healerPerk});
		attackerSideHero->setSecSkillLevel(light, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		attackerSideHero->applyPerkSelection({lightMagicSkill, retributionistPerk});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(lightMagicSkill, retributionistPerk));
	}
};

TEST_F(NewHorizonsDivineRetributionTest, UnrankedCastStoresTheBaseCapAndTwoRoundDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castRetribution());
	const auto applied = protectionBonuses();
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(applied->front()->val, 150);
	EXPECT_EQ(applied->front()->turnsRemain, 2);
	EXPECT_EQ(applied->front()->source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(applied->front()->sid.as<SpellID>(), divineRetributionSpell());
	ASSERT_NE(applied->front()->parameters, nullptr);
	const auto unrankedParameters = applied->front()->parameters->toCustom<JsonNode>();
	EXPECT_EQ(unrankedParameters["retributionistPercent"].Integer(), 100);
}

TEST_F(NewHorizonsDivineRetributionTest, BasicLightScalesOnlyTheSpellPowerTerm)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto light = SecondarySkill(SecondarySkill::decode(lightMagicSkill));
	ASSERT_TRUE(light.hasValue());
	attackerSideHero->setSecSkillLevel(light, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castRetribution());
	const auto applied = protectionBonuses();
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(applied->front()->val, 168);
	EXPECT_EQ(applied->front()->turnsRemain, 2);
	EXPECT_EQ(applied->front()->parameters->toCustom<JsonNode>()["retributionistPercent"].Integer(), 100);
}

TEST_F(NewHorizonsDivineRetributionTest, AdvancedRetributionistIsSnapshottedSeparatelyFromTheRawCap)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	selectAdvancedRetributionist();
	ASSERT_TRUE(castRetribution());
	const auto applied = protectionBonuses();
	ASSERT_EQ(applied->size(), 1u);
	EXPECT_EQ(applied->front()->val, 187);
	const auto perkParameters = applied->front()->parameters->toCustom<JsonNode>();
	EXPECT_EQ(perkParameters["retributionistPercent"].Integer(), 120);
}

TEST_F(NewHorizonsDivineRetributionTest, RecastReplacesThePreviousCapAndRefreshesDuration)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_TRUE(castRetribution());
	ASSERT_EQ(protectionBonuses()->size(), 1u);
	EXPECT_EQ(protectionBonuses()->front()->val, 150);

	endRound();
	attackerSideHero->setPrimarySkill(PrimarySkill::SPELL_POWER, 200, ChangeValueMode::ABSOLUTE);
	ASSERT_TRUE(castRetribution());
	const auto refreshed = protectionBonuses();
	ASSERT_EQ(refreshed->size(), 1u);
	EXPECT_EQ(refreshed->front()->val, 275);
	EXPECT_EQ(refreshed->front()->turnsRemain, 2);
}

TEST_F(NewHorizonsDivineRetributionTest, AccumulatesActualMeleeHpLossAndPaysSixtyFromTwoHundredEachRound)
{
	ASSERT_NO_FATAL_FAILURE(prepare(4));
	ASSERT_TRUE(castRetribution());
	const auto expectedAttackDamage = int64_t{200};
	const auto expectedPayout = int64_t{60};
	auto healthBefore = protectedStack->getAvailableHealth();
	ASSERT_EQ(protectionBonuses()->size(), 1u);
	ASSERT_TRUE(attack(enemyAttacker, protectedStack->getPosition()));
	EXPECT_EQ(healthBefore - protectedStack->getAvailableHealth(), expectedAttackDamage);
	EXPECT_EQ(protectionBonuses()->size(), 1u);
	EXPECT_TRUE(enemyAttacker->hasBonusOfType(BonusType::DIVINE_RETRIBUTION_JUDGED));

	auto judgments = judgmentBonuses();
	ASSERT_EQ(judgments->size(), 1u);
	EXPECT_EQ(judgments->front()->source, BonusSource::SPELL_EFFECT);
	EXPECT_EQ(judgments->front()->sid.as<SpellID>(), divineRetributionSpell());
	EXPECT_EQ(judgments->front()->subtype, BonusSubtypeID(
		BonusCustomSubtype(static_cast<int32_t>(protectedStack->unitId()))));
	ASSERT_NE(judgments->front()->parameters, nullptr);
	const auto savedJudgment = judgments->front()->parameters->toCustom<JsonNode>();
	EXPECT_EQ(savedJudgment["round"].Integer(), battle()->getRound());
	EXPECT_EQ(savedJudgment["protectedUnitId"].Integer(), protectedStack->unitId());
	EXPECT_EQ(savedJudgment["actualHpDamage"].Integer(), expectedAttackDamage);
	EXPECT_EQ(savedJudgment["rawCap"].Integer(), 150);
	EXPECT_EQ(savedJudgment["retributionistPercent"].Integer(), 100);

	CMemorySerializer wire;
	wire.oser.version = ESerializationVersion::CURRENT;
	wire.iser.version = ESerializationVersion::CURRENT;
	Bonus saved(*judgments->front());
	wire.oser & saved;
	Bonus restored;
	wire.iser & restored;
	ASSERT_NE(restored.parameters, nullptr);
	EXPECT_EQ(restored.parameters->toCustom<JsonNode>(), savedJudgment);

	const auto enemyHealthBeforeFirstPayout = enemyAttacker->getAvailableHealth();
	endRound();
	EXPECT_EQ(enemyHealthBeforeFirstPayout - enemyAttacker->getAvailableHealth(), expectedPayout);
	EXPECT_TRUE(judgmentBonuses()->empty());
	ASSERT_EQ(protectionBonuses()->size(), 1u);
	EXPECT_EQ(protectionBonuses()->front()->turnsRemain, 1);
	EXPECT_TRUE(std::ranges::any_of(server.battleLogLines, [](const std::string & line)
	{
		return line.find("Divine Retribution deals 60 Holy damage") != std::string::npos;
	}));

	healthBefore = protectedStack->getAvailableHealth();
	ASSERT_TRUE(attack(enemyAttacker, protectedStack->getPosition()));
	const auto secondDamage = healthBefore - protectedStack->getAvailableHealth();
	EXPECT_GT(secondDamage, 0);
	const auto enemyHealthBeforeSecondPayout = enemyAttacker->getAvailableHealth();
	endRound();
	EXPECT_EQ(enemyHealthBeforeSecondPayout - enemyAttacker->getAvailableHealth(),
		std::min<int64_t>(secondDamage * 30 / 100, 150));
	EXPECT_TRUE(protectionBonuses()->empty());
	EXPECT_TRUE(judgmentBonuses()->empty());
}

TEST_F(NewHorizonsDivineRetributionTest, CapsEightHundredActualDamageAtOneHundredFifty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(16));
	ASSERT_TRUE(castRetribution());
	const auto healthBefore = protectedStack->getAvailableHealth();
	ASSERT_TRUE(attack(enemyAttacker, protectedStack->getPosition()));
	EXPECT_EQ(healthBefore - protectedStack->getAvailableHealth(), 800);

	const auto judgments = judgmentBonuses();
	ASSERT_EQ(judgments->size(), 1u);
	const auto savedJudgment = judgments->front()->parameters->toCustom<JsonNode>();
	EXPECT_EQ(savedJudgment["actualHpDamage"].Integer(), 800);
	EXPECT_EQ(savedJudgment["rawCap"].Integer(), 150);

	const auto enemyHealthBefore = enemyAttacker->getAvailableHealth();
	endRound();
	EXPECT_EQ(enemyHealthBefore - enemyAttacker->getAvailableHealth(), 150);
}

TEST_F(NewHorizonsDivineRetributionTest, RetributionistScalesTheFinalCappedPayout)
{
	ASSERT_NO_FATAL_FAILURE(prepare(16));
	selectAdvancedRetributionist();
	ASSERT_TRUE(castRetribution());
	ASSERT_EQ(protectionBonuses()->front()->val, 187);

	const auto healthBefore = protectedStack->getAvailableHealth();
	ASSERT_TRUE(attack(enemyAttacker, protectedStack->getPosition()));
	EXPECT_EQ(healthBefore - protectedStack->getAvailableHealth(), 800);

	const auto enemyHealthBefore = enemyAttacker->getAvailableHealth();
	endRound();
	EXPECT_EQ(enemyHealthBefore - enemyAttacker->getAvailableHealth(), 224)
		<< "The 120% perk applies after the 187 raw cap is reached";
}

TEST_F(NewHorizonsDivineRetributionTest, PendingJudgmentPaysAfterTheProtectedStackDies)
{
	ASSERT_NO_FATAL_FAILURE(prepare(16, 1));
	ASSERT_TRUE(castRetribution());
	ASSERT_TRUE(protectedStack->alive());

	ASSERT_TRUE(attack(enemyAttacker, protectedStack->getPosition()));
	EXPECT_FALSE(protectedStack->alive());
	ASSERT_EQ(judgmentBonuses()->size(), 1u);
	const auto savedJudgment = judgmentBonuses()->front()->parameters->toCustom<JsonNode>();
	EXPECT_EQ(savedJudgment["actualHpDamage"].Integer(), 200);

	const auto enemyHealthBefore = enemyAttacker->getAvailableHealth();
	endRound();
	EXPECT_EQ(enemyHealthBefore - enemyAttacker->getAvailableHealth(), 60);
	EXPECT_TRUE(protectionBonuses()->empty());
}
