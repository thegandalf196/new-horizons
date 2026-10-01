/*
 * NewHorizonsFortunesFavorTest.cpp, part of VCMI engine
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
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../server/CGameHandler.h"

#include <algorithm>

namespace
{
constexpr auto luckSkillId = "new-horizons:luck";
constexpr auto fortuneFavorId = "new-horizons:luck.fortuneSFavor";

JsonNode certainLuckChance()
{
	JsonNode result;
	for(int i = 0; i < 10; ++i)
		result.Vector().emplace_back(100);
	return result;
}

class FortunesFavorEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit FortunesFavorEnvironment(std::shared_ptr<CGameState> value)
		: state(std::move(value))
	{}

	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsFortunesFavorTest : public BattleTestFixture
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

	void acceptFortuneFavorThroughNormalOffer(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [](const auto & candidate)
			{
				return candidate.selection.skillId == luckSkillId
					&& candidate.selection.perkId == fortuneFavorId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			EXPECT_TRUE(hero->hasActivePerk(luckSkillId, fortuneFavorId));
			return;
		}

		FAIL() << "Fortune's Favor never appeared in a legal Basic Luck perk offer";
	}

	std::pair<size_t, int> fortuneFavorBonuses() const
	{
		const auto bonuses = attackerSideHero->getBonuses(Selector::source(
			BonusSource::SECONDARY_SKILL, BonusSourceID(luckSkill())));
		size_t count = 0;
		int total = 0;
		for(const auto & bonus : *bonuses)
			if(bonus->type == BonusType::LUCKY_STRIKE_DAMAGE_PERCENTAGE)
			{
				++count;
				total += bonus->val;
			}
		return {count, total};
	}

	void expectSingleFortuneFavorBonus()
	{
		const auto [count, total] = fortuneFavorBonuses();
		EXPECT_EQ(count, 1u);
		EXPECT_EQ(total, 25);
	}

	CStack * attackerAngel()
	{
		return addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	}

	CStack * defenderAngel()
	{
		return addStack(BattleSide::DEFENDER, creatureByName("core:angel"), BattleHex(rightHex), 100);
	}
};
}

TEST_F(NewHorizonsFortunesFavorTest, AcceptedPerkTracksRankAndReconstructionWithoutDuplicates)
{
	ASSERT_EQ(fortuneFavorBonuses(), (std::pair<size_t, int>{0, 0}));

	setLuckRank(MasteryLevel::BASIC);
	EXPECT_EQ(fortuneFavorBonuses(), (std::pair<size_t, int>{0, 0}));
	acceptFortuneFavorThroughNormalOffer(attackerSideHero);
	expectSingleFortuneFavorBonus();

	for(int i = 0; i < 3; ++i)
	{
		attackerSideHero->recreateSecondarySkillsBonuses();
		expectSingleFortuneFavorBonus();
	}

	setLuckRank(MasteryLevel::NONE);
	EXPECT_FALSE(attackerSideHero->hasActivePerk(luckSkillId, fortuneFavorId));
	EXPECT_EQ(fortuneFavorBonuses(), (std::pair<size_t, int>{0, 0}));

	setLuckRank(MasteryLevel::BASIC);
	EXPECT_TRUE(attackerSideHero->hasActivePerk(luckSkillId, fortuneFavorId));
	expectSingleFortuneFavorBonus();
}

TEST_F(NewHorizonsFortunesFavorTest, AddsDamageOnlyToPositiveLuckyStrike)
{
	setLuckRank(MasteryLevel::BASIC);
	acceptFortuneFavorThroughNormalOffer(attackerSideHero);

	const auto * source = attackerAngel();
	const auto * target = defenderAngel();
	BattleAttackInfo ordinary(source, target, 0, false);
	auto lucky = ordinary;
	lucky.luckyStrike = true;
	auto unlucky = ordinary;
	unlucky.unluckyStrike = true;

	EXPECT_EQ(battle()->calculateDmgRange(ordinary).damage.min, 5000);
	EXPECT_EQ(battle()->calculateDmgRange(lucky).damage.min, 11250);
	EXPECT_EQ(battle()->calculateDmgRange(unlucky).damage.min, 2500);
}

TEST_F(NewHorizonsFortunesFavorTest, LiveAndProjectedExpectedDamageUseTheSameLuckyMultiplier)
{
	setLuckRank(MasteryLevel::BASIC);
	acceptFortuneFavorThroughNormalOffer(attackerSideHero);

	const auto * source = attackerAngel();
	const auto * target = defenderAngel();
	const BattleAttackInfo ordinary(source, target, 0, false);
	auto lucky = ordinary;
	lucky.luckyStrike = true;
	const auto expectedStrike = battle()->calculateDmgRange(lucky).damage.min;
	ASSERT_EQ(expectedStrike, 11250);
	EXPECT_EQ(battle()->battleExpectedLuckDamage(ordinary), expectedStrike);

	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	FortunesFavorEnvironment environment(gameState());
	HypotheticBattle projected(&environment, callback);
	auto projectedSource = projected.getForUpdate(source->unitId());
	auto projectedTarget = projected.getForUpdate(target->unitId());
	ASSERT_NE(static_cast<const battle::Unit *>(projectedSource.get()), static_cast<const battle::Unit *>(source));
	ASSERT_NE(static_cast<const battle::Unit *>(projectedTarget.get()), static_cast<const battle::Unit *>(target));
	const BattleAttackInfo projectedAttack(projectedSource.get(), projectedTarget.get(), 0, false);
	EXPECT_EQ(projected.battleExpectedLuckDamage(projectedAttack), expectedStrike);
}
