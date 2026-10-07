/*
 * NewHorizonsLastStandAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of the license available in license.txt file, in main folder
 */
#include "StdInc.h"

#include "../server/battles/HeroCommandFixture.h"
#include "../hero/NewHorizonsHeroRulesFixture.h"

#include "../../AI/BattleAI/AttackPossibility.h"
#include "../../AI/BattleAI/BattleExchangeVariant.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/CSkillHandler.h"
#include "../../lib/CStack.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/battle/CUnitState.h"
#include "../../lib/battle/NewHorizonsArmorer.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/BattleChanges.h"
#include "../../server/CGameHandler.h"
#include <vcmi/Environment.h>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace
{
constexpr std::string_view armorerSkillId = newHorizonsArmorer::SKILL_ID;
constexpr std::string_view pavisePerkId = "new-horizons:armorer.pavise";
constexpr std::string_view veteranPerkId = "new-horizons:armorer.veteran";
constexpr std::string_view lastStandPerkId = newHorizonsArmorer::LAST_STAND_PERK_ID;

bool activateLastStand(JsonNode & rules)
{
	auto & perks = rules["skills"][std::string(armorerSkillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [](const JsonNode & perk)
	{
		return perk["id"].String() == lastStandPerkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class LastStandAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;

public:
	explicit LastStandAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};

class NewHorizonsLastStandAITest : public HeroCommandFixture
{
protected:
	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, testHeroRules());
		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!activateLastStand(perkRules))
			throw std::runtime_error("Missing Last Stand from the New Horizons Armorer registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, std::string_view perkId,
		MasteryLevel::Type requiredRank)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offers = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offers.begin(), offers.end(), [perkId](const auto & offer)
			{
				return offer.selection.skillId == armorerSkillId && offer.selection.perkId == perkId;
			});
			if(selected == offers.end())
				continue;

			ASSERT_EQ(selected->requiredRank, static_cast<int>(requiredRank));
			gameHandler->levelUpHero(hero, offers,
				static_cast<size_t>(std::distance(offers.begin(), selected)), seed, false);
			ASSERT_TRUE(hero->hasActivePerk(std::string(armorerSkillId), std::string(perkId)));
			return;
		}
		FAIL() << "No legal Armorer offer contained " << perkId;
	}

	void selectLastStand(CGHeroInstance * hero)
	{
		const int decoded = SecondarySkill::decode(std::string(armorerSkillId));
		ASSERT_GE(decoded, 0);
		const SecondarySkill armorer(decoded);
		hero->setSecSkillLevel(armorer, MasteryLevel::BASIC, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, pavisePerkId, MasteryLevel::BASIC);
		hero->setSecSkillLevel(armorer, MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, veteranPerkId, MasteryLevel::ADVANCED);
		hero->setSecSkillLevel(armorer, MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		acceptPerkThroughOffer(hero, lastStandPerkId, MasteryLevel::EXPERT);
	}

	void removeDeployedUnits()
	{
		BattleUnitsChanged remove;
		remove.battleID = BattleID(0);
		for(const auto * unit : battle()->battleGetAllUnits(false))
			remove.changedStacks.emplace_back(unit->unitId(), UnitChanges::EOperation::REMOVE);
		if(!remove.changedStacks.empty())
			gameHandler->sendAndApply(remove);
	}
};
}

TEST_F(NewHorizonsLastStandAITest, DetachedLethalAttackForecastAndReplayMatchWithoutChangingLiveBattle)
{
	startGame();
	selectLastStand(defenderSideHero);
	startBattle();
	removeDeployedUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 100);
	auto * target = addStack(BattleSide::DEFENDER, creatureByName("core:pikeman"), BattleHex(rightHex), 2);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(target, nullptr);
	forceMaximumDamage(attacker);
	blockRetaliation(target);
	beginCombat();

	const auto liveAttackerHealth = attacker->getAvailableHealth();
	const auto liveTargetHealth = target->getAvailableHealth();
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));

	LastStandAIEnvironment environment(gameState());
	// The spectator callback is deliberately used for this rules projection so
	// the opposing hero's selected perk is visible to the combat evaluator.
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	ASSERT_NE(callback->battleGetFightingHero(BattleSide::DEFENDER), nullptr);
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	auto projectedTarget = model->getForUpdate(target->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	ASSERT_NE(projectedTarget, nullptr);

	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(
		projectedAttacker.get(), projectedTarget.get(), 0, false),
		projectedAttacker->getPosition(), damageCache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	const auto projectedTargetState = std::find_if(prediction.affectedUnits.begin(), prediction.affectedUnits.end(),
		[target](const auto & unit) { return unit->unitId() == target->unitId(); });
	ASSERT_NE(projectedTargetState, prediction.affectedUnits.end());
	EXPECT_TRUE((*projectedTargetState)->alive());
	EXPECT_EQ((*projectedTargetState)->getCount(), 1);
	EXPECT_EQ((*projectedTargetState)->getAvailableHealth(), 1);
	EXPECT_TRUE((*projectedTargetState)->defended());
	EXPECT_TRUE((*projectedTargetState)->armorerLastStandDefending);
	EXPECT_TRUE(prediction.effectPreview->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_EQ(attacker->getAvailableHealth(), liveAttackerHealth);
	EXPECT_EQ(target->getAvailableHealth(), liveTargetHealth);

	BattleExchangeVariant replay;
	replay.trackAttack(prediction, model, damageCache);
	const auto replayedTarget = model->getForUpdate(target->unitId());
	EXPECT_TRUE(replayedTarget->alive());
	EXPECT_EQ(replayedTarget->getCount(), 1);
	EXPECT_EQ(replayedTarget->getAvailableHealth(), 1);
	EXPECT_TRUE(replayedTarget->defended());
	EXPECT_TRUE(replayedTarget->armorerLastStandDefending);
	EXPECT_TRUE(model->armorerLastStandUsed(BattleSide::DEFENDER));
	EXPECT_EQ(attacker->getAvailableHealth(), liveAttackerHealth);
	EXPECT_EQ(target->getAvailableHealth(), liveTargetHealth);
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::DEFENDER));
}

TEST_F(NewHorizonsLastStandAITest, OwnedLastStandIsVisibleToOrdinaryPlayerProjectionOnLethalRetaliation)
{
	startGame();
	selectLastStand(attackerSideHero);
	startBattle();
	removeDeployedUnits();
	auto * attacker = addStack(BattleSide::ATTACKER, creatureByName("core:angel"), BattleHex(leftHex), 1);
	auto * defender = addStack(BattleSide::DEFENDER, creatureByName("core:peasant"), BattleHex(rightHex), 1000);
	ASSERT_NE(attacker, nullptr);
	ASSERT_NE(defender, nullptr);
	forceMaximumDamage(defender);
	beginCombat();
	ASSERT_GT(battle()->calculateDmgRange(BattleAttackInfo(defender, attacker, 0, false)).damage.max,
		attacker->getAvailableHealth());

	const auto owner = battle()->sideToPlayer(BattleSide::ATTACKER);
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), owner);
	ASSERT_EQ(callback->battleGetFightingHero(BattleSide::ATTACKER), attackerSideHero);
	LastStandAIEnvironment environment(gameState());
	auto model = std::make_shared<HypotheticBattle>(&environment, callback);
	auto projectedAttacker = model->getForUpdate(attacker->unitId());
	auto projectedDefender = model->getForUpdate(defender->unitId());
	ASSERT_NE(projectedAttacker, nullptr);
	ASSERT_NE(projectedDefender, nullptr);

	const auto attackerHealthBefore = attacker->getAvailableHealth();
	const auto defenderHealthBefore = defender->getAvailableHealth();
	DamageCache damageCache;
	damageCache.buildDamageCache(model, BattleSide::ATTACKER);
	const auto prediction = AttackPossibility::evaluate(BattleAttackInfo(
		projectedAttacker.get(), projectedDefender.get(), 0, false),
		projectedAttacker->getPosition(), damageCache, model);
	ASSERT_NE(prediction.effectPreview, nullptr);
	ASSERT_NE(prediction.attackerState, nullptr);
	EXPECT_EQ(prediction.attackerState->unitId(), attacker->unitId());
	EXPECT_TRUE(prediction.attackerState->alive());
	EXPECT_EQ(prediction.attackerState->getAvailableHealth(), 1);
	EXPECT_TRUE(prediction.attackerState->defended());
	EXPECT_TRUE(prediction.attackerState->armorerLastStandDefending);
	const auto branchAttacker = prediction.effectPreview->getForUpdate(attacker->unitId());
	ASSERT_NE(branchAttacker, nullptr);
	EXPECT_EQ(branchAttacker->getAvailableHealth(), prediction.attackerState->getAvailableHealth());
	EXPECT_EQ(branchAttacker->armorerLastStandDefending, prediction.attackerState->armorerLastStandDefending);
	EXPECT_TRUE(prediction.effectPreview->armorerLastStandUsed(BattleSide::ATTACKER));
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(attacker->getAvailableHealth(), attackerHealthBefore);
	EXPECT_EQ(defender->getAvailableHealth(), defenderHealthBefore);

	BattleExchangeVariant replay;
	replay.trackAttack(prediction, model, damageCache);
	const auto replayedAttacker = model->getForUpdate(attacker->unitId());
	ASSERT_NE(replayedAttacker, nullptr);
	EXPECT_TRUE(replayedAttacker->alive());
	EXPECT_EQ(replayedAttacker->getAvailableHealth(), 1);
	EXPECT_TRUE(replayedAttacker->defended());
	EXPECT_TRUE(replayedAttacker->armorerLastStandDefending);
	EXPECT_TRUE(model->armorerLastStandUsed(BattleSide::ATTACKER));
	EXPECT_EQ(attacker->getAvailableHealth(), attackerHealthBefore);
	EXPECT_EQ(defender->getAvailableHealth(), defenderHealthBefore);
	EXPECT_FALSE(battle()->armorerLastStandUsed(BattleSide::ATTACKER));
}
