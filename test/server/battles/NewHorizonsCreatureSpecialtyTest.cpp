/*
 * NewHorizonsCreatureSpecialtyTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "HeroCommandFixture.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <array>
#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <tuple>

namespace
{
constexpr std::string_view SPECIALTY_MARKER_PREFIX = "new-horizons:creature-line-specialty:";
constexpr std::string_view AUTHORED_SPECIALTY_MARKER = "fixture:authored-creature-specialty";

CreatureID creature(const char * identifier)
{
	return CreatureID(CreatureID::decode(identifier));
}

std::array<int, GameConstants::PRIMARY_SKILLS> primarySkills(const CGHeroInstance & hero)
{
	std::array<int, GameConstants::PRIMARY_SKILLS> result{};
	for(int skill = 0; skill < GameConstants::PRIMARY_SKILLS; ++skill)
		result[skill] = hero.getPrimSkillLevel(PrimarySkill(skill));
	return result;
}

std::set<std::string> specialtyMarkers(const CGHeroInstance & hero)
{
	std::set<std::string> result;
	for(const auto & bonus : hero.getExportedBonusList())
		if(bonus && bonus->stacking.starts_with(SPECIALTY_MARKER_PREFIX))
			result.insert(bonus->stacking);
	return result;
}

size_t specialtyMarkerCount(const CGHeroInstance & hero)
{
	return std::count_if(hero.getExportedBonusList().begin(), hero.getExportedBonusList().end(), [](const auto & bonus)
	{
		return bonus && bonus->stacking.starts_with(SPECIALTY_MARKER_PREFIX);
	});
}

void applyLevelUp(CGameState & state, CGHeroInstance & hero)
{
	HeroLevelUp pack;
	pack.heroId = hero.id;
	GameStatePackVisitor visitor(state);
	pack.visit(visitor);
}

void expectStackDelta(const CStackInstance * specialized, const CStackInstance * control,
	int speed, int initiative, int attack, int defense)
{
	ASSERT_NE(specialized, nullptr);
	ASSERT_NE(control, nullptr);
	EXPECT_EQ(static_cast<int>(specialized->getMovementRange()) - static_cast<int>(control->getMovementRange()), speed);
	EXPECT_EQ(specialized->getInitiative() - control->getInitiative(), initiative);
	EXPECT_EQ(specialized->getAttack(false) - control->getAttack(false), attack);
	EXPECT_EQ(specialized->getDefense(false) - control->getDefense(false), defense);
}
}

class NewHorizonsCreatureSpecialtyTest : public HeroCommandFixture
{
protected:
	bool omitCreatureLineRules = false;

	void SetUp() override
	{
		HeroCommandFixture::SetUp();
		const auto & activeMods = LIBRARY->modh->getActiveMods();
		if(std::find(activeMods.begin(), activeMods.end(), GameConstants::NEW_HORIZONS_MOD_SCOPE) == activeMods.end())
			GTEST_SKIP() << "Requires the activated New Horizons preset";
	}

	void mapLoaded(CMap * loaded) override
	{
		HeroCommandFixture::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(omitCreatureLineRules)
		{
			rules.Struct().erase("creatureLineSpecialties");
			// Map overrides merge over installed defaults. Mark the full loaded
			// rules object as authoritative so the deliberately absent optional
			// snapshot field is not inherited from the base setting.
			rules.setOverrideFlag(true);
		}
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
	}

	void startTowerSpecialtyMap()
	{
		const auto mage = creature("core:mage");
		const auto archMage = creature("core:archMage");
		const auto genie = creature("core:genie");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:theodorus")), PlayerColor(0))
			.heroExperience(0).heroGarrison({{mage, 1}, {archMage, 1}, {genie, 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
			.heroExperience(0).heroGarrison({{mage, 1}, {archMage, 1}, {genie, 1}});
		startWithMap(std::move(builder));
	}

	void startCastleSpecialtyMap()
	{
		const auto archer = creature("core:archer");
		const auto marksman = creature("core:marksman");
		const auto pikeman = creature("core:pikeman");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:valeska")), PlayerColor(0))
			.heroExperience(0).heroGarrison({{archer, 1}, {marksman, 1}, {pikeman, 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(1))
			.heroExperience(0).heroGarrison({{archer, 1}, {marksman, 1}, {pikeman, 1}});
		startWithMap(std::move(builder));
	}
};

TEST_F(NewHorizonsCreatureSpecialtyTest, MageLineRefreshesAtCanonicalThresholdsAndSurvivesSaveAndNextLevel)
{
	startTowerSpecialtyMap();
	auto * theodorus = findHeroAt({5, 5, 0});
	auto * solmyr = findHeroAt({7, 7, 0});
	ASSERT_NE(theodorus, nullptr);
	ASSERT_NE(solmyr, nullptr);
	ASSERT_EQ(theodorus->getHeroType()->getJsonKey(), "core:theodorus");
	ASSERT_EQ(solmyr->getHeroType()->getJsonKey(), "core:solmyr");
	ASSERT_TRUE(theodorus->getHeroType()->creatureLineSpecialtyAlias);
	ASSERT_EQ(theodorus->getHeroType()->creatureLineSpecialtyAlias->creature, creature("core:mage"));
	EXPECT_FALSE(solmyr->getHeroType()->creatureLineSpecialtyAlias);
	ASSERT_EQ(theodorus->level, 1);
	ASSERT_EQ(solmyr->level, 1);

	const auto originalSpecialtyBonuses = theodorus->getHeroType()->specialty;
	std::vector<std::tuple<const Bonus *, int, std::string>> prototypeSnapshot;
	for(const auto & bonus : originalSpecialtyBonuses)
		prototypeSnapshot.emplace_back(bonus.get(), bonus->val, bonus->stacking);

	// A separately authored creature-limited bonus must survive canonical refreshes.
	auto authored = std::make_shared<Bonus>(BonusDuration::PERMANENT, BonusType::PRIMARY_SKILL,
		BonusSource::HERO_SPECIAL, 2, BonusSourceID(theodorus->getHeroTypeID()), BonusSubtypeID(PrimarySkill::ATTACK));
	authored->stacking = std::string(AUTHORED_SPECIALTY_MARKER);
	auto limiter = std::make_shared<CCreatureTypeLimiter>();
	limiter->setCreature(creature("core:mage"));
	authored->limiter = limiter;
	theodorus->addNewBonus(authored);

	const auto basePrimary = primarySkills(*theodorus);
	const auto controlPrimary = primarySkills(*solmyr);
	ASSERT_EQ(basePrimary, controlPrimary);
	const auto levelInvariants = [&](int level)
	{
		ASSERT_EQ(theodorus->level, level);
		ASSERT_EQ(solmyr->level, level);
		EXPECT_EQ(primarySkills(*theodorus), basePrimary) << "specialty must not change hero Primary Attributes";
		EXPECT_EQ(primarySkills(*solmyr), controlPrimary);
		const int attributeBonus = std::min(6, level / 5);
		expectStackDelta(theodorus->getStackPtr(SlotID(0)), solmyr->getStackPtr(SlotID(0)),
			1, 1, attributeBonus + 2, attributeBonus);
		expectStackDelta(theodorus->getStackPtr(SlotID(1)), solmyr->getStackPtr(SlotID(1)),
			1, 1, attributeBonus, attributeBonus);
		expectStackDelta(theodorus->getStackPtr(SlotID(2)), solmyr->getStackPtr(SlotID(2)),
			0, 0, 0, 0);
		EXPECT_EQ(std::count_if(theodorus->getExportedBonusList().begin(), theodorus->getExportedBonusList().end(), [](const auto & bonus)
		{
			return bonus && bonus->stacking == AUTHORED_SPECIALTY_MARKER;
		}), 1);
		const auto markers = specialtyMarkers(*theodorus);
		EXPECT_EQ(markers.size(), specialtyMarkerCount(*theodorus)) << "refresh must not duplicate marker bonuses";
	};

	for(int level = 1; level <= 35; ++level)
	{
		if(level == 1 || level == 4 || level == 5 || level == 9 || level == 10 || level == 29 || level == 30 || level == 35)
			levelInvariants(level);

		if(level == 4)
		{
			CMemorySerializer memory;
			memory.oser & *gameState();
			CGameState restored;
			memory.iser.cb = &restored;
			memory.iser.loadingGamestate = true;
			memory.iser & restored;
			auto * loadedTheodorus = restored.getHero(theodorus->id);
			auto * loadedSolmyr = restored.getHero(solmyr->id);
			ASSERT_NE(loadedTheodorus, nullptr);
			ASSERT_NE(loadedSolmyr, nullptr);
			EXPECT_TRUE(newHorizonsHeroes::creatureLineSpecialtyRules(loadedTheodorus->getPrimaryGrowthRules()));
			EXPECT_EQ(specialtyMarkers(*loadedTheodorus).size(), specialtyMarkerCount(*loadedTheodorus));
			expectStackDelta(loadedTheodorus->getStackPtr(SlotID(0)), loadedSolmyr->getStackPtr(SlotID(0)),
				1, 1, 2, 0);
			applyLevelUp(restored, *loadedTheodorus);
			applyLevelUp(restored, *loadedSolmyr);
			EXPECT_EQ(loadedTheodorus->level, 5);
			expectStackDelta(loadedTheodorus->getStackPtr(SlotID(0)), loadedSolmyr->getStackPtr(SlotID(0)),
				1, 1, 3, 1);
		}

		if(level == 35)
			break;
		applyLevelUp(*gameState(), *theodorus);
		applyLevelUp(*gameState(), *solmyr);
	}

	ASSERT_EQ(theodorus->getHeroType()->specialty.size(), prototypeSnapshot.size());
	for(size_t i = 0; i < prototypeSnapshot.size(); ++i)
	{
		EXPECT_EQ(theodorus->getHeroType()->specialty[i].get(), std::get<0>(prototypeSnapshot[i]));
		EXPECT_EQ(theodorus->getHeroType()->specialty[i]->val, std::get<1>(prototypeSnapshot[i]));
		EXPECT_EQ(theodorus->getHeroType()->specialty[i]->stacking, std::get<2>(prototypeSnapshot[i]));
	}
}

TEST_F(NewHorizonsCreatureSpecialtyTest, ArcherLineSpeedFallbackAddsInitiativeOnlyOnce)
{
	startCastleSpecialtyMap();
	auto * valeska = findHeroAt({5, 5, 0});
	auto * orrin = findHeroAt({7, 7, 0});
	ASSERT_NE(valeska, nullptr);
	ASSERT_NE(orrin, nullptr);
	ASSERT_TRUE(valeska->getHeroType()->creatureLineSpecialtyAlias);
	ASSERT_EQ(valeska->getHeroType()->creatureLineSpecialtyAlias->creature, creature("core:archer"));
	ASSERT_FALSE(valeska->getStackPtr(SlotID(0))->hasBonusOfType(BonusType::STACKS_INITIATIVE_BASE));
	ASSERT_FALSE(valeska->getStackPtr(SlotID(1))->hasBonusOfType(BonusType::STACKS_INITIATIVE_BASE));
	ASSERT_EQ(valeska->level, 1);
	applyLevelUp(*gameState(), *valeska);
	applyLevelUp(*gameState(), *orrin);
	ASSERT_EQ(valeska->level, 2);
	expectStackDelta(valeska->getStackPtr(SlotID(0)), orrin->getStackPtr(SlotID(0)), 1, 1, 0, 0);
	expectStackDelta(valeska->getStackPtr(SlotID(1)), orrin->getStackPtr(SlotID(1)), 1, 1, 0, 0);
	expectStackDelta(valeska->getStackPtr(SlotID(2)), orrin->getStackPtr(SlotID(2)), 0, 0, 0, 0);
	EXPECT_TRUE(newHorizonsHeroes::creatureLineSpecialtyRules(valeska->getPrimaryGrowthRules()));
}

TEST_F(NewHorizonsCreatureSpecialtyTest, MissingSavedCreatureLineRulesRetainLegacySpecialty)
{
	omitCreatureLineRules = true;
	startCastleSpecialtyMap();
	EXPECT_FALSE(gameState()->getHeroDevelopmentRules().Struct().contains("creatureLineSpecialties"));
	auto * valeska = findHeroAt({5, 5, 0});
	ASSERT_NE(valeska, nullptr);
	ASSERT_TRUE(valeska->getHeroType()->creatureLineSpecialtyAlias);
	EXPECT_FALSE(newHorizonsHeroes::creatureLineSpecialtyRules(valeska->getPrimaryGrowthRules()));
	EXPECT_EQ(valeska->getSpecialtyDescriptionTranslated(), valeska->getHeroType()->getSpecialtyDescriptionTranslated());
	EXPECT_TRUE(specialtyMarkers(*valeska).empty());
	for(const auto & legacyBonus : valeska->getHeroType()->creatureLineSpecialtyAlias->bonuses)
		EXPECT_TRUE(std::any_of(valeska->getExportedBonusList().begin(), valeska->getExportedBonusList().end(),
			[&legacyBonus](const auto & bonus) { return bonus == legacyBonus; }));
}
