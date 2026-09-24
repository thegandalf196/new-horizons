/*
 * SpellPointRewardTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/BattleTestFixture.h"

#include "../../lib/GameLibrary.h"
#include "../../lib/IGameSettings.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/mapObjects/CRewardableObject.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForClient.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../server/CGameHandler.h"

namespace
{
class SpellPointRewardObject : public CRewardableObject
{
public:
	using CRewardableObject::CRewardableObject;

	void applyReward(IGameEventCallback & gameEvents, const Rewardable::VisitInfo & info, const CGHeroInstance * hero) const
	{
		grantRewardAfterLevelup(gameEvents, info, hero, hero);
	}
};

class SpellPointRewardTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module for its saved spell-point rules";

		startGame();
		ASSERT_TRUE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
		setKnowledge(50);
		ASSERT_EQ(attackerSideHero->manaLimit(), 50);
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);

		JsonNode magicRules(JsonPath::builtin("config/newHorizonsMagic"));
		newHorizonsMagic::validateRules(magicRules);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magicRules);
	}

	void setKnowledge(int32_t value)
	{
		SetPrimarySkill change;
		change.id = attackerSideHero->id;
		change.which = PrimarySkill::KNOWLEDGE;
		change.val = value;
		change.mode = ChangeValueMode::ABSOLUTE;
		gameState()->apply(change);
	}

	void setPools(int32_t normal, int32_t buffer)
	{
		attackerSideHero->initializeSpellPoints(normal, buffer);
	}

	void grant(const Rewardable::Reward & reward)
	{
		Rewardable::VisitInfo info;
		info.reward = reward;
		SpellPointRewardObject object(gameState().get());
		object.applyReward(*gameHandler, info, attackerSideHero);
	}
};

class LegacySpellPointRewardTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		startGame();
		ASSERT_FALSE(newHorizonsMagic::spellPointRulesActive(attackerSideHero->getMagicRules()));
	}

	void mapLoaded(CMap * loaded) override
	{
		BattleTestFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
	}

	void grant(const Rewardable::Reward & reward)
	{
		Rewardable::VisitInfo info;
		info.reward = reward;
		SpellPointRewardObject object(gameState().get());
		object.applyReward(*gameHandler, info, attackerSideHero);
	}
};
}

TEST_F(SpellPointRewardTest, BufferRewardLeavesNormalUntouched)
{
	setPools(30, 0);

	Rewardable::Reward reward;
	reward.manaBuffer = 50;
	grant(reward);

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 30);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 50);
}

TEST_F(SpellPointRewardTest, FullOrdinaryRefillPreservesExistingBuffer)
{
	setPools(30, 50);

	Rewardable::Reward reward;
	reward.manaPercentage = 100;
	grant(reward);

	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 50);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 50);
}

TEST_F(SpellPointRewardTest, SpringRefillAndBufferGrantCommuteWithOrdinaryRefill)
{
	Rewardable::Reward ordinaryRefill;
	ordinaryRefill.manaPercentage = 100;
	Rewardable::Reward springRefill;
	springRefill.manaPercentage = 100;
	springRefill.manaBuffer = 25;

	setPools(30, 0);
	grant(springRefill);
	grant(ordinaryRefill);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 50);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 25);

	setPools(30, 0);
	grant(ordinaryRefill);
	grant(springRefill);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 50);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 25);
}

TEST_F(SpellPointRewardTest, LegacyPercentAndOverflowFieldsCannotExceedNormalCapacity)
{
	setPools(30, 13);
	Rewardable::Reward percentage;
	percentage.manaPercentage = 400;
	percentage.manaDiff = 10;
	percentage.manaOverflowFactor = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(percentage.calculateManaPoints(attackerSideHero), 50);
	grant(percentage);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 50);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);

	setPools(30, 13);
	Rewardable::Reward overflow;
	overflow.manaDiff = std::numeric_limits<int32_t>::max();
	overflow.manaOverflowFactor = std::numeric_limits<int32_t>::max();
	EXPECT_EQ(overflow.calculateManaPoints(attackerSideHero), 50);
	grant(overflow);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 50);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 13);
}

TEST_F(SpellPointRewardTest, NegativeFixedManaCostConsumesBufferBeforeNormalAndDisplaysTotalDelta)
{
	setPools(30, 50);
	Rewardable::Reward cost;
	cost.manaDiff = -10;
	EXPECT_EQ(cost.calculateManaPoints(attackerSideHero), 30);

	std::vector<Component> components;
	cost.loadComponents(components, attackerSideHero);
	const auto mana = std::find_if(components.begin(), components.end(), [](const Component & component)
	{
		return component.type == ComponentType::MANA;
	});
	ASSERT_NE(mana, components.end());
	ASSERT_TRUE(mana->value.has_value());
	EXPECT_EQ(*mana->value, -10);

	grant(cost);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 30);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 40);

	setPools(30, 5);
	EXPECT_EQ(cost.calculateManaPoints(attackerSideHero), 25);
	grant(cost);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 25);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 0);
}

TEST_F(SpellPointRewardTest, NegativeFixedCostAppliesAfterCappedPercentageAndSaturatesSafely)
{
	setPools(30, 50);
	Rewardable::Reward percentageThenCost;
	percentageThenCost.manaPercentage = 100;
	percentageThenCost.manaDiff = -10;
	EXPECT_EQ(percentageThenCost.calculateManaPoints(attackerSideHero), 50);
	grant(percentageThenCost);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 50);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 40);

	setPools(30, 5);
	Rewardable::Reward unaffordableCost;
	unaffordableCost.manaDiff = std::numeric_limits<int32_t>::min();
	EXPECT_EQ(unaffordableCost.calculateManaPoints(attackerSideHero), 0);
	std::vector<Component> components;
	unaffordableCost.loadComponents(components, attackerSideHero);
	const auto mana = std::find_if(components.begin(), components.end(), [](const Component & component)
	{
		return component.type == ComponentType::MANA;
	});
	ASSERT_NE(mana, components.end());
	ASSERT_TRUE(mana->value.has_value());
	EXPECT_EQ(*mana->value, -35);
	grant(unaffordableCost);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 0);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 0);
}

TEST_F(SpellPointRewardTest, NegativeCostAndNewBufferGrantApplyIndependentlyInOneReward)
{
	setPools(30, 5);
	Rewardable::Reward reward;
	reward.manaDiff = -10;
	reward.manaBuffer = 25;
	EXPECT_EQ(reward.calculateManaPoints(attackerSideHero), 25);

	std::vector<Component> components;
	reward.loadComponents(components, attackerSideHero);
	const auto mana = std::find_if(components.begin(), components.end(), [](const Component & component)
	{
		return component.type == ComponentType::MANA;
	});
	ASSERT_NE(mana, components.end());
	ASSERT_TRUE(mana->value.has_value());
	EXPECT_EQ(*mana->value, 15);

	grant(reward);
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 25);
	EXPECT_EQ(attackerSideHero->getBufferSpellPoints(), 25);
}

TEST_F(LegacySpellPointRewardTest, NegativeManaRewardUsesSaturatingLegacyCallback)
{
	attackerSideHero->setNormalSpellPoints(5);
	Rewardable::Reward cost;
	cost.manaDiff = -10;
	EXPECT_EQ(cost.calculateManaPoints(attackerSideHero), -5);

	EXPECT_NO_THROW(grant(cost));
	EXPECT_EQ(attackerSideHero->getNormalSpellPoints(), 0);
}
