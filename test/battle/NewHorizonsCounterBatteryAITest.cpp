/*
 * NewHorizonsCounterBatteryAITest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 * License: GNU General Public License v2.0 or later; see license.txt in main folder
 */
#include "../StdInc.h"
#include "../server/battles/BattleTestFixture.h"

#include "../../AI/BattleAI/PotentialTargets.h"
#include "../../AI/BattleAI/StackWithBonuses.h"
#include "../../lib/GameConstants.h"
#include "../../lib/GameSettings.h"
#include "../../lib/battle/BattleAction.h"
#include "../../lib/battle/BattleAttackInfo.h"
#include "../../lib/battle/CPlayerBattleCallback.h"
#include "../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../lib/filesystem/ResourcePath.h"
#include "../../lib/gameState/CGameState.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/mapObjects/CGTownInstance.h"
#include "../../lib/mapping/CMap.h"
#include "../../lib/modding/CModHandler.h"
#include "../../server/CGameHandler.h"
#include "../../server/battles/BattleProcessor.h"

namespace
{
constexpr auto skillId = "new-horizons:warMachines";
constexpr auto perkId = "new-horizons:warMachines.counterBattery";

class CounterBatteryAIEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit CounterBatteryAIEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

class NewHorizonsCounterBatteryAITest : public BattleTestFixture
{
protected:
	CGTownInstance * town = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsPerks"));
		auto & perks = rules["skills"][skillId]["perks"].Vector();
		const auto perk = std::ranges::find_if(perks, [](const JsonNode & entry)
		{
			return entry["id"].String() == perkId;
		});
		if(perk == perks.end())
			throw std::runtime_error("Missing Counter-Battery registry entry");
		// Private pre-activation profile; production status is retained once active.
		if((*perk)["effect"]["status"].String() == "planned")
		{
			(*perk)["effect"]["status"].String() = "active";
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, rules);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
	}

	SecondarySkill warMachines() const
	{
		return SecondarySkill(SecondarySkill::decode(skillId));
	}

	void acceptPerk(CGHeroInstance * hero, const char * id)
	{
		const auto rank = [hero](const std::string & skill) { return hero->getPerkSkillRank(skill); };
		for(uint64_t seedValue = 0; seedValue < 4096; ++seedValue)
		{
			const auto offer = hero->getPerkState().prepareOffer(rank, seedValue);
			const auto choice = std::ranges::find_if(offer, [id](const auto & candidate)
			{
				return candidate.selection.skillId == skillId && candidate.selection.perkId == id;
			});
			if(choice == offer.end())
				continue;
			gameHandler->levelUpHero(hero, offer, std::distance(offer.begin(), choice), seedValue, false);
			ASSERT_TRUE(hero->hasActivePerk(skillId, id));
			return;
		}
		FAIL() << "Missing legal offer for " << id;
	}

	void learnCounterBattery(CGHeroInstance * hero)
	{
		const auto skill = warMachines();
		ASSERT_TRUE(skill.hasValue());
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(skillId), MasteryLevel::BASIC);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, "new-horizons:warMachines.surgeon"));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(skillId), MasteryLevel::ADVANCED);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, "new-horizons:warMachines.piercingBolts"));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(skillId), MasteryLevel::EXPERT);
		ASSERT_NO_FATAL_FAILURE(acceptPerk(hero, perkId));
	}

	void prepareTown()
	{
		startGame(true);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		town = towns.front();
		for(const auto resource : {GameResID(GameResID::WOOD), GameResID(GameResID::ORE), GameResID(GameResID::GOLD)})
			grantResources(PlayerColor(1), resource, 100000);
		ASSERT_TRUE(gameHandler->buildStructure(town->id, BuildingID::CITADEL));
		ASSERT_GT(town->fortificationsLevel().citadelHealth, 0);
	}

	void activate(const CStack * source)
	{
		beginCombat();
		const auto limit = battle()->stacks.size() * 4;
		for(size_t i = 0; i < limit && battle()->battleActiveUnit() != source; ++i)
		{
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
		}
		ASSERT_EQ(battle()->battleActiveUnit(), source);
	}

	void checkSelectedShot(const CStack * source, const CStack * target)
	{
		ASSERT_TRUE(battle()->battleCanShootAction(source, target->getPosition()));
		auto * hero = source->unitSide() == BattleSide::ATTACKER ? attackerSideHero : defenderSideHero;
		const BattleAttackInfo shot(source, target, 0, true);
		const auto boosted = battle()->calculateDmgRange(shot).damage;
		ASSERT_TRUE(hero->getSiegeCapabilities());
		const auto boostedSiege = hero->getSiegeCapabilities()->siegeRating;
		const auto creatures = battle()->battleGetStacksIf([source](const CStack * stack)
		{
			return stack->alive() && stack->unitSide() != source->unitSide()
				&& !stack->isTurret() && !stack->hasBonusOfType(BonusType::SIEGE_WEAPON);
		});
		ASSERT_FALSE(creatures.empty());
		const BattleAttackInfo creatureShot(source, creatures.front(), 0, true);
		const auto ordinaryCreature = battle()->calculateDmgRange(creatureShot).damage;
		// Accepted rank loss disables the Expert selection. Compensate only the
		// rank's Siege delta so both forecasts have identical machine base output.
		gameHandler->changeSecSkill(hero, warMachines(), MasteryLevel::ADVANCED, ChangeValueMode::ABSOLUTE);
		ASSERT_FALSE(hero->hasActivePerk(skillId, perkId));
		ASSERT_TRUE(hero->getSiegeCapabilities());
		auto compensation = std::make_shared<Bonus>(BonusDuration::ONE_BATTLE, BonusType::SIEGE_RATING,
			BonusSource::OTHER, boostedSiege - hero->getSiegeCapabilities()->siegeRating, BonusSourceID());
		hero->addNewBonus(compensation);
		ASSERT_EQ(hero->getSiegeCapabilities()->siegeRating, boostedSiege);
		const auto ordinary = battle()->calculateDmgRange(shot).damage;
		const auto unselectedCreature = battle()->calculateDmgRange(creatureShot).damage;
		hero->removeBonus(compensation);
		gameHandler->changeSecSkill(hero, warMachines(), MasteryLevel::EXPERT, ChangeValueMode::ABSOLUTE);
		ASSERT_TRUE(hero->hasActivePerk(skillId, perkId));
		ASSERT_GT(ordinary.min, 0);
		// The normal calculator rounds once after all final multipliers; a
		// previously rounded ordinary bound can differ by at most one point.
		EXPECT_GE(boosted.min, ordinary.min * 3 / 2);
		EXPECT_LE(boosted.min, ordinary.min * 3 / 2 + 1);
		EXPECT_GE(boosted.max, ordinary.max * 3 / 2);
		EXPECT_LE(boosted.max, ordinary.max * 3 / 2 + 1);
		EXPECT_EQ(ordinaryCreature.min, unselectedCreature.min);
		EXPECT_EQ(ordinaryCreature.max, unselectedCreature.max);
		CounterBatteryAIEnvironment environment(gameState());
		auto callback = std::make_shared<CPlayerBattleCallback>(battle(), battle()->battleGetOwner(source));
		auto model = std::make_shared<HypotheticBattle>(&environment, callback);
		DamageCache cache;
		PotentialTargets candidates(source, cache, model);
		ASSERT_FALSE(candidates.possibleAttacks.empty());
		const auto & selected = candidates.bestAction();
		ASSERT_TRUE(selected.attack.shooting);
		ASSERT_EQ(selected.attack.defender->unitId(), target->unitId());
		EXPECT_EQ(selected.attack.attacker->unitId(), source->unitId());
		const auto forecast = model->calculateDmgRange(selected.attack).damage;
		EXPECT_EQ(cache.getDamage(source, target, model), model->battleExpectedLuckDamage(selected.attack));
		const auto healthBefore = target->getAvailableHealth();
		const auto attacksBefore = server.attacks.size();
		ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
			battle()->sideToPlayer(source->unitSide()), BattleAction::makeShotAttack(source, target)));
		const auto actualDamage = healthBefore - target->getAvailableHealth();
		EXPECT_GE(actualDamage, std::min(forecast.min, healthBefore));
		EXPECT_LE(actualDamage, std::min(forecast.max, healthBefore));
		const auto publishedAttack = std::find_if(server.attacks.begin() + attacksBefore, server.attacks.end(),
			[source](const BattleAttack & packet) { return packet.stackAttacking == source->unitId(); });
		ASSERT_NE(publishedAttack, server.attacks.end());
		const auto hit = std::ranges::find(publishedAttack->bsa, target->unitId(), &BattleStackAttacked::stackAttacked);
		ASSERT_NE(hit, publishedAttack->bsa.end());
		// prepareAttacked updates the packet amount to damage actually absorbed, including the overkill cap.
		EXPECT_EQ(hit->damageAmount, actualDamage);
		EXPECT_GE(hit->damageAmount, std::min(forecast.min, healthBefore));
		EXPECT_LE(hit->damageAmount, std::min(forecast.max, healthBefore));
	}
};

TEST_F(NewHorizonsCounterBatteryAITest, RealTowerSelectsEnemyMachineAndAcceptedShotMatchesBoostedForecast)
{
	ASSERT_NO_FATAL_FAILURE(prepareTown());
	ASSERT_NO_FATAL_FAILURE(learnCounterBattery(defenderSideHero));
	startBattle(town);
	const auto towers = battle()->battleGetStacksIf([](const CStack * stack)
	{
		return stack->unitSide() == BattleSide::DEFENDER && stack->isTurret();
	});
	ASSERT_FALSE(towers.empty());
	const auto * tower = towers.front();
	ASSERT_EQ(battle()->getDefendedTown(), town);
	ASSERT_FALSE(tower->getPosition().isValid());
	auto * target = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(leftHex), 1);
	ASSERT_NE(target, nullptr);
	ASSERT_NO_FATAL_FAILURE(activate(tower));
	ASSERT_NO_FATAL_FAILURE(checkSelectedShot(tower, target));
}

TEST_F(NewHorizonsCounterBatteryAITest, BallistaCandidatesIncludeEnemyMachineAndSharedForecastUsesFinalBonus)
{
	startGame();
	ASSERT_NO_FATAL_FAILURE(learnCounterBattery(attackerSideHero));
	startBattle();
	auto * source = addStack(BattleSide::ATTACKER, CreatureID::BALLISTA, BattleHex(leftHex), 1);
	auto * target = addStack(BattleSide::DEFENDER, CreatureID::BALLISTA, BattleHex(rightHex + 5), 1);
	ASSERT_NE(source, nullptr);
	ASSERT_NE(target, nullptr);
	ASSERT_NO_FATAL_FAILURE(activate(source));
	ASSERT_NO_FATAL_FAILURE(checkSelectedShot(source, target));
}
