/*
 * NewHorizonsAvatarOfRageTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "../../../server/CGameHandler.h"
#include "../../../lib/battle/NewHorizonsBloodrage.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../AI/BattleAI/StackWithBonuses.h"

namespace
{
constexpr std::string_view SKILL = "new-horizons:bloodrage";
constexpr std::string_view AVATAR = "new-horizons:bloodrage.avatarOfRage";
class AvatarEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit AvatarEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsAvatarOfRageTest : public BattleTestFixture
{
protected:
	CStack * source = nullptr;
	CStack * target = nullptr;
	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}
	void select(std::string_view id)
	{
		const auto rankLookup = [this](const std::string & skill)
		{ return attackerSideHero->getPerkSkillRank(skill); };
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = attackerSideHero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t choice = 0; choice < offers.size(); ++choice)
				if(offers[choice].selection.skillId == SKILL && offers[choice].selection.perkId == id)
				{
					gameHandler->levelUpHero(attackerSideHero, offers, choice, seed, false);
					ASSERT_TRUE(attackerSideHero->hasActivePerk(std::string(SKILL), std::string(id)));
					return;
				}
		}
		FAIL() << "Missing legal active perk offer " << id;
	}
	void prepare(const std::string & attackerCreature = "core:archangel")
	{
		startGame();
		const SecondarySkill skill(SecondarySkill::decode(std::string(SKILL)));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select("new-horizons:bloodrage.bloodScent"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_NO_FATAL_FAILURE(select("new-horizons:bloodrage.unrelenting"));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		startBattle();
		source = addStack(BattleSide::ATTACKER, creatureByName(attackerCreature), BattleHex(leftHex), 100);
		target = addStack(BattleSide::DEFENDER, creatureByName("core:archangel"), BattleHex(rightHex), 100);
		battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 60;
		ASSERT_EQ(battle()->getBloodrageCapPercent(BattleSide::ATTACKER), 60);
		ASSERT_FALSE(newHorizonsBloodrage::hasAvatarOfRage(attackerSideHero));
	}
};
}

TEST(NewHorizonsAvatarOfRageRulesTest, MaximumUsesCapturedCapIncludingExtendedCap)
{
	EXPECT_FALSE(newHorizonsBloodrage::atMaximumForAttack(48, 60));
	EXPECT_TRUE(newHorizonsBloodrage::atMaximumForAttack(60, 60));
	EXPECT_FALSE(newHorizonsBloodrage::atMaximumForAttack(60, 80));
	EXPECT_TRUE(newHorizonsBloodrage::atMaximumForAttack(80, 80));
	EXPECT_FALSE(newHorizonsBloodrage::atMaximumForAttack(0, 0));
}

TEST_F(NewHorizonsAvatarOfRageTest, SelectedMaximumMeleeOnlyAndNoStoredRageMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleAttackInfo attack(source, target, 0, false);
	const auto inactiveMax = battle()->calculateDmgRange(attack).damage;
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 48;
	const auto inactiveBelow = battle()->calculateDmgRange(attack).damage;
	ASSERT_NO_FATAL_FAILURE(select(AVATAR));
	EXPECT_EQ(battle()->calculateDmgRange(attack).damage.min, inactiveBelow.min);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 48);
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 60;
	EXPECT_GT(battle()->calculateDmgRange(attack).damage.min, inactiveMax.min);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 60);
}

TEST_F(NewHorizonsAvatarOfRageTest, MaximumPhysicalRangedAttack)
{
	ASSERT_NO_FATAL_FAILURE(prepare("core:marksman"));
	BattleAttackInfo attack(source, target, 0, true);
	const auto inactive = battle()->calculateDmgRange(attack).damage;
	ASSERT_NO_FATAL_FAILURE(select(AVATAR));
	EXPECT_GT(battle()->calculateDmgRange(attack).damage.min, inactive.min);
}

TEST_F(NewHorizonsAvatarOfRageTest, MaximumRetaliationAndNonphysicalNegative)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	BattleAttackInfo attack(source, target, 0, false);
	attack.retaliation = true;
	const auto inactive = battle()->calculateDmgRange(attack).damage;
	attack.physicalDamage = false;
	const auto magical = battle()->calculateDmgRange(attack).damage;
	ASSERT_NO_FATAL_FAILURE(select(AVATAR));
	EXPECT_EQ(battle()->calculateDmgRange(attack).damage.min, magical.min);
	attack.physicalDamage = true;
	EXPECT_GT(battle()->calculateDmgRange(attack).damage.min, inactive.min);
}

TEST_F(NewHorizonsAvatarOfRageTest, BloodScentQualifiesOnlyThisAttackAtMaximum)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	battle()->getSide(BattleSide::ATTACKER).bloodrageDamagePercent = 48;
	int64_t damage = target->getAvailableHealth() * 3 / 5;
	target->damage(damage);
	BattleAttackInfo attack(source, target, 0, false);
	ASSERT_EQ(battle()->battleGetBloodrageDamagePercent(source, target), 60);
	const auto inactive = battle()->calculateDmgRange(attack).damage;
	ASSERT_NO_FATAL_FAILURE(select(AVATAR));
	EXPECT_GT(battle()->calculateDmgRange(attack).damage.min, inactive.min);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 48);
	EXPECT_EQ(source->getPersonalBloodrageIncrement(), 0);
}

TEST_F(NewHorizonsAvatarOfRageTest, DetachedPredictionMatchesLiveWithoutChangingCounters)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_NO_FATAL_FAILURE(select(AVATAR));
	const auto live = battle()->calculateDmgRange(BattleAttackInfo(source, target, 0, false)).damage;
	AvatarEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor(0));
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	const auto projectedSource = model->getForUpdate(source->unitId());
	const auto projectedTarget = model->getForUpdate(target->unitId());
	const auto projected = model->calculateDmgRange(BattleAttackInfo(projectedSource.get(), projectedTarget.get(), 0, false)).damage;
	EXPECT_EQ(projected.min, live.min);
	EXPECT_EQ(projected.max, live.max);
	EXPECT_EQ(battle()->getBloodrageDamagePercent(BattleSide::ATTACKER), 60);
}
