/*
 * NewHorizonsTombGuardTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "BattleTestFixture.h"
#include "HistoricalNecromancy087Fixture.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancy.h"
#include "../../../lib/entities/hero/NewHorizonsNecromancyPolicy.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/battle/NewHorizonsNecromancyCombat.h"
#include "../../../lib/battle/ProjectedUnitEnvironmentState.h"
#include "../../../lib/bonuses/BonusList.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <tuple>
#include <limits>
#if ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/HypotheticBattle.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#include "../../../lib/callback/CBattleCallback.h"
#endif

namespace
{
#if ENABLE_BATTLE_AI
class TombGuardEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit TombGuardEnvironment(std::shared_ptr<CGameState> state) : state(std::move(state)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
#endif
// 0: pre-policy historical meaning; 1: 0.87 retaliation policy; 2: new Double Strike.
class NewHorizonsTombGuardTest : public BattleTestFixture,
	public ::testing::WithParamInterface<std::tuple<int, bool, bool>>
{
protected:
	bool legacyMagic = false;
	int captureVersion() const { return std::get<0>(GetParam()); }

	void mapLoaded(CMap * map) override
	{
		TinyMapGameTest::mapLoaded(map);
		if(legacyMagic)
			map->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS,
			JsonNode(JsonPath::builtin("config/newHorizonsHeroes")));
		auto perks = necromancy087Test::rules();
		if(captureVersion() == 2)
		{
			auto & policy = perks[newHorizonsNecromancy::POLICY_KEY];
			policy["version"].Integer() = 2;
			policy["bonewardMagicalReductionPercent"].Integer() = 0;
			policy["tombGuardAdditionalRetaliations"].Integer() = 0;
			policy["graveKnowledgeRecoveryPercent"].Integer() = 50;
			policy["bonewardMagicResistancePercent"].Integer() = 50;
			policy["tombGuardDoubleStrike"].Bool() = true;
			policy["deadMansLuckBonus"].Integer() = 3;
			policy["boneCollectorRaiseBonusPercent"].Integer() = 50;
			policy["soulHarvesterManaPerRaisedCreature"].Integer() = 1;
			policy["soulHarvesterManaCap"].Integer() = 50;
			// An explicit complete 4/4/2 mechanics fixture, independent of the
			// installed defaults. Historical rows otherwise retain exact old data.
			for(auto & row : perks["skills"][newHorizonsNecromancy::SKILL_ID]["perks"].Vector())
			{
				const auto & id = row["id"].String();
				if(id == newHorizonsNecromancy::GRAVE_KNOWLEDGE_ID
					|| id == newHorizonsNecromancy::CORPSE_PRESERVATION_ID)
					row["requires"].String() = "basic";
				if(id == newHorizonsNecromancy::MASTER_OF_BONES_ID
					|| id == newHorizonsNecromancy::BONE_COLLECTOR_ID)
					row["requires"].String() = "advanced";
				if(id == newHorizonsNecromancy::SOUL_HARVESTER_ID)
					row["requires"].String() = "expert";
			}
		}
		if(captureVersion() == 0)
			perks.Struct().erase(newHorizonsNecromancy::POLICY_KEY);
		perks.setOverrideFlag(true);
		map->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
	}

	void prepare()
	{
		startGame();
		attackerSideHero->setHeroType(HeroTypeID(HeroTypeID::decode("core:isra")));
		const auto skill = SecondarySkill(SecondarySkill::decode(newHorizonsNecromancy::SKILL_ID));
		attackerSideHero->setSecSkillLevel(skill, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		const auto & rules = attackerSideHero->getPerkState().rules;
		const auto captured = newHorizonsNecromancy::policy(rules);
		ASSERT_EQ(captured.has_value(), captureVersion() != 0);
		if(captured)
		{
			ASSERT_EQ(rules[newHorizonsNecromancy::POLICY_KEY]["version"].Integer(), captureVersion());
			ASSERT_EQ(captured->tombGuardDoubleStrike, captureVersion() == 2);
		}
		// Choose legal preceding tiers; current Tomb Guard must occupy Expert.
		attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::DARK_CONVERSION_ID});
		if(captureVersion() == 2)
			attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID,
				newHorizonsNecromancy::OSSUARY_ID});
		attackerSideHero->applyPerkSelection({newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::SOUL_HARVESTER_ID});
		ASSERT_TRUE(attackerSideHero->hasActivePerk(newHorizonsNecromancy::SKILL_ID,
			newHorizonsNecromancy::SOUL_HARVESTER_ID));
		const auto & pool = rules["skills"][newHorizonsNecromancy::SKILL_ID]["perks"].Vector();
		const auto tomb = std::find_if(pool.begin(), pool.end(), [](const auto & row)
		{
			return row["id"].String() == newHorizonsNecromancy::SOUL_HARVESTER_ID;
		});
		ASSERT_NE(tomb, pool.end());
		EXPECT_EQ((*tomb)["requires"].String(), captureVersion() == 2 ? "expert" : "advanced");

		// Round-trip the real world before combat; do not claim ongoing-battle save support.
		CGameState restored;
		restored.preInit(LIBRARY);
		restored.loadFromMemory(gameState()->saveToMemory());
		const auto * loaded = restored.getHero(attackerSideHero->id);
		ASSERT_NE(loaded, nullptr);
		EXPECT_EQ(loaded->getPerkState().rules, rules);
		EXPECT_EQ(newHorizonsNecromancy::tombGuardDoubleStrike(loaded, CreatureID(CreatureID::SKELETON)),
			captureVersion() == 2);
		EXPECT_EQ(newHorizonsNecromancy::tombGuardAdditionalRetaliations(loaded, CreatureID(CreatureID::SKELETON)),
			captureVersion() == 1 ? 1 : 0);

		startBattle();
		skeleton = addStack(BattleSide::ATTACKER,
			creatureByName(std::get<1>(GetParam()) ? "core:skeletonWarrior" : "core:skeleton"),
			BattleHex(leftHex), 10);
		enemy = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 10);
		ASSERT_NE(skeleton, nullptr);
		ASSERT_NE(enemy, nullptr);
		if(std::get<2>(GetParam()))
			skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
				BonusType::ADDITIONAL_ATTACK, BonusSource::OTHER, 1, BonusSourceID()));
		beginCombat();
	}

	int hits(uint32_t id, bool counter) const
	{
		return static_cast<int>(std::count_if(server.attacks.begin(), server.attacks.end(),
			[&](const auto & attack) { return attack.stackAttacking == id && attack.counter() == counter; }));
	}

	CStack * skeleton = nullptr;
	CStack * enemy = nullptr;
};
}

TEST_P(NewHorizonsTombGuardTest, OrdinaryAttackDoublesWithoutMultiplyingAndRetaliationStaysSingle)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int ordinaryAttacks = captureVersion() == 2 || std::get<2>(GetParam()) ? 2 : 1;
	EXPECT_EQ(skeleton->getTotalAttacks(false), ordinaryAttacks);
	EXPECT_EQ(skeleton->getTotalAttacks(true), std::get<2>(GetParam()) ? 2 : 1);
	EXPECT_EQ(skeleton->counterAttacks.available(), captureVersion() == 1 ? 2 : 1);
	server.attacks.clear();
	ASSERT_TRUE(attack(skeleton, enemy->getPosition()));
	EXPECT_EQ(hits(skeleton->unitId(), false), ordinaryAttacks);
	EXPECT_EQ(hits(enemy->unitId(), true), 1);
	ASSERT_TRUE(skeleton->alive());
	ASSERT_TRUE(enemy->alive());

	server.attacks.clear();
	ASSERT_TRUE(attack(enemy, skeleton->getPosition()));
	EXPECT_EQ(hits(skeleton->unitId(), true), 1);
	EXPECT_EQ(skeleton->counterAttacks.available(), captureVersion() == 1 ? 1 : 0);
}

TEST_P(NewHorizonsTombGuardTest, LuckResistanceAndAttackCountFollowRecipientFamilyAndController)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int grantedLuck = captureVersion() == 2 ? 3 : 0;
	const auto before = skeleton->getBonusesOfType(BonusType::LUCK);
	const int raw = before->totalValue();
	TConstBonusListPtr display;
	EXPECT_EQ(skeleton->luckValAndBonusList(display), raw + grantedLuck);
	EXPECT_EQ(before->totalValue(), raw);
	EXPECT_EQ(display->size(), before->size() + (grantedLuck != 0));

	EXPECT_EQ(battle()->battleGetAttackLuck(skeleton, enemy, false, false), raw + grantedLuck);
	EXPECT_EQ(skeleton->magicResistance(), captureVersion() == 2 ? 50 : 0);
	EXPECT_EQ(skeleton->intrinsicMagicalDamageReductionPercent(), captureVersion() == 1 ? 30 : 0);
	const auto inputs = battle::ProjectedUnitEnvironmentState::capture(*battle());
	EXPECT_EQ(inputs.inputs(BattleSide::ATTACKER).necromancy.doubleStrike, captureVersion() == 2);
	EXPECT_EQ(inputs.inputs(BattleSide::ATTACKER).necromancy.luckBonus, grantedLuck);
#if ENABLE_BATTLE_AI
	auto observer = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	observer->onBattleStarted(battle());
	TombGuardEnvironment environment(gameState());
	HypotheticBattle projected(&environment, observer->getBattle(BattleID(0)));
	const auto * actor = projected.battleGetUnitByID(skeleton->unitId());
	const auto * target = projected.battleGetUnitByID(enemy->unitId());
	ASSERT_NE(actor, nullptr);
	ASSERT_NE(target, nullptr);
	EXPECT_EQ(actor->getTotalAttacks(false), skeleton->getTotalAttacks(false));
	EXPECT_EQ(actor->magicResistance(), skeleton->magicResistance());
	EXPECT_EQ(projected.battleGetAttackLuck(actor, target, false, false), raw + grantedLuck);
#endif
	EXPECT_EQ(enemy->magicResistance(), 0);
	EXPECT_EQ(enemy->necromancyCombatPassives().luckBonus, 0);
	skeleton->beginBattleForm(creatureByName("core:zombie"), 2);
	EXPECT_EQ(skeleton->necromancyCombatPassives().luckBonus, 0);
	EXPECT_EQ(skeleton->magicResistance(), 0);
	EXPECT_FALSE(skeleton->necromancyCombatPassives().doubleStrike);
	skeleton->endBattleForm();
	EXPECT_EQ(skeleton->necromancyCombatPassives().luckBonus, grantedLuck);
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::ONE_BATTLE,
		BonusType::HYPNOTIZED, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(skeleton->necromancyCombatPassives().luckBonus, 0);
	EXPECT_EQ(skeleton->magicResistance(), 0);
	EXPECT_FALSE(skeleton->necromancyCombatPassives().doubleStrike);
	EXPECT_EQ(skeleton->getTotalAttacks(false), std::get<2>(GetParam()) ? 2 : 1);
}

TEST_P(NewHorizonsTombGuardTest, ResistanceStacksBeforeTheExistingCapAndProjectionAppliesItOnce)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 30, BonusSourceID()));
	const int expected = captureVersion() == 2 ? 75 : 30;
	EXPECT_EQ(skeleton->magicResistance(), expected);
	EXPECT_EQ(battle()->battleGetMagicResistance(skeleton), expected);
#if ENABLE_BATTLE_AI
	auto observer = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	observer->onBattleStarted(battle());
	TombGuardEnvironment environment(gameState());
	HypotheticBattle projected(&environment, observer->getBattle(BattleID(0)));
	EXPECT_EQ(projected.battleGetUnitByID(skeleton->unitId())->magicResistance(), expected);
#endif
}

TEST_P(NewHorizonsTombGuardTest, LuckIsFlatAfterOrdinaryPercentagesAndKeepsNoLuckPrecedence)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(skeleton->valOfBonuses(BonusType::LUCK), 0);
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LUCK, BonusSource::OTHER, 2, BonusSourceID()));
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LUCK, BonusSource::OTHER, 50, BonusSourceID(), BonusSubtypeID(), BonusValueType::PERCENT_TO_ALL));
	ASSERT_EQ(skeleton->valOfBonuses(BonusType::LUCK), 3);
	const int expected = captureVersion() == 2 ? 6 : 3;
	TConstBonusListPtr explanation;
	EXPECT_EQ(skeleton->luckValAndBonusList(explanation), expected);
	EXPECT_EQ(battle()->battleGetAttackLuck(skeleton, enemy, false, false), expected);
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::NO_LUCK, BonusSource::OTHER, 1, BonusSourceID()));
	EXPECT_EQ(skeleton->luckVal(), 0);
	EXPECT_EQ(battle()->battleGetAttackLuck(skeleton, enemy, false, false), 0);
}

TEST_P(NewHorizonsTombGuardTest, MaximumIntegerOrdinaryLuckRemainsClampedWithoutOverflow)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	ASSERT_EQ(skeleton->valOfBonuses(BonusType::LUCK), 0);
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::LUCK, BonusSource::OTHER, std::numeric_limits<int>::max(), BonusSourceID()));
	EXPECT_EQ(skeleton->luckVal(), 10);
	EXPECT_EQ(battle()->battleGetAttackLuck(skeleton, enemy, false, false), 10);
}

TEST_P(NewHorizonsTombGuardTest, MixedLegacyMagicKeepsBonewardAndItsZeroSourceControlConsistent)
{
	legacyMagic = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const int granted = captureVersion() == 2 ? 50 : 0;
	EXPECT_EQ(skeleton->magicResistance(), granted);
	EXPECT_EQ(enemy->magicResistance(), 0);
	skeleton->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT,
		BonusType::MAGIC_RESISTANCE, BonusSource::OTHER, 30, BonusSourceID()));
	EXPECT_EQ(skeleton->magicResistance(), 30 + granted);
#if ENABLE_BATTLE_AI
	auto observer = std::make_shared<CBattleCallback>(PlayerColor(0), nullptr);
	observer->onBattleStarted(battle());
	TombGuardEnvironment environment(gameState());
	HypotheticBattle projected(&environment, observer->getBattle(BattleID(0)));
	const auto * actor = projected.battleGetUnitByID(skeleton->unitId());
	const auto * target = projected.battleGetUnitByID(enemy->unitId());
	EXPECT_EQ(actor->magicResistance(), 30 + granted);
	EXPECT_EQ(target->magicResistance(), 0);
	if(granted == 0)
	{
		EXPECT_FALSE(projected.unitMagicResistance(actor));
	}
#endif
}

INSTANTIATE_TEST_SUITE_P(CapturesFormsAndExistingDoubleAttack, NewHorizonsTombGuardTest,
	::testing::Combine(::testing::Values(0, 1, 2), ::testing::Bool(), ::testing::Bool()));
