/*
 * NewHorizonsLuckyAimTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"

#include "BattleTestFixture.h"

#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto luckyAimId = "new-horizons:luck.luckyAim";

JsonNode certainLuckChance()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(100);
	return result;
}

class LuckyAimEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit LuckyAimEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsLuckyAimTest : public BattleTestFixture
{
protected:
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
		startGame();
		startBattle();
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		loaded->overrideGameSetting(EGameSettings::COMBAT_GOOD_LUCK_CHANCE, certainLuckChance());
		loaded->overrideGameSetting(EGameSettings::COMBAT_LUCK_DICE_SIZE, JsonNode(100));
	}

	SecondarySkill luckSkill() const
	{
		const int decoded = SecondarySkill::decode(luckSkillId);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void setLuckRank(int rank)
	{
		attackerSideHero->setSecSkillLevel(luckSkill(), rank, ChangeValueMode::ABSOLUTE);
	}

	void selectLuckyAim()
	{
		setLuckRank(MasteryLevel::BASIC);
		attackerSideHero->applyPerkSelection({luckSkillId, luckyAimId});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(luckSkillId, luckyAimId));
	}

	void expectSameDamage(const DamageRange & actual, const DamageRange & expected)
	{
		EXPECT_EQ(actual.min, expected.min);
		EXPECT_EQ(actual.max, expected.max);
	}

	CStack * attackerTitan()
	{
		return addStack(BattleSide::ATTACKER, creatureByName("core:titan"), BattleHex(leftHex), 100);
	}

	CStack * defenderArchangel()
	{
		return addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(leftHex + 3), 100);
	}
};
}

TEST_F(NewHorizonsLuckyAimTest, OnlyPositivePhysicalLuckyShotsIgnoreTargetCreatureDefense)
{
	setLuckRank(MasteryLevel::BASIC);
	ASSERT_FALSE(attackerSideHero->hasActivePerk(luckSkillId, luckyAimId));

	auto * source = attackerTitan();
	auto * target = defenderArchangel();
	BattleAttackInfo ordinaryRanged(source, target, 0, true);
	auto luckyRanged = ordinaryRanged;
	luckyRanged.luckyStrike = true;
	auto unluckyRanged = ordinaryRanged;
	unluckyRanged.unluckyStrike = true;
	auto luckyMelee = BattleAttackInfo(source, target, 0, false);
	luckyMelee.luckyStrike = true;
	auto nonPhysicalLuckyRanged = luckyRanged;
	nonPhysicalLuckyRanged.physicalDamage = false;

	const auto ordinaryBefore = battle()->calculateDmgRange(ordinaryRanged).damage;
	const auto luckyBefore = battle()->calculateDmgRange(luckyRanged).damage;
	const auto unluckyBefore = battle()->calculateDmgRange(unluckyRanged).damage;
	const auto meleeBefore = battle()->calculateDmgRange(luckyMelee).damage;
	const auto nonPhysicalBefore = battle()->calculateDmgRange(nonPhysicalLuckyRanged).damage;
	EXPECT_EQ(target->getDefense(true), 30);
	EXPECT_EQ(source->getAttack(true), 24);
	EXPECT_DOUBLE_EQ(LIBRARY->engineSettings()->getDouble(EGameSettings::COMBAT_DEFENSE_POINT_DAMAGE_FACTOR), 0.025);

	selectLuckyAim();
	const auto ordinaryAfter = battle()->calculateDmgRange(ordinaryRanged).damage;
	const auto luckyAfter = battle()->calculateDmgRange(luckyRanged).damage;
	const auto unluckyAfter = battle()->calculateDmgRange(unluckyRanged).damage;
	const auto meleeAfter = battle()->calculateDmgRange(luckyMelee).damage;
	const auto nonPhysicalAfter = battle()->calculateDmgRange(nonPhysicalLuckyRanged).damage;

	// Archangel has 30 Creature Defense; Lucky Aim ignores floor(30 * 25%) = 7,
	// moving the Titan from six points below its target to one point above it.
	// Negative Defense factors multiply Lucky damage: 2 * (1 - 6 * .025).
	// Positive Attack factors add: 1 + 1 + 1 * .05. Lua's integer truncation
	// can put the fractional maximum one HP below its mathematical value.
	EXPECT_EQ(luckyAfter.min, 8200);
	EXPECT_GE(luckyAfter.max, 12299);
	EXPECT_LE(luckyAfter.max, 12300);
	EXPECT_EQ(luckyBefore.min, 6800);
	EXPECT_EQ(luckyBefore.max, 10200);
	expectSameDamage(ordinaryAfter, ordinaryBefore);
	expectSameDamage(unluckyAfter, unluckyBefore);
	expectSameDamage(meleeAfter, meleeBefore);
	expectSameDamage(nonPhysicalAfter, nonPhysicalBefore);
}

TEST_F(NewHorizonsLuckyAimTest, LosingLuckRankDisablesAndRegainingItRestoresLuckyAim)
{
	selectLuckyAim();
	auto * source = attackerTitan();
	auto * target = defenderArchangel();
	BattleAttackInfo luckyRanged(source, target, 0, true);
	luckyRanged.luckyStrike = true;
	const auto activeDamage = battle()->calculateDmgRange(luckyRanged).damage;

	setLuckRank(MasteryLevel::NONE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(luckSkillId, luckyAimId));
	const auto inactiveDamage = battle()->calculateDmgRange(luckyRanged).damage;
	EXPECT_EQ(inactiveDamage.min, 6800);
	EXPECT_EQ(inactiveDamage.max, 10200);

	setLuckRank(MasteryLevel::BASIC);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(luckSkillId, luckyAimId));
	expectSameDamage(battle()->calculateDmgRange(luckyRanged).damage, activeDamage);
}

TEST_F(NewHorizonsLuckyAimTest, LiveAndDetachedAIPredictionUseTheSameLuckyAimDefenseIgnore)
{
	selectLuckyAim();
	auto * source = attackerTitan();
	auto * target = defenderArchangel();
	const BattleAttackInfo ordinaryRanged(source, target, 0, true);
	const auto expected = battle()->battleExpectedLuckDamage(ordinaryRanged);
	ASSERT_GE(expected, 10249);
	ASSERT_LE(expected, 10250);

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	LuckyAimEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	const auto projectedSource = projected.getForUpdate(source->unitId());
	const auto projectedTarget = projected.getForUpdate(target->unitId());
	ASSERT_NE(projectedSource, nullptr);
	ASSERT_NE(projectedTarget, nullptr);
	const BattleAttackInfo projectedRanged(projectedSource.get(), projectedTarget.get(), 0, true);
	EXPECT_EQ(projected.battleExpectedLuckDamage(projectedRanged), expected);
}
