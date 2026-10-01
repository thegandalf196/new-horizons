/*
 * NewHorizonsFortificationEngineerTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in the main folder
 *
 */
#include "StdInc.h"
#include "BattleTestFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CSkillHandler.h"
#include "../../../lib/battle/BattleAction.h"
#include "../../../lib/battle/BattleAttackInfo.h"
#include "../../../lib/battle/CBattleInfoCallback.h"
#include "../../../lib/battle/PossiblePlayerBattleAction.h"
#include "../../../lib/constants/EntityIdentifiers.h"
#include "../../../lib/constants/Enumerations.h"
#include "../../../lib/entities/hero/NewHorizonsCapabilityRules.h"
#include "../../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../../lib/gameState/CGameState.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/mapObjects/CGTownInstance.h"
#include "../../../lib/mapping/CMap.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/json/JsonNode.h"
#include "../../../lib/filesystem/ResourcePath.h"
#include "../../../server/CGameHandler.h"
#include "../../../server/battles/BattleProcessor.h"

#include <algorithm>
#include <cstdint>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace
{
constexpr auto warMachinesSkillId = "new-horizons:warMachines";
constexpr auto surgeonPerkId = "new-horizons:warMachines.surgeon";
constexpr auto piercingBoltsPerkId = "new-horizons:warMachines.piercingBolts";
constexpr auto fortificationEngineerPerkId = "new-horizons:warMachines.fortificationEngineer";

bool setPerkActive(JsonNode & rules, const std::string_view skillId, const std::string_view perkId)
{
	auto & perks = rules["skills"][std::string(skillId)]["perks"].Vector();
	const auto found = std::find_if(perks.begin(), perks.end(), [perkId](const JsonNode & perk)
	{
		return perk["id"].String() == perkId;
	});
	if(found == perks.end())
		return false;

	(*found)["effect"]["status"].String() = "active";
	return true;
}

class NewHorizonsFortificationEngineerTest : public BattleTestFixture
{
protected:
	CGTownInstance * fortifiedTown = nullptr;

	void SetUp() override
	{
		BattleTestFixture::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons content module";
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);

		JsonNode perkRules(JsonPath::builtin("config/newHorizonsPerks"));
		if(!setPerkActive(perkRules, warMachinesSkillId, surgeonPerkId)
			|| !setPerkActive(perkRules, warMachinesSkillId, piercingBoltsPerkId)
			|| !setPerkActive(perkRules, warMachinesSkillId, fortificationEngineerPerkId))
			throw std::runtime_error("Missing War Machines perk from the New Horizons perk registry");
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, std::move(perkRules));

		// Use the current saved capability rules. A legacy fixture would silently
		// exercise a different Siege scale from the active New Horizons ruleset.
		JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, std::move(capabilities));
	}

	SecondarySkill warMachines() const
	{
		const int decoded = SecondarySkill::decode(warMachinesSkillId);
		EXPECT_GE(decoded, 0);
		return SecondarySkill(decoded);
	}

	void acceptPerkThroughOffer(CGHeroInstance * hero, const std::string_view perkId)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};

		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			const auto selected = std::find_if(offer.begin(), offer.end(), [perkId](const auto & candidate)
			{
				return candidate.selection.skillId == warMachinesSkillId
					&& candidate.selection.perkId == perkId;
			});
			if(selected == offer.end())
				continue;

			const auto choice = static_cast<size_t>(std::distance(offer.begin(), selected));
			gameHandler->levelUpHero(hero, offer, choice, seed, false);
			ASSERT_TRUE(hero->hasActivePerk(warMachinesSkillId, std::string(perkId)));
			return;
		}

		FAIL() << "No legal New Horizons perk offer contained " << perkId;
	}

	void prepareFullWarMachines(CGHeroInstance * hero, const bool selectFortificationEngineer)
	{
		const auto skill = warMachines();
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(warMachinesSkillId,
			hero->getSecSkillLevel(skill)));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(warMachinesSkillId), MasteryLevel::BASIC);
		acceptPerkThroughOffer(hero, surgeonPerkId);

		int currentRank = hero->getPerkSkillRank(warMachinesSkillId);
		ASSERT_EQ(currentRank, MasteryLevel::BASIC);
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(warMachinesSkillId, currentRank));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(warMachinesSkillId), MasteryLevel::ADVANCED);
		acceptPerkThroughOffer(hero, piercingBoltsPerkId);

		currentRank = hero->getPerkSkillRank(warMachinesSkillId);
		ASSERT_EQ(currentRank, MasteryLevel::ADVANCED);
		ASSERT_TRUE(hero->getPerkState().canAdvanceSkillNormally(warMachinesSkillId, currentRank));
		gameHandler->levelUpHero(hero, skill, false);
		ASSERT_EQ(hero->getPerkSkillRank(warMachinesSkillId), MasteryLevel::EXPERT);

		if(selectFortificationEngineer)
			acceptPerkThroughOffer(hero, fortificationEngineerPerkId);
		else
			ASSERT_FALSE(hero->hasActivePerk(warMachinesSkillId, fortificationEngineerPerkId));
	}

	void prepareFortifiedTownBattle()
	{
		startGame(true);
		const auto towns = gameState()->getPlayerState(PlayerColor(1))->getTowns();
		ASSERT_EQ(towns.size(), 1u);
		fortifiedTown = towns.front();
		ASSERT_EQ(fortifiedTown->fortLevel(), CGTownInstance::FORT);

		// The tiny map gives this town its standard Fort. Build its Citadel through
		// the authoritative, prerequisite-checked path; this supplies a real tower.
		for(const auto resource : {GameResID(GameResID::WOOD), GameResID(GameResID::ORE), GameResID(GameResID::GOLD)})
			grantResources(PlayerColor(1), resource, 100000);
		ASSERT_TRUE(gameHandler->buildStructure(fortifiedTown->id, BuildingID::CITADEL));

		const auto fortifications = fortifiedTown->fortificationsLevel();
		ASSERT_GT(fortifications.wallsHealth, 0);
		ASSERT_GT(fortifications.citadelHealth, 0);
	}

	const CStack * defensiveTower() const
	{
		const auto towers = battle()->battleGetStacksIf([](const CStack * stack)
		{
			return stack->unitSide() == BattleSide::DEFENDER && stack->isTurret();
		});
		return towers.empty() ? nullptr : towers.front();
	}

	void advanceUntilTowerActivation(const CStack * tower)
	{
		beginCombat();
		const auto firstRound = battle()->getRound();
		const auto maximumTurns = battle()->stacks.size() * 4;
		for(size_t turn = 0; turn < maximumTurns && battle()->battleActiveUnit() != tower; ++turn)
		{
			ASSERT_EQ(battle()->getRound(), firstRound);
			const auto * active = battle()->battleActiveUnit();
			ASSERT_NE(active, nullptr);
			ASSERT_NE(active->unitId(), tower->unitId());
			ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
				battle()->sideToPlayer(active->unitSide()), BattleAction::makeDefend(active)));
		}
		ASSERT_EQ(battle()->battleActiveUnit(), tower);
	}

	std::pair<int, int> expectedTowerDamageBases(const CGHeroInstance * hero) const
	{
		const auto & rules = hero->getCapabilityRules();
		EXPECT_EQ(rules["rulesetVersion"].Integer(), newHorizonsHeroes::CAPABILITY_RULESET_VERSION);
		const auto capabilities = hero->getSiegeCapabilities();
		EXPECT_TRUE(capabilities);
		if(!capabilities)
			return {0, 0};

		const int ordinaryBase = newHorizonsHeroes::capabilitySiegeOutput(rules,
			capabilities->siegeRating, "defensiveTowerDamage");
		const int boostedSiege = capabilities->siegeRating * 125 / 100;
		const int boostedBase = newHorizonsHeroes::capabilitySiegeOutput(rules,
			boostedSiege, "defensiveTowerDamage");
		return {ordinaryBase, boostedBase};
	}
};
}

TEST_F(NewHorizonsFortificationEngineerTest, ExpertEngineerManuallyTargetsRealFortifiedTownTowerAtBoostedSiegeOutput)
{
	prepareFortifiedTownBattle();
	prepareFullWarMachines(defenderSideHero, true);
	startBattle(fortifiedTown);

	const auto * tower = defensiveTower();
	ASSERT_NE(tower, nullptr);
	ASSERT_EQ(battle()->getDefendedTown(), fortifiedTown);
	ASSERT_EQ(battle()->battleGetOwnerHero(tower), defenderSideHero);
	ASSERT_TRUE(tower->isTurret());
	ASSERT_TRUE(defenderSideHero->hasActivePerk(warMachinesSkillId, fortificationEngineerPerkId));
	ASSERT_TRUE(battle()->battleCanUseFortificationEngineer(tower));

	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1000);
	ASSERT_NE(target, nullptr);
	ASSERT_TRUE(battle()->battleCanShoot(tower, target->getPosition()));

	const auto [ordinaryBase, boostedBase] = expectedTowerDamageBases(defenderSideHero);
	ASSERT_GT(boostedBase, ordinaryBase);
	EXPECT_NE(boostedBase, ordinaryBase * 125 / 100)
		<< "Fortification Engineer scales Siege before the tower output formula";
	const auto forecast = battle()->calculateDmgRange(BattleAttackInfo(tower, target, 0, true));
	EXPECT_EQ(forecast.damageBeforeDefense.min, boostedBase);
	EXPECT_EQ(forecast.damageBeforeDefense.max, boostedBase);

	advanceUntilTowerActivation(tower);
	BattleClientInterfaceData clientData{};
	const auto actions = battle()->getClientActionsForStack(tower, clientData);
	EXPECT_TRUE(std::ranges::any_of(actions, [](const PossiblePlayerBattleAction & action)
	{
		return action.get() == PossiblePlayerBattleAction::SHOOT;
	})) << "The active tower should expose the existing manual Shoot action";

	const auto acceptedForecast = battle()->calculateDmgRange(BattleAttackInfo(tower, target, 0, true)).damage;
	const auto healthBefore = target->getAvailableHealth();
	ASSERT_TRUE(gameHandler->battles->makePlayerBattleAction(BattleID(0),
		battle()->sideToPlayer(tower->unitSide()), BattleAction::makeShotAttack(tower, target)));
	const auto actualDamage = healthBefore - target->getAvailableHealth();
	EXPECT_GE(actualDamage, acceptedForecast.min);
	EXPECT_LE(actualDamage, acceptedForecast.max);
}

TEST_F(NewHorizonsFortificationEngineerTest, MissingSavedPerkUsesOrdinarySiegeOutputAndDoesNotGrantManualTowerControl)
{
	prepareFortifiedTownBattle();
	prepareFullWarMachines(defenderSideHero, false);
	startBattle(fortifiedTown);

	const auto * tower = defensiveTower();
	ASSERT_NE(tower, nullptr);
	ASSERT_EQ(battle()->battleGetOwnerHero(tower), defenderSideHero);
	ASSERT_FALSE(defenderSideHero->hasActivePerk(warMachinesSkillId, fortificationEngineerPerkId));
	EXPECT_FALSE(battle()->battleCanUseFortificationEngineer(tower));

	auto * target = addStack(BattleSide::ATTACKER, creatureByName("core:pikeman"), BattleHex(leftHex), 1000);
	ASSERT_NE(target, nullptr);
	const auto [ordinaryBase, boostedBase] = expectedTowerDamageBases(defenderSideHero);
	EXPECT_GT(boostedBase, ordinaryBase);
	const auto forecast = battle()->calculateDmgRange(BattleAttackInfo(tower, target, 0, true));
	EXPECT_EQ(forecast.damageBeforeDefense.min, ordinaryBase);
	EXPECT_EQ(forecast.damageBeforeDefense.max, ordinaryBase);
}

TEST_F(NewHorizonsFortificationEngineerTest, ExpertEngineerDoesNotApplyInAFieldBattle)
{
	startGame();
	prepareFullWarMachines(defenderSideHero, true);
	startBattle();
	ASSERT_EQ(battle()->getDefendedTown(), nullptr);

	const auto * tower = addStack(BattleSide::DEFENDER, CreatureID::ARROW_TOWERS, BattleHex(rightHex), 1);
	ASSERT_NE(tower, nullptr);
	ASSERT_EQ(battle()->battleGetOwnerHero(tower), defenderSideHero);
	EXPECT_FALSE(battle()->battleCanUseFortificationEngineer(tower));
}

TEST_F(NewHorizonsFortificationEngineerTest, AttackingHeroesPerkCannotControlTheDefendedTownsTowers)
{
	prepareFortifiedTownBattle();
	prepareFullWarMachines(attackerSideHero, true);
	startBattle(fortifiedTown);

	const auto * tower = defensiveTower();
	ASSERT_NE(tower, nullptr);
	ASSERT_EQ(tower->unitSide(), BattleSide::DEFENDER);
	ASSERT_EQ(battle()->battleGetOwnerHero(tower), defenderSideHero);
	ASSERT_TRUE(attackerSideHero->hasActivePerk(warMachinesSkillId, fortificationEngineerPerkId));
	EXPECT_FALSE(battle()->battleCanUseFortificationEngineer(tower));
}
