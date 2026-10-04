/*
 * NewHorizonsSkillSpecialtyTest.cpp, part of VCMI engine
 *
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"

#include "../../mock/GameHandlerTestServer.h"
#include "../../mock/TinyMapGameTest.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameLibrary.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/pathfinder/TurnInfo.h"
#include "../../../lib/serializer/CMemorySerializer.h"
#include "../../../server/CGameHandler.h"

#include <sstream>

namespace
{
constexpr auto LOGISTICS_SKILL = "new-horizons:logistics";
constexpr auto NAVIGATION_PERK = "new-horizons:logistics.navigation";
constexpr PlayerColor PLAYER(0);

SecondarySkill getNewHorizonsLogistics()
{
	const int skill = SecondarySkill::decode(LOGISTICS_SKILL);
	if(skill < 0)
		throw std::runtime_error("New Horizons Logistics is not registered");
	return SecondarySkill(skill);
}

class NewHorizonsSkillSpecialtyTest : public TinyMapGameTest
{
protected:
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		if(!vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE))
			GTEST_SKIP() << "Requires the New Horizons module";
	}

	void configurePlayer(PlayerSettings & settings) const override
	{
		if(settings.color == PLAYER)
			settings.connectedPlayerIDs.clear();
	}

	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto heroRules = JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
		if(!includeSpecialtyRules)
		{
			heroRules.Struct().erase("skillSpecialties");
			// Preserve an older, exact profile snapshot rather than inheriting the
			// newly-added optional object from the module defaults.
			heroRules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, std::move(heroRules));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES,
			JsonNode(JsonPath::builtin("config/newHorizonsCapabilities")));
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	}

	void startGame()
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		const CreatureID pikeman(CreatureID::decode("core:pikeman"));
		builder.size(36, false).name("NewHorizonsSkillSpecialty")
			.playerActive(PLAYER).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, heroType("core:kyrre"), PLAYER).heroExperience(0).heroGarrison({{pikeman, 1}})
			.hero({10, 5, 0}, heroType("core:gunnar"), PLAYER).heroExperience(0).heroGarrison({{pikeman, 1}})
			.hero({15, 5, 0}, heroType("core:dessa"), PLAYER).heroExperience(0).heroGarrison({{pikeman, 1}})
			.hero({20, 5, 0}, heroType("core:mephala"), PLAYER).heroExperience(0).heroGarrison({{pikeman, 1}});
		startWithMap(std::move(builder));
		revealMap(PLAYER);

		kyrre = findHeroAt({5, 5, 0});
		gunnar = findHeroAt({10, 5, 0});
		dessa = findHeroAt({15, 5, 0});
		control = findHeroAt({20, 5, 0});
		ASSERT_NE(kyrre, nullptr);
		ASSERT_NE(gunnar, nullptr);
		ASSERT_NE(dessa, nullptr);
		ASSERT_NE(control, nullptr);
		ASSERT_TRUE(kyrre->usesNewHorizonsMovement());
		ASSERT_TRUE(gunnar->usesNewHorizonsMovement());
		ASSERT_TRUE(dessa->usesNewHorizonsMovement());
		ASSERT_TRUE(control->usesNewHorizonsMovement());

		server = std::make_unique<GameHandlerTestServer>(gameState(), PLAYER);
		handler = std::make_unique<CGameHandler>(*server, gameState());
	}

	static HeroTypeID heroType(const char * id)
	{
		const int decoded = HeroTypeID::decode(id);
		if(decoded < 0)
			throw std::runtime_error(std::string("Missing hero in skill specialty fixture: ") + id);
		return HeroTypeID(decoded);
	}

	void setRank(CGHeroInstance * hero, const MasteryLevel::Type rank)
	{
		handler->changeSecSkill(hero, getNewHorizonsLogistics(), rank, ChangeValueMode::ABSOLUTE);
	}

	static int movementLimit(CGHeroInstance * hero, const bool water)
	{
		const auto turn = hero->getTurnInfo(0);
		if(!turn)
			throw std::runtime_error("Hero has no current-turn movement forecast");
		return water ? turn->getMovePointsLimitWater() : turn->getMovePointsLimitLand();
	}

	static int countSkillSpecialtyMarkers(const CGHeroInstance * hero)
	{
		return static_cast<int>(std::count_if(hero->getExportedBonusList().begin(), hero->getExportedBonusList().end(),
			[](const std::shared_ptr<Bonus> & bonus)
			{
				return bonus->type == BonusType::NONE
					&& bonus->source == BonusSource::HERO_SPECIAL
					&& bonus->stacking.starts_with("new-horizons:skill-specialty:");
			}));
	}

	void grantNavigationBasic(CGHeroInstance * hero)
	{
		const auto rankLookup = [hero](const std::string & skillId)
		{
			return hero->getPerkSkillRank(skillId);
		};
		for(uint64_t seed = 0; seed < 4096; ++seed)
		{
			const auto offer = hero->getPerkState().prepareOffer(rankLookup, seed);
			for(size_t index = 0; index < offer.size(); ++index)
			{
				if(offer[index].selection.perkId != NAVIGATION_PERK)
					continue;
				handler->levelUpHero(hero, offer, index, seed, false);
				return;
			}
		}
		throw std::runtime_error("Could not legally offer Basic Navigation in skill specialty fixture");
	}

	bool includeSpecialtyRules = true;
	CGHeroInstance * kyrre = nullptr;
	CGHeroInstance * gunnar = nullptr;
	CGHeroInstance * dessa = nullptr;
	CGHeroInstance * control = nullptr;
	std::unique_ptr<GameHandlerTestServer> server;
	std::unique_ptr<CGameHandler> handler;
};
}

TEST_F(NewHorizonsSkillSpecialtyTest, ScalesOnlyCoreLogisticsForTheThreeLegacySpecialists)
{
	startGame();
	const SecondarySkill coreLogistics(SecondarySkill::LOGISTICS);
	for(auto * specialist : {kyrre, gunnar, dessa})
	{
		ASSERT_TRUE(specialist->getHeroType()->secondarySkillSpecialtyAlias.has_value());
		EXPECT_EQ(specialist->getHeroType()->secondarySkillSpecialtyAlias->skill, coreLogistics);
		EXPECT_EQ(specialist->getSkillSpecialtyCoreBonusPercent(coreLogistics), 20);
		EXPECT_EQ(countSkillSpecialtyMarkers(specialist), 1);
	}
	EXPECT_EQ(control->getSkillSpecialtyCoreBonusPercent(coreLogistics), 0);
	EXPECT_EQ(countSkillSpecialtyMarkers(control), 0);

	std::vector<std::vector<std::pair<si32, std::string>>> originalPrototypeSpecialties;
	for(auto * specialist : {kyrre, gunnar, dessa})
	{
		std::vector<std::pair<si32, std::string>> prototype;
		for(const auto & bonus : specialist->getHeroType()->secondarySkillSpecialtyAlias->bonuses)
			prototype.emplace_back(bonus->val, bonus->stacking);
		originalPrototypeSpecialties.push_back(std::move(prototype));
	}

	const std::array<MasteryLevel::Type, 3> ranks = {
		MasteryLevel::BASIC, MasteryLevel::ADVANCED, MasteryLevel::EXPERT};
	const std::array<int, 3> expectedSpecialist = {224, 248, 272};
	const std::array<int, 3> expectedControl = {220, 240, 260};
	const std::array<CGHeroInstance *, 4> heroes = {kyrre, gunnar, dessa, control};
	std::array<std::unique_ptr<TurnInfoCache>, heroes.size()> sharedCaches;
	for(size_t heroIndex = 0; heroIndex < heroes.size(); ++heroIndex)
		sharedCaches[heroIndex] = std::make_unique<TurnInfoCache>(heroes[heroIndex]);
	for(size_t index = 0; index < ranks.size(); ++index)
	{
		for(auto * hero : heroes)
			setRank(hero, ranks[index]);

		for(size_t heroIndex = 0; heroIndex < 3; ++heroIndex)
		{
			TurnInfo refreshed(sharedCaches[heroIndex].get(), heroes[heroIndex], 0);
			EXPECT_EQ(refreshed.getMovePointsLimitLand(), expectedSpecialist[index]);
			EXPECT_EQ(refreshed.getMovePointsLimitWater(), expectedSpecialist[index]);
			EXPECT_EQ(heroes[heroIndex]->movementPointsLimit(), expectedSpecialist[index]);
		}
		TurnInfo refreshed(sharedCaches.back().get(), control, 0);
		EXPECT_EQ(refreshed.getMovePointsLimitLand(), expectedControl[index]);
		EXPECT_EQ(refreshed.getMovePointsLimitWater(), expectedControl[index]);
		EXPECT_EQ(control->movementPointsLimit(), expectedControl[index]);
	}

	for(size_t heroIndex = 0; heroIndex < 3; ++heroIndex)
	{
		const auto & alias = heroes[heroIndex]->getHeroType()->secondarySkillSpecialtyAlias->bonuses;
		ASSERT_EQ(alias.size(), originalPrototypeSpecialties[heroIndex].size());
		for(size_t bonusIndex = 0; bonusIndex < alias.size(); ++bonusIndex)
		{
			EXPECT_EQ(alias[bonusIndex]->val, originalPrototypeSpecialties[heroIndex][bonusIndex].first);
			EXPECT_EQ(alias[bonusIndex]->stacking, originalPrototypeSpecialties[heroIndex][bonusIndex].second);
		}
	}

	const auto heroId = kyrre->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * restoredKyrre = restored.getHero(heroId);
	ASSERT_NE(restoredKyrre, nullptr);
	EXPECT_EQ(restoredKyrre->getSkillSpecialtyCoreBonusPercent(coreLogistics), 20);
	EXPECT_EQ(countSkillSpecialtyMarkers(restoredKyrre), 1);
	EXPECT_EQ(restoredKyrre->movementPointsLimit(), expectedSpecialist.back());

	// Removing the skill after Expert invalidates the same cached daily pools.
	for(auto * hero : heroes)
		setRank(hero, MasteryLevel::NONE);
	for(size_t heroIndex = 0; heroIndex < heroes.size(); ++heroIndex)
	{
		TurnInfo refreshed(sharedCaches[heroIndex].get(), heroes[heroIndex], 0);
		EXPECT_EQ(refreshed.getMovePointsLimitLand(), 200);
		EXPECT_EQ(refreshed.getMovePointsLimitWater(), 200);
		EXPECT_EQ(heroes[heroIndex]->movementPointsLimit(), 200);
	}
}

TEST_F(NewHorizonsSkillSpecialtyTest, NavigationAddsItsFullSeaBonusBesideSpecializedCoreLogistics)
{
	startGame();
	setRank(kyrre, MasteryLevel::BASIC);
	grantNavigationBasic(kyrre);
	EXPECT_TRUE(kyrre->hasActivePerk(LOGISTICS_SKILL, NAVIGATION_PERK));
	EXPECT_EQ(movementLimit(kyrre, false), 224);
	EXPECT_EQ(movementLimit(kyrre, true), 274);
}

TEST_F(NewHorizonsSkillSpecialtyTest, OtherAndHeroSpecialMovementPercentagesRemainUnscaled)
{
	startGame();
	setRank(kyrre, MasteryLevel::BASIC);
	const auto land = BonusSubtypeID(BonusCustomSubtype::heroMovementLand);
	kyrre->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::OTHER, 7, BonusSourceID(), land, BonusValueType::PERCENT_TO_BASE));
	kyrre->addNewBonus(std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::MOVEMENT,
		BonusSource::HERO_SPECIAL, 5, BonusSourceID(kyrre->getHeroTypeID()), land, BonusValueType::PERCENT_TO_BASE));
	EXPECT_EQ(movementLimit(kyrre, false), 248); // 12% core + 7% OTHER + 5% other HERO_SPECIAL.
	EXPECT_EQ(movementLimit(kyrre, true), 224);
}

TEST_F(NewHorizonsSkillSpecialtyTest, MissingSavedRulesPreserveTheLegacyAliasAndUnmarkedBehavior)
{
	includeSpecialtyRules = false;
	startGame();
	const SecondarySkill coreLogistics(SecondarySkill::LOGISTICS);
	setRank(kyrre, MasteryLevel::BASIC);
	EXPECT_EQ(kyrre->getSkillSpecialtyCoreBonusPercent(coreLogistics), 0);
	EXPECT_EQ(countSkillSpecialtyMarkers(kyrre), 0);
	ASSERT_TRUE(kyrre->getHeroType()->secondarySkillSpecialtyAlias.has_value());
	const auto & legacyAlias = kyrre->getHeroType()->secondarySkillSpecialtyAlias->bonuses;
	ASSERT_FALSE(legacyAlias.empty());
	std::ostringstream aliasDescription;
	aliasDescription << "count=" << legacyAlias.size();
	for(size_t index = 0; index < legacyAlias.size(); ++index)
	{
		const auto & prototypeBonus = legacyAlias[index];
		aliasDescription << ";[" << index << "]type=" << static_cast<int>(prototypeBonus->type)
			<< ",source=" << static_cast<int>(prototypeBonus->source)
			<< ",subtype=" << prototypeBonus->subtype.toString()
			<< ",valType=" << static_cast<int>(prototypeBonus->valType)
			<< ",val=" << prototypeBonus->val
			<< ",sid=" << prototypeBonus->sid.toString()
			<< ",updater=" << static_cast<bool>(prototypeBonus->updater);

		const auto & localBonuses = kyrre->getExportedBonusList();
		const auto local = std::find_if(localBonuses.begin(), localBonuses.end(), [&prototypeBonus](const auto & candidate)
		{
			return candidate->type == prototypeBonus->type
				&& candidate->source == prototypeBonus->source
				&& candidate->subtype == prototypeBonus->subtype
				&& candidate->valType == prototypeBonus->valType
				&& candidate->val == prototypeBonus->val
				&& candidate->sid == prototypeBonus->sid
				&& candidate->updater == prototypeBonus->updater;
		});
		ASSERT_NE(local, localBonuses.end()) << "Legacy alias bonus is not present on the hero: " << aliasDescription.str();
	}
	RecordProperty("legacy_secondary_skill_specialty_alias", aliasDescription.str());
	EXPECT_EQ(movementLimit(kyrre, false), 220);
	EXPECT_EQ(movementLimit(kyrre, true), 220);

	const auto heroId = kyrre->id;
	const auto saved = gameState()->saveToMemory();
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(saved);
	auto * restoredKyrre = restored.getHero(heroId);
	ASSERT_NE(restoredKyrre, nullptr);
	EXPECT_EQ(restoredKyrre->getSkillSpecialtyCoreBonusPercent(coreLogistics), 0);
	EXPECT_EQ(countSkillSpecialtyMarkers(restoredKyrre), 0);
	EXPECT_EQ(restoredKyrre->movementPointsLimit(), 220);
}
