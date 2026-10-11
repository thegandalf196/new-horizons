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
#include "../../../client/windows/HeroSpecialtyPresentation.h"

#include "../../../lib/GameConstants.h"
#include "../../../lib/GameSettings.h"
#include "../../../lib/CCreatureHandler.h"
#include "../../../lib/bonuses/Bonus.h"
#include "../../../lib/bonuses/Limiters.h"
#include "../../../lib/entities/hero/CHero.h"
#include "../../../lib/entities/hero/CHeroHandler.h"
#include "../../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../../lib/gameState/GameStatePackVisitor.h"
#include "../../../lib/mapObjects/CGHeroInstance.h"
#include "../../../lib/modding/CModHandler.h"
#include "../../../lib/networkPacks/PacksForClient.h"
#include "../../../lib/spells/CSpellHandler.h"
#include "FullGameSnapshotTypes.h"
#include "../../../lib/callback/GameRandomizer.h"
#include "../../../lib/campaign/CampaignState.h"
#include "../../../lib/mapping/CMapInfo.h"
#include "../../../lib/serializer/CMemorySerializer.h"

#include <array>
#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#ifdef ENABLE_BATTLE_AI
#include "../../../AI/BattleAI/StackWithBonuses.h"
#include "../../../lib/battle/CPlayerBattleCallback.h"
#endif

namespace
{
constexpr std::string_view SPECIALTY_MARKER_PREFIX = "new-horizons:creature-line-specialty:";
constexpr std::string_view AUTHORED_SPECIALTY_MARKER = "fixture:authored-creature-specialty";

class FixtureHeroHandler : public CHeroHandler
{
public:
	using CHeroHandler::loadFromJson;
};

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
		// Accepted UP304 fixtures intentionally retain the historical Psychic target.
		rules.Struct().erase("defaultCreatureLineReplacements");
		rules.setOverrideFlag(true);
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

	void startFixedConfluxSpecialtyMap()
	{
		const auto psychic = creature("core:psychicElemental");
		const auto magic = creature("core:magicElemental");
		const auto air = creature("core:airElemental");
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:pasis")), PlayerColor(0))
			.heroExperience(0).heroGarrison({{psychic, 1}, {magic, 1}, {air, 1}})
			.hero({6, 5, 0}, HeroTypeID(HeroTypeID::decode("core:monere")), PlayerColor(0))
			.heroExperience(0).heroGarrison({{psychic, 1}, {magic, 1}, {air, 1}})
			.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
			.heroExperience(0).heroGarrison({{psychic, 1}, {magic, 1}, {air, 1}});
		startWithMap(std::move(builder));
	}

	void expectFixedConfluxLine(const CGHeroInstance & hero, const CGHeroInstance & control, bool canonical)
	{
		const int attribute = canonical ? std::min<ui32>(6, hero.level / 5) : 3;
		for(int slot : {0, 1})
			expectStackDelta(hero.getStackPtr(SlotID(slot)), control.getStackPtr(SlotID(slot)),
				canonical ? 1 : 0, canonical ? 1 : 0, attribute, attribute);
		expectStackDelta(hero.getStackPtr(SlotID(2)), control.getStackPtr(SlotID(2)), 0, 0, 0, 0);
		EXPECT_EQ(specialtyMarkerCount(hero), canonical ? 4 : 0);
		EXPECT_EQ(specialtyMarkers(hero).size(), specialtyMarkerCount(hero));
	}
};

TEST_F(NewHorizonsCreatureSpecialtyTest, FixedConfluxPackagesConvertWithoutStackingAndSurviveSave)
{
	startFixedConfluxSpecialtyMap();
	auto * pasis = findHeroAt({5, 5, 0});
	auto * monere = findHeroAt({6, 5, 0});
	auto * control = findHeroAt({7, 7, 0});
	ASSERT_NE(pasis, nullptr);
	ASSERT_NE(monere, nullptr);
	ASSERT_NE(control, nullptr);
	const auto originalPasis = pasis->getHeroType()->specialty;
	const auto originalMonere = monere->getHeroType()->specialty;
	for(const auto * hero : {pasis, monere})
	{
		ASSERT_TRUE(hero->getHeroType()->creatureLineSpecialtyAlias);
		const auto & provenance = *hero->getHeroType()->creatureLineSpecialtyAlias;
		EXPECT_EQ(provenance.creature, creature("core:psychicElemental"));
		ASSERT_EQ(provenance.bonuses.size(), 2);
		for(const auto & original : provenance.bonuses)
		{
			EXPECT_EQ(original->type, BonusType::PRIMARY_SKILL);
			EXPECT_EQ(original->val, 3);
			EXPECT_TRUE(std::any_of(hero->getHeroType()->specialty.begin(), hero->getHeroType()->specialty.end(),
				[&original](const auto & bonus) { return bonus == original; }));
			EXPECT_FALSE(std::any_of(hero->getExportedBonusList().begin(), hero->getExportedBonusList().end(),
				[&original](const auto & bonus) { return bonus == original; }));
		}
	}
	for(int level = 1; level <= 35; ++level)
	{
		ASSERT_EQ(pasis->level, level);
		ASSERT_EQ(monere->level, level);
		if(level == 1 || level == 5 || level == 30 || level == 35)
			for(const auto * hero : {pasis, monere})
				expectFixedConfluxLine(*hero, *control, true);
		if(level == 4)
		{
			CMemorySerializer memory;
			memory.oser & *gameState();
			CGameState restored;
			memory.iser.cb = &restored;
			memory.iser.loadingGamestate = true;
			memory.iser & restored;
			auto * loadedControl = restored.getHero(control->id);
			ASSERT_NE(loadedControl, nullptr);
			applyLevelUp(restored, *loadedControl);
			for(const auto * hero : {pasis, monere})
			{
				auto * loaded = restored.getHero(hero->id);
				ASSERT_NE(loaded, nullptr);
				EXPECT_EQ(specialtyMarkers(*loaded), specialtyMarkers(*hero));
				applyLevelUp(restored, *loaded);
				ASSERT_EQ(loaded->level, 5);
				expectFixedConfluxLine(*loaded, *loadedControl, true);
			}
		}
		if(level == 35)
			break;
		for(auto * hero : {pasis, monere, control})
			applyLevelUp(*gameState(), *hero);
	}
	for(const auto & entry : {std::make_pair(pasis, originalPasis), std::make_pair(monere, originalMonere)})
	{
		ASSERT_EQ(entry.first->getHeroType()->specialty.size(), entry.second.size());
		for(size_t i = 0; i < entry.second.size(); ++i)
		{
			EXPECT_EQ(entry.first->getHeroType()->specialty[i], entry.second[i]);
			EXPECT_EQ(entry.second[i]->val, 3);
			EXPECT_TRUE(entry.second[i]->stacking.empty());
		}
	}
}

TEST_F(NewHorizonsCreatureSpecialtyTest, FixedConfluxMissingRulesKeepOriginalFlatPackageAcrossSaveAndLevels)
{
	omitCreatureLineRules = true;
	startFixedConfluxSpecialtyMap();
	auto * pasis = findHeroAt({5, 5, 0});
	auto * monere = findHeroAt({6, 5, 0});
	auto * control = findHeroAt({7, 7, 0});
	ASSERT_NE(pasis, nullptr);
	ASSERT_NE(monere, nullptr);
	ASSERT_NE(control, nullptr);
	for(const auto * hero : {pasis, monere})
	{
		ASSERT_TRUE(hero->getHeroType()->creatureLineSpecialtyAlias);
		EXPECT_FALSE(newHorizonsHeroes::creatureLineSpecialtyRules(hero->getPrimaryGrowthRules()));
		for(const auto & original : hero->getHeroType()->creatureLineSpecialtyAlias->bonuses)
			EXPECT_TRUE(std::any_of(hero->getExportedBonusList().begin(), hero->getExportedBonusList().end(),
				[&original](const auto & bonus) { return bonus == original; }));
	}
	for(int level = 1; level <= 35; ++level)
	{
		if(level == 1 || level == 5 || level == 30 || level == 35)
			for(const auto * hero : {pasis, monere})
				expectFixedConfluxLine(*hero, *control, false);
		if(level == 4)
		{
			CMemorySerializer memory;
			memory.oser & *gameState();
			CGameState restored;
			memory.iser.cb = &restored;
			memory.iser.loadingGamestate = true;
			memory.iser & restored;
			auto * loadedControl = restored.getHero(control->id);
			ASSERT_NE(loadedControl, nullptr);
			applyLevelUp(restored, *loadedControl);
			for(const auto * hero : {pasis, monere})
			{
				auto * loaded = restored.getHero(hero->id);
				ASSERT_NE(loaded, nullptr);
				EXPECT_FALSE(newHorizonsHeroes::creatureLineSpecialtyRules(loaded->getPrimaryGrowthRules()));
				applyLevelUp(restored, *loaded);
				expectFixedConfluxLine(*loaded, *loadedControl, false);
			}
		}
		if(level == 35)
			break;
		for(auto * hero : {pasis, monere, control})
			applyLevelUp(*gameState(), *hero);
	}
}

TEST_F(NewHorizonsCreatureSpecialtyTest, FixedConfluxProvenanceRejectsAmbiguousOrMissingNames)
{
	JsonNode node;
	auto & specialty = node["specialty"];
	specialty["bonuses"]["attack"]["type"].String() = "PRIMARY_SKILL";
	specialty["creatureLineConversion"]["creature"].String() = "core:psychicElemental";
	specialty["creatureLineConversion"]["bonuses"].Vector().push_back(JsonNode("attack"));
	FixtureHeroHandler handler;
	auto ambiguous = node;
	ambiguous["specialty"]["creature"].String() = "core:psychicElemental";
	EXPECT_THROW(handler.loadFromJson("new-horizons", ambiguous, "fixtureAmbiguousLine", 0), std::runtime_error);
	auto duplicate = node;
	duplicate["specialty"]["creatureLineConversion"]["bonuses"].Vector().push_back(JsonNode("attack"));
	EXPECT_THROW(handler.loadFromJson("new-horizons", duplicate, "fixtureDuplicateLine", 0), std::runtime_error);
	auto missing = node;
	missing["specialty"]["creatureLineConversion"]["bonuses"].Vector().push_back(JsonNode("missing"));
	EXPECT_THROW(handler.loadFromJson("new-horizons", missing, "fixtureMissingLine", 0), std::runtime_error);
	auto empty = node;
	empty["specialty"]["creatureLineConversion"]["bonuses"].Vector().clear();
	EXPECT_THROW(handler.loadFromJson("new-horizons", empty, "fixtureEmptyList", 0), std::runtime_error);
	for(const auto & malformed : {JsonNode(""), JsonNode(true), JsonNode(7)})
	{
		auto invalidName = node;
		invalidName["specialty"]["creatureLineConversion"]["bonuses"].Vector().push_back(malformed);
		EXPECT_THROW(handler.loadFromJson("new-horizons", invalidName, "fixtureInvalidName", 0), std::runtime_error);
		auto invalidCreature = node;
		invalidCreature["specialty"]["creatureLineConversion"]["creature"] = malformed;
		EXPECT_THROW(handler.loadFromJson("new-horizons", invalidCreature, "fixtureInvalidCreature", 0), std::runtime_error);
	}
	for(const auto & malformed : {JsonNode(true), JsonNode(7), JsonNode("invalid"), JsonNode()})
	{
		auto invalidMap = node;
		invalidMap["specialty"]["bonuses"] = malformed;
		EXPECT_THROW(handler.loadFromJson("new-horizons", invalidMap, "fixtureInvalidBonusMap", 0), std::runtime_error);
		auto invalidList = node;
		invalidList["specialty"]["creatureLineConversion"]["bonuses"] = malformed;
		EXPECT_THROW(handler.loadFromJson("new-horizons", invalidList, "fixtureInvalidList", 0), std::runtime_error);
		auto invalidBonus = node;
		invalidBonus["specialty"]["bonuses"]["attack"] = malformed;
		EXPECT_THROW(handler.loadFromJson("new-horizons", invalidBonus, "fixtureInvalidBonus", 0), std::runtime_error);
		if(!malformed.isNull())
		{
			auto invalidDeclaration = node;
			invalidDeclaration["specialty"]["creatureLineConversion"] = malformed;
			EXPECT_THROW(handler.loadFromJson("new-horizons", invalidDeclaration, "fixtureInvalidDeclaration", 0),
				std::runtime_error);
		}
	}
}

TEST_F(NewHorizonsCreatureSpecialtyTest, FixedConfluxUnmarkedSavedPackagesAreNotConvertedByCurrentRules)
{
	startFixedConfluxSpecialtyMap();
	auto * pasis = findHeroAt({5, 5, 0});
	auto * monere = findHeroAt({6, 5, 0});
	auto * control = findHeroAt({7, 7, 0});
	ASSERT_NE(pasis, nullptr);
	ASSERT_NE(monere, nullptr);
	ASSERT_NE(control, nullptr);
	// Simulate the exported package of a hero saved before this producer was
	// annotated, while retaining the installed canonical development rules.
	for(auto * hero : {pasis, monere})
	{
		ASSERT_TRUE(newHorizonsHeroes::creatureLineSpecialtyRules(hero->getPrimaryGrowthRules()));
		ASSERT_TRUE(hero->getHeroType()->creatureLineSpecialtyAlias);
		hero->removeBonuses(CSelector([](const Bonus * bonus)
		{
			return bonus && bonus->stacking.starts_with(SPECIALTY_MARKER_PREFIX);
		}));
		for(const auto & original : hero->getHeroType()->creatureLineSpecialtyAlias->bonuses)
			hero->addNewBonus(original);
		expectFixedConfluxLine(*hero, *control, false);
	}
	CMemorySerializer memory;
	memory.oser & *gameState();
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	memory.iser & restored;
	auto * loadedControl = restored.getHero(control->id);
	ASSERT_NE(loadedControl, nullptr);
	for(int level = 1; level <= 35; ++level)
	{
		for(const auto * hero : {pasis, monere})
		{
			auto * loaded = restored.getHero(hero->id);
			ASSERT_NE(loaded, nullptr);
			ASSERT_EQ(loaded->level, level);
			ASSERT_TRUE(newHorizonsHeroes::creatureLineSpecialtyRules(loaded->getPrimaryGrowthRules()));
			if(level == 1 || level == 5 || level == 30 || level == 35)
				expectFixedConfluxLine(*loaded, *loadedControl, false);
			if(level != 35)
				applyLevelUp(restored, *loaded);
		}
		if(level != 35)
			applyLevelUp(restored, *loadedControl);
	}
}

namespace
{
struct FixedCreaturePackage
{
	const char * hero;
	const char * creature;
	const char * upgrade;
	int attack;
	int defense;
	int damage;
	int speed;
	int bonusCount;
};
}

class NewHorizonsFixedCreaturePackageTest : public NewHorizonsCreatureSpecialtyTest,
	public ::testing::WithParamInterface<std::tuple<FixedCreaturePackage, bool>>
{
};

TEST_P(NewHorizonsFixedCreaturePackageTest, ActualPackageCanonicalAndLegacyThresholdsAndSave)
{
	const auto & [package, canonical] = GetParam();
	omitCreatureLineRules = !canonical;
	const auto base = creature(package.creature);
	const auto upgraded = creature(package.upgrade);
	const auto unrelated = creature("core:airElemental");
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(package.hero)), PlayerColor(0))
		.heroExperience(0).heroGarrison({{base, 1}, {upgraded, 1}, {unrelated, 1}})
		.hero({7, 7, 0}, HeroTypeID(HeroTypeID::decode("core:solmyr")), PlayerColor(1))
		.heroExperience(0).heroGarrison({{base, 1}, {upgraded, 1}, {unrelated, 1}});
	startWithMap(std::move(builder));
	auto * hero = findHeroAt({5, 5, 0});
	auto * control = findHeroAt({7, 7, 0});
	ASSERT_NE(hero, nullptr);
	ASSERT_NE(control, nullptr);
	ASSERT_EQ(hero->getHeroType()->getJsonKey(), package.hero);
	ASSERT_TRUE(hero->getHeroType()->creatureLineSpecialtyAlias);
	const auto & provenance = *hero->getHeroType()->creatureLineSpecialtyAlias;
	ASSERT_EQ(provenance.creature, base);
	ASSERT_EQ(provenance.bonuses.size(), package.bonusCount);
	const auto originalPackage = hero->getHeroType()->specialty;
	std::vector<std::tuple<const Bonus *, int, std::string>> originalSnapshot;
	for(const auto & bonus : originalPackage)
		originalSnapshot.emplace_back(bonus.get(), bonus->val, bonus->stacking);
	for(const auto & original : provenance.bonuses)
		EXPECT_EQ(std::any_of(hero->getExportedBonusList().begin(), hero->getExportedBonusList().end(),
			[&original](const auto & bonus) { return bonus == original; }), !canonical);

	const auto check = [&](const CGHeroInstance & specialized, const CGHeroInstance & ordinary)
	{
		const int attribute = std::min<ui32>(6, specialized.level / 5);
		for(int slot : {0, 1, 2})
		{
			const auto * stack = specialized.getStackPtr(SlotID(slot));
			const auto * controlStack = ordinary.getStackPtr(SlotID(slot));
			ASSERT_NE(stack, nullptr);
			ASSERT_NE(controlStack, nullptr);
			const bool affected = slot != 2;
			const int legacyInitiative = stack->hasBonusOfType(BonusType::STACKS_INITIATIVE_BASE) ? 0 : package.speed;
			expectStackDelta(stack, controlStack,
				affected ? (canonical ? 1 : package.speed) : 0,
				affected ? (canonical ? 1 : legacyInitiative) : 0,
				affected ? (canonical ? attribute : package.attack) : 0,
				affected ? (canonical ? attribute : package.defense) : 0);
			EXPECT_EQ(stack->valOfBonuses(BonusType::CREATURE_DAMAGE) - controlStack->valOfBonuses(BonusType::CREATURE_DAMAGE),
				affected && !canonical ? package.damage : 0);
		}
		EXPECT_EQ(specialtyMarkerCount(specialized), canonical ? 4 : 0);
		EXPECT_EQ(specialtyMarkers(specialized).size(), specialtyMarkerCount(specialized));
	};
	for(int level = 1; level <= 35; ++level)
	{
		ASSERT_EQ(hero->level, level);
		if(level == 1 || level == 5 || level == 30 || level == 35)
			check(*hero, *control);
		if(level == 4)
		{
			CMemorySerializer memory;
			memory.oser & *gameState();
			CGameState restored;
			memory.iser.cb = &restored;
			memory.iser.loadingGamestate = true;
			memory.iser & restored;
			auto * loaded = restored.getHero(hero->id);
			auto * loadedControl = restored.getHero(control->id);
			ASSERT_NE(loaded, nullptr);
			ASSERT_NE(loadedControl, nullptr);
			EXPECT_EQ(specialtyMarkers(*loaded), specialtyMarkers(*hero));
			EXPECT_EQ(newHorizonsHeroes::creatureLineSpecialtyRules(loaded->getPrimaryGrowthRules()).has_value(), canonical);
			applyLevelUp(restored, *loaded);
			applyLevelUp(restored, *loadedControl);
			ASSERT_EQ(loaded->level, 5);
			check(*loaded, *loadedControl);
		}
		if(level == 35)
			break;
		applyLevelUp(*gameState(), *hero);
		applyLevelUp(*gameState(), *control);
	}
	ASSERT_EQ(hero->getHeroType()->specialty.size(), originalSnapshot.size());
	for(size_t i = 0; i < originalSnapshot.size(); ++i)
	{
		EXPECT_EQ(hero->getHeroType()->specialty[i].get(), std::get<0>(originalSnapshot[i]));
		EXPECT_EQ(hero->getHeroType()->specialty[i]->val, std::get<1>(originalSnapshot[i]));
		EXPECT_EQ(hero->getHeroType()->specialty[i]->stacking, std::get<2>(originalSnapshot[i]));
	}
}

INSTANTIATE_TEST_SUITE_P(SingleRootPackages, NewHorizonsFixedCreaturePackageTest,
	::testing::Combine(::testing::Values(
		FixedCreaturePackage{"core:pasis", "core:psychicElemental", "core:magicElemental", 3, 3, 0, 0, 2},
		FixedCreaturePackage{"core:monere", "core:psychicElemental", "core:magicElemental", 3, 3, 0, 0, 2},
		FixedCreaturePackage{"core:lacus", "core:waterElemental", "core:iceElemental", 2, 0, 0, 0, 1},
		FixedCreaturePackage{"core:kalt", "core:waterElemental", "core:iceElemental", 2, 0, 0, 0, 1},
		FixedCreaturePackage{"core:thunar", "core:earthElemental", "core:magmaElemental", 2, 1, 5, 0, 3},
		FixedCreaturePackage{"core:erdamon", "core:earthElemental", "core:magmaElemental", 2, 1, 5, 0, 3},
		FixedCreaturePackage{"core:ignissa", "core:fireElemental", "core:energyElemental", 1, 2, 2, 0, 3},
		FixedCreaturePackage{"core:fiur", "core:fireElemental", "core:energyElemental", 1, 2, 2, 0, 3},
		FixedCreaturePackage{"core:kilgor", "core:behemoth", "core:ancientBehemoth", 5, 5, 10, 0, 3},
		FixedCreaturePackage{"core:undeadHaart", "core:blackKnight", "core:dreadKnight", 5, 5, 10, 0, 3},
		FixedCreaturePackage{"core:xeron", "core:devil", "core:archDevil", 4, 2, 0, 1, 3}), ::testing::Bool()));

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

class NewHorizonsWispSpecialtyTest : public NewHorizonsCreatureSpecialtyTest
{
protected:
	bool absent = false;
	bool legacy = false;
	bool preset = false;
	bool previousSerializationContext = false;
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsCreatureSpecialtyTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		ASSERT_EQ(rules["defaultCreatureLineReplacements"]["core:pasis"].String(), "new-horizons:wisp");
		ASSERT_EQ(rules["defaultCreatureLineReplacements"]["core:monere"].String(), "new-horizons:wisp");
		if(absent) rules.Struct().erase("defaultCreatureLineReplacements");
		if(legacy) rules = JsonNode();
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		if(previousSerializationContext)
		{
			// This positive writer control predates the later canonical-spell
			// clauses. Keep the v3 policy, but do not capture those future keys.
			JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
			magic["spells"]["core:implosion"].Struct().erase("implosion");
			magic["spells"]["core:teleport"].Struct().erase("ignoreInterveningBarriers");
			magic["spells"]["core:dispel"].Struct().erase("temporaryMagicalEffectsOnly");
			magic["spells"]["core:curse"].Struct().erase("schoolRankDurations");
			magic["spells"]["core:fireWall"].Struct().erase("burnGroundedFlyers");
			magic.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
		}
		if(legacy)
		{
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, JsonNode());
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		}
	}
	void prepare()
	{
		const std::vector<std::pair<CreatureID, uint16_t>> army = {
			{creature("new-horizons:wisp"), 1}, {creature("new-horizons:wispUpgrade"), 1},
			{creature("core:psychicElemental"), 1}, {creature("core:magicElemental"), 1},
			{creature("core:airElemental"), 1}};
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36, false).playerActive(PlayerColor(0)).playerActive(PlayerColor(1));
		for(const auto & [name, position, owner] : std::array{
			std::tuple{"core:pasis", int3(5, 5, 0), PlayerColor(0)},
			std::tuple{"core:monere", int3(6, 5, 0), PlayerColor(0)},
			std::tuple{"core:solmyr", int3(7, 7, 0), PlayerColor(1)}})
		{
			builder.hero(position, HeroTypeID(HeroTypeID::decode(name)), owner)
				.heroExperience(0).heroGarrison({{creature("core:pikeman"), 1}});
			if(preset && owner == PlayerColor(0))
				builder.heroSecondarySkills({{SecondarySkill::LOGISTICS, MasteryLevel::BASIC}});
		}
		startWithMap(std::move(builder));
		// SOD can encode only stock creature IDs; install resolved module creatures
		// through the ordinary runtime army API after the authored hero is loaded.
		for(auto * hero : {pasis(), monere(), control()})
		{
			ASSERT_NE(hero, nullptr);
			hero->clearSlots();
			for(size_t index = 0; index < army.size(); ++index)
			{
				ASSERT_TRUE(hero->setCreature(SlotID(static_cast<int>(index)), army[index].first, army[index].second));
				const auto * stack = hero->getStackPtr(SlotID(static_cast<int>(index)));
				ASSERT_NE(stack, nullptr);
				ASSERT_EQ(stack->getCreatureID(), army[index].first);
				ASSERT_EQ(stack->getCount(), army[index].second);
			}
		}
		// Initialize the same real recording server/handler as BattleTestFixture,
		// without its makeNeutral step, which would erase the specialties under test.
		server.gameState = gameState();
		gameHandler = std::make_shared<CGameHandler>(server, gameState());
		gameHandler->randomizer->setSeed(seed);
	}
	CGHeroInstance * pasis() { return findHeroAt({5, 5, 0}); }
	CGHeroInstance * monere() { return findHeroAt({6, 5, 0}); }
	CGHeroInstance * control() { return findHeroAt({7, 7, 0}); }
	void expectLine(CGHeroInstance & hero, CGHeroInstance & reference, bool wisp, bool oldMode = false)
	{
		const int attribute = oldMode ? 3 : std::min<ui32>(6, hero.level / 5);
		// Original-mode stacks include their native hero primary attributes;
		// retain those real heroes and account for that unrelated contribution.
		const int primaryAttack = oldMode ? hero.getPrimSkillLevel(PrimarySkill::ATTACK)
			- reference.getPrimSkillLevel(PrimarySkill::ATTACK) : 0;
		const int primaryDefense = oldMode ? hero.getPrimSkillLevel(PrimarySkill::DEFENSE)
			- reference.getPrimSkillLevel(PrimarySkill::DEFENSE) : 0;
		for(int slot = 0; slot < 5; ++slot)
		{
			const bool selected = wisp ? slot < 2 : slot == 2 || slot == 3;
			expectStackDelta(hero.getStackPtr(SlotID(slot)), reference.getStackPtr(SlotID(slot)),
				selected && !oldMode ? 1 : 0, selected && !oldMode ? 1 : 0,
				(selected ? attribute : 0) + primaryAttack, (selected ? attribute : 0) + primaryDefense);
		}
	}
};

TEST_F(NewHorizonsWispSpecialtyTest, FreshDefaultsBothEnrollRealWispAndUpgradeNotPsychicAliases)
{
	prepare();
	for(auto * hero : {pasis(), monere()})
	{
		ASSERT_NE(hero, nullptr);
		EXPECT_EQ(hero->getCreatureLineSpecialtyTarget(), creature("new-horizons:wisp"));
		EXPECT_EQ(hero->getPrimaryGrowthRules()["creatureLineSpecialtyTarget"].String(), "new-horizons:wisp");
		expectLine(*hero, *control(), true);
		EXPECT_EQ(specialtyMarkerCount(*hero), 4u);
		EXPECT_EQ(specialtyMarkers(*hero).size(), 4u);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, ExplicitMapDevelopmentPreservesAcceptedPsychicTarget)
{
	preset = true;
	prepare();
	for(auto * hero : {pasis(), monere()})
	{
		EXPECT_EQ(hero->getCreatureLineSpecialtyTarget(), creature("core:psychicElemental"));
		EXPECT_FALSE(hero->getPrimaryGrowthRules().Struct().contains("creatureLineSpecialtyTarget"));
		// Explicit map development prevents the default hero profile/specialty
		// replacement, but still uses the captured creation-time skill migration.
		EXPECT_EQ(hero->secSkills, (std::vector<std::pair<SecondarySkill, ui8>>{
			{SecondarySkill(SecondarySkill::decode("new-horizons:logistics")), MasteryLevel::BASIC},
			{SecondarySkill(SecondarySkill::decode("new-horizons:elementalRebirth")), MasteryLevel::BASIC}}));
		expectLine(*hero, *control(), false);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, AbsentCapturedOptInPreservesPsychicAndNeverRecapturesOnInit)
{
	absent = true;
	prepare();
	GameRandomizer randomizer(*gameState());
	for(auto * hero : {pasis(), monere()})
	{
		ASSERT_NO_THROW(hero->initHero(randomizer));
		EXPECT_EQ(hero->getCreatureLineSpecialtyTarget(), creature("core:psychicElemental"));
		EXPECT_FALSE(hero->getPrimaryGrowthRules().Struct().contains("creatureLineSpecialtyTarget"));
		expectLine(*hero, *control(), false);
		EXPECT_EQ(specialtyMarkerCount(*hero), 4u);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, OriginalModeKeepsNativeFixedPsychicBonusesAndPrototype)
{
	legacy = true;
	prepare();
	for(auto * hero : {pasis(), monere()})
	{
		EXPECT_EQ(hero->getCreatureLineSpecialtyTarget(), creature("core:psychicElemental"));
		expectLine(*hero, *control(), false, true);
		EXPECT_TRUE(specialtyMarkers(*hero).empty());
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, RealLevelUpRefreshUsesCapturedWispTargetAndUnchangedCap)
{
	prepare();
	for(int level = 1; level <= 35; ++level)
	{
		for(auto * hero : {pasis(), monere()})
		{
			ASSERT_EQ(hero->level, level);
			expectLine(*hero, *control(), true);
			EXPECT_EQ(specialtyMarkerCount(*hero), 4u);
		}
		if(level < 35)
			for(auto * hero : {pasis(), monere(), control()}) applyLevelUp(*gameState(), *hero);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, WorldReplayAndRepeatedInitRetainTargetWithoutDuplicateMarkers)
{
	prepare();
	for(int level = 1; level < 5; ++level)
		for(auto * hero : {pasis(), monere(), control()}) applyLevelUp(*gameState(), *hero);
	CMemorySerializer memory;
	ASSERT_NO_THROW(memory.oser & *gameState());
	CGameState restored;
	memory.iser.cb = &restored;
	memory.iser.loadingGamestate = true;
	ASSERT_NO_THROW(memory.iser & restored);
	auto * reference = restored.getHero(control()->id);
	ASSERT_NE(reference, nullptr);
	GameRandomizer randomizer(restored);
	for(auto * original : {pasis(), monere()})
	{
		auto * loaded = restored.getHero(original->id);
		ASSERT_NE(loaded, nullptr);
		ASSERT_NO_THROW(loaded->initHero(randomizer));
		ASSERT_NO_THROW(loaded->initHero(randomizer));
		EXPECT_EQ(loaded->getCreatureLineSpecialtyTarget(), creature("new-horizons:wisp"));
		EXPECT_EQ(specialtyMarkers(*loaded), specialtyMarkers(*original));
		EXPECT_EQ(specialtyMarkerCount(*loaded), 4u);
		const auto loadedPresentation = heroSpecialtyPresentation(*loaded);
		const auto originalPresentation = heroSpecialtyPresentation(*original);
		EXPECT_EQ(loadedPresentation.creature, originalPresentation.creature);
		EXPECT_EQ(loadedPresentation.frame(), originalPresentation.frame());
		EXPECT_EQ(loadedPresentation.description, originalPresentation.description);
		expectLine(*loaded, *reference, true);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, DescriptionAndLocalMarkersFollowActualTargetWhilePrototypesStayPsychic)
{
	prepare();
	for(auto * hero : {pasis(), monere()})
	{
		EXPECT_EQ(hero->getHeroType()->creatureLineSpecialtyAlias->creature, creature("core:psychicElemental"));
		ASSERT_EQ(hero->getHeroType()->creatureLineSpecialtyAlias->bonuses.size(), 2u);
		for(const auto & bonus : hero->getHeroType()->creatureLineSpecialtyAlias->bonuses)
			EXPECT_EQ(bonus->val, 3);
		EXPECT_NE(hero->getSpecialtyDescriptionTranslated().find(creature("new-horizons:wisp").toCreature()->getNamePluralTranslated()), std::string::npos);
		for(const auto & marker : specialtyMarkers(*hero))
			EXPECT_NE(marker.find(std::to_string(creature("new-horizons:wisp").getNum())), std::string::npos);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, CurrentDefaultSpecialtyPresentationUsesSelectedWispIconAndCapturedDescription)
{
	prepare();
	const auto wisp = creature("new-horizons:wisp");
	for(auto * hero : {pasis(), monere()})
	{
		const auto presentation = heroSpecialtyPresentation(*hero);
		ASSERT_TRUE(presentation.creature.has_value());
		EXPECT_EQ(*presentation.creature, wisp);
		EXPECT_EQ(presentation.frame(), wisp.toCreature()->getIconIndex());
		EXPECT_EQ(presentation.animation().getOriginalName(), "CPRSMALL");
		EXPECT_EQ(presentation.animation(true).getOriginalName(), "CPRSMALL");
		EXPECT_EQ(presentation.name, wisp.toCreature()->getNamePluralTranslated());
		EXPECT_EQ(presentation.description, hero->getSpecialtyDescriptionTranslated());
		const auto prototype = heroSpecialtyPresentation(*hero->getHeroType(), hero->getPrimaryGrowthRules(),
			newHorizonsHeroes::defaultCreatureLineTarget(hero->getPrimaryGrowthRules(), hero->getHeroTypeID()));
		EXPECT_EQ(prototype.creature, presentation.creature);
		EXPECT_EQ(prototype.name, presentation.name);
		EXPECT_EQ(prototype.description, presentation.description);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, ExplicitMapAndAbsentLegacyPresentationKeepOriginalPsychicIconAndText)
{
	preset = true;
	prepare();
	for(auto * hero : {pasis(), monere()})
	{
		const auto presentation = heroSpecialtyPresentation(*hero);
		EXPECT_FALSE(presentation.creature.has_value());
		EXPECT_EQ(presentation.frame(), hero->getHeroType()->imageIndex);
		EXPECT_EQ(presentation.animation().getOriginalName(), "UN44");
		EXPECT_EQ(presentation.animation(true).getOriginalName(), "UN32");
		EXPECT_EQ(presentation.name, hero->getHeroType()->getSpecialtyNameTranslated());
		EXPECT_EQ(presentation.description, hero->getSpecialtyDescriptionTranslated());
		const auto legacy = heroSpecialtyPresentation(*hero->getHeroType(), JsonNode(), std::nullopt);
		EXPECT_FALSE(legacy.creature.has_value());
		EXPECT_EQ(legacy.frame(), presentation.frame());
		EXPECT_EQ(legacy.name, presentation.name);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, AllActualOuterWritersRejectNewTargetBeforeAnyPrefix)
{
	prepare();
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	for(int kind = 0; kind < 4; ++kind)
	{
		CMemorySerializer memory;
		memory.oser.version = ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS;
		if(kind == 0) EXPECT_THROW(memory.oser & *pasis(), std::runtime_error);
		if(kind == 1) EXPECT_THROW(memory.oser & *map(), std::runtime_error);
		if(kind == 2) EXPECT_THROW(memory.oser & *gameState(), std::runtime_error);
		if(kind == 3) EXPECT_THROW(memory.oser & lobby, std::runtime_error);
		EXPECT_TRUE(memory.extractBuffer().empty());
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, MalformedTableAndCapturedTargetRejectWithoutChangingPrototype)
{
	prepare();
	const auto original = pasis()->getHeroType()->creatureLineSpecialtyAlias->creature;
	for(const auto invalid : {JsonNode(), JsonNode(false), JsonNode(1), JsonNode("new-horizons:wisp")})
	{
		auto rules = pasis()->getPrimaryGrowthRules();
		rules["defaultCreatureLineReplacements"] = invalid;
		EXPECT_THROW(newHorizonsHeroes::validateDefaultCreatureLineSerialization(rules, true), std::runtime_error);
		EXPECT_THROW(newHorizonsHeroes::validateDefaultCreatureLineSerialization(rules, false), std::runtime_error);
	}
	auto rules = pasis()->getPrimaryGrowthRules();
	rules["defaultCreatureLineReplacements"].Struct().erase("core:monere");
	EXPECT_THROW(newHorizonsHeroes::validateDefaultCreatureLineSerialization(rules, true), std::runtime_error);
	rules = pasis()->getPrimaryGrowthRules();
	rules["creatureLineSpecialtyTarget"].String() = "core:psychicElemental";
	EXPECT_THROW(newHorizonsHeroes::validateDefaultCreatureLineSerialization(rules, true), std::runtime_error);
	EXPECT_EQ(pasis()->getHeroType()->creatureLineSpecialtyAlias->creature, original);
	auto stale = std::make_shared<Bonus>();
	stale->type = BonusType::STACKS_SPEED;
	stale->source = BonusSource::HERO_SPECIAL;
	stale->sid = BonusSourceID(pasis()->getHeroTypeID());
	stale->stacking = std::string(SPECIALTY_MARKER_PREFIX) + std::to_string(pasis()->getHeroTypeID().getNum())
		+ ":" + std::to_string(original.getNum()) + ":speed";
	pasis()->addNewBonus(stale);
	CMemorySerializer invalidSnapshot;
	EXPECT_THROW(invalidSnapshot.oser & *pasis(), std::runtime_error);
	EXPECT_TRUE(invalidSnapshot.extractBuffer().empty());
}

TEST_F(NewHorizonsWispSpecialtyTest, RawOldReaderRejectsPresenceAndAbsentPreviousContextRemainsWritable)
{
	JsonNode raw;
	raw["heroes"]["newHorizons"]["defaultCreatureLineReplacements"]["core:pasis"].String() = "new-horizons:wisp";
	raw["heroes"]["newHorizons"]["defaultCreatureLineReplacements"]["core:monere"].String() = "new-horizons:wisp";
	const auto previous = ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS;
	GameSettings authored;
	authored.loadOverrides(raw);
	CMemorySerializer currentRaw;
	ASSERT_NO_THROW(currentRaw.oser & authored);
	JsonNode emitted;
	ASSERT_NO_THROW(currentRaw.iser & emitted);
	// The ordinary writer materializes these absent null guard branches in its
	// local override copy; compare their exact wire shape, not an invented rule.
	JsonNode expected = raw;
	expected["heroes"]["newHorizonsCapabilities"] = JsonNode();
	expected["heroes"]["newHorizonsPerks"] = JsonNode();
	expected["magic"]["newHorizons"] = JsonNode();
	EXPECT_EQ(emitted, expected);
	CMemorySerializer oldWriter;
	oldWriter.oser.version = previous;
	EXPECT_THROW(oldWriter.oser & authored, std::runtime_error);
	EXPECT_TRUE(oldWriter.extractBuffer().empty());
	CMemorySerializer forged;
	forged.oser.version = previous;
	forged.iser.version = previous;
	forged.oser & raw;
	GameSettings untouched;
	EXPECT_THROW(untouched.serialize(forged.iser), std::runtime_error);
	CMemorySerializer current;
	ASSERT_NO_THROW(current.oser & untouched);
	JsonNode unchanged;
	ASSERT_NO_THROW(current.iser & unchanged);
	EXPECT_TRUE(unchanged["heroes"]["newHorizons"].isNull());
	absent = true;
	previousSerializationContext = true;
	prepare();
	EXPECT_FALSE(pasis()->getMagicRules()["spells"]["core:implosion"].Struct().contains("implosion"));
	CMemorySerializer old;
	old.oser.version = previous;
	ASSERT_NO_THROW(old.oser & *pasis());
	EXPECT_FALSE(old.extractBuffer().empty());
}

TEST_F(NewHorizonsWispSpecialtyTest, PublicCrossoverReplayPreservesCapturedTargetAndAcceptedAbsentTarget)
{
	prepare();
	CampaignState campaign{};
	GameRandomizer randomizer(*gameState());
	for(auto * hero : {pasis(), monere()})
	{
		const auto node = campaign.crossoverSerialize(hero);
		ASSERT_EQ(node["primaryGrowthRules"]["creatureLineSpecialtyTarget"].String(), "new-horizons:wisp");
		auto loaded = campaign.crossoverDeserialize(node, map());
		ASSERT_NE(loaded, nullptr);
		ASSERT_NO_THROW(loaded->initHero(randomizer));
		EXPECT_EQ(loaded->getCreatureLineSpecialtyTarget(), creature("new-horizons:wisp"));
		EXPECT_EQ(specialtyMarkerCount(*loaded), 4u);
		auto preceding = node;
		preceding["primaryGrowthRules"].Struct().erase("creatureLineSpecialtyTarget");
		preceding["primaryGrowthRules"].Struct().erase("defaultCreatureLineReplacements");
		auto old = campaign.crossoverDeserialize(preceding, map());
		ASSERT_NE(old, nullptr);
		ASSERT_NO_THROW(old->initHero(randomizer));
		EXPECT_EQ(old->getCreatureLineSpecialtyTarget(), creature("core:psychicElemental"));
		EXPECT_EQ(specialtyMarkerCount(*old), 4u);
	}
}

namespace
{
// Typed binary fixture authoring: inject captured JSON into the existing public
// serialization format, without exposing private campaign pools to production.
struct CrossoverPoolWriter
{
	using Version = ESerializationVersion;
	CMemorySerializer & bytes;
	JsonNode hero;
	bool global = false;
	bool saving = true;
	bool hasFeature(Version feature) const { return bytes.oser.hasFeature(feature); }
	template<typename T> CrossoverPoolWriter & operator&(T & value)
	{
		if constexpr(std::is_same_v<T, std::map<CampaignScenarioID, std::vector<JsonNode>>>)
		{
			auto pool = value;
			if(!global) pool[CampaignScenarioID(0)] = {hero};
			bytes.oser & pool;
		}
		else if constexpr(std::is_same_v<T, std::map<HeroTypeID, JsonNode>>)
		{
			auto pool = value;
			if(global) pool[HeroTypeID(HeroTypeID::decode("core:pasis"))] = hero;
			bytes.oser & pool;
		}
		else bytes.oser & value;
		return *this;
	}
};
}

TEST_F(NewHorizonsWispSpecialtyTest, ActualCampaignPoolsAndStartLobbyEnvelopesRejectBeforePrefix)
{
	prepare();
	const auto previous = ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS;
	for(bool global : {false, true})
	{
		CampaignState empty{};
		CMemorySerializer seeded;
		CrossoverPoolWriter writer{seeded, empty.crossoverSerialize(pasis()), global};
		empty.serialize(writer);
		auto captured = std::make_shared<CampaignState>();
		ASSERT_NO_THROW(seeded.iser & *captured);
		EXPECT_NO_THROW(captured->validateDefaultCreatureLineSerialization(true));
		EXPECT_THROW(captured->validateDefaultCreatureLineSerialization(false), std::runtime_error);
		StartInfo start;
		start.campState = captured;
		LobbyState lobby;
		lobby.si = std::make_shared<StartInfo>(start);
		LobbyStartGame starting;
		starting.initializedStartInfo = std::make_shared<StartInfo>(start);
		for(int kind = 0; kind < 4; ++kind)
		{
			CMemorySerializer old;
			old.oser.version = previous;
			if(kind == 0) EXPECT_THROW(old.oser & *captured, std::runtime_error);
			if(kind == 1) EXPECT_THROW(old.oser & start, std::runtime_error);
			if(kind == 2) EXPECT_THROW(old.oser & lobby, std::runtime_error);
			if(kind == 3) EXPECT_THROW(old.oser & starting, std::runtime_error);
			EXPECT_TRUE(old.extractBuffer().empty());
		}
		CMemorySerializer legacy;
		legacy.oser.version = previous;
		legacy.iser.version = previous;
		CrossoverPoolWriter forged{legacy, empty.crossoverSerialize(pasis()), global};
		empty.serialize(forged);
		CampaignState rejected{};
		EXPECT_THROW(legacy.iser & rejected, std::runtime_error);
	}
}

TEST_F(NewHorizonsWispSpecialtyTest, WorldPreflightAlsoSeesCrossoverOnlyTargetWithAbsentMapPolicy)
{
	absent = true;
	prepare();
	ASSERT_FALSE(gameState()->getHeroDevelopmentRules().Struct().contains("defaultCreatureLineReplacements"));
	ASSERT_FALSE(pasis()->getPrimaryGrowthRules().Struct().contains("creatureLineSpecialtyTarget"));
	CampaignState empty{};
	auto node = empty.crossoverSerialize(pasis());
	const JsonNode installed(JsonPath::builtin("config/newHorizonsHeroes"));
	node["primaryGrowthRules"]["defaultCreatureLineReplacements"] = installed["defaultCreatureLineReplacements"];
	node["primaryGrowthRules"]["creatureLineSpecialtyTarget"].String() = "new-horizons:wisp";
	CMemorySerializer seeded;
	CrossoverPoolWriter writer{seeded, node, false};
	empty.serialize(writer);
	auto captured = std::make_shared<CampaignState>();
	ASSERT_NO_THROW(seeded.iser & *captured);
	gameState()->getStartInfo()->campState = captured;
	CMemorySerializer old;
	old.oser.version = ESerializationVersion::NEW_HORIZONS_PROTECTED_ADVENTURE_BARRIERS;
	EXPECT_THROW(old.oser & *gameState(), std::runtime_error);
	EXPECT_TRUE(old.extractBuffer().empty());
}

#ifdef ENABLE_BATTLE_AI
namespace
{
class WispPredictionEnvironment final : public Environment
{
	std::shared_ptr<CGameState> state;
public:
	explicit WispPredictionEnvironment(std::shared_ptr<CGameState> value) : state(std::move(value)) {}
	const Services * services() const override { return LIBRARY; }
	const BattleCb * battle(const BattleID & id) const override { return state->getBattle(id); }
	const GameCb * game() const override { return state.get(); }
};
}

TEST_F(NewHorizonsWispSpecialtyTest, DetachedBattleAIInheritsRealWispStatsWithoutChangingLiveArmy)
{
	prepare();
	for(int level = 1; level < 5; ++level)
		for(auto * hero : {pasis(), control()}) applyLevelUp(*gameState(), *hero);
	attackerSideHero = pasis();
	defenderSideHero = control();
	startBattle();
	WispPredictionEnvironment environment(gameState());
	auto callback = std::make_shared<CPlayerBattleCallback>(battle(), PlayerColor::SPECTATOR);
	HypotheticBattle projected(&environment, callback);
	const auto before = specialtyMarkers(*pasis());
	int enrolled = 0;
	for(const auto & live : battle()->stacks)
	{
		const auto * copy = projected.battleGetUnitByID(live->unitId());
		ASSERT_NE(copy, nullptr);
		EXPECT_EQ(copy->getAttack(false), live->getAttack(false));
		EXPECT_EQ(copy->getDefense(false), live->getDefense(false));
		EXPECT_EQ(copy->getMovementRange(), live->getMovementRange());
		EXPECT_EQ(copy->getInitiative(), live->getInitiative());
		if(live->unitSide() == BattleSide::ATTACKER
			&& (live->creatureId() == creature("new-horizons:wisp") || live->creatureId() == creature("new-horizons:wispUpgrade")))
			++enrolled;
	}
	EXPECT_EQ(enrolled, 2);
	EXPECT_EQ(specialtyMarkers(*pasis()), before);
	expectLine(*pasis(), *control(), true);
}
#endif
