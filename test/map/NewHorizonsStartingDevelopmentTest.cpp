/*
 * NewHorizonsStartingDevelopmentTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameSettings.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsEagleEye.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/rewardable/Reward.h"
#include "../../lib/CPlayerState.h"
#include "../../lib/gameState/CGameState.h"
#include "../../server/CGameHandler.h"
#include "../../server/queries/QueriesProcessor.h"
#include "../mock/GameHandlerTestServer.h"

namespace
{
struct ExpectedStart
{
	const char * hero;
	const char * parent;
	int rank;
	const char * perk;
	const char * faction;
	int factionRank = MasteryLevel::BASIC;
};
const std::array<ExpectedStart, 8> starts{{
	{"core:clancy", "new-horizons:logistics", 1, "new-horizons:logistics.pathfinding", "new-horizons:sylvanLuck"},
	{"core:piquedram", "new-horizons:logistics", 1, "new-horizons:logistics.scouting", "new-horizons:metamagic"},
	{"core:thane", "new-horizons:learning", 2, "new-horizons:learning.scholar", "new-horizons:metamagic"},
	{"core:torosar", "new-horizons:warMachines", 1, "new-horizons:warMachines.masterGunner", "new-horizons:metamagic"},
	{"core:iona", "new-horizons:learning", 1, "new-horizons:learning.scholar", "new-horizons:metamagic"},
	{"core:fiona", "new-horizons:logistics", 2, "new-horizons:logistics.scouting", "new-horizons:demonicGating"},
	{"core:ignatius", "new-horizons:battlecraft", 1, "new-horizons:battlecraft.tactics", "new-horizons:demonicGating"},
	{"core:lacus", "new-horizons:battlecraft", 2, "new-horizons:battlecraft.tactics", "new-horizons:elementalRebirth"}
}};
const std::array<ExpectedStart, 4> fortressStarts{{
	{"core:mirlanda", "new-horizons:shadowMagic", 1, "new-horizons:shadowMagic.witheringTouch", "new-horizons:bulwarkOfTheMire", MasteryLevel::ADVANCED},
	{"core:rosic", "new-horizons:wisdom", 1, "new-horizons:wisdom.mysticism", "new-horizons:bulwarkOfTheMire"},
	{"core:andra", "new-horizons:wisdom", 1, "new-horizons:wisdom.intelligence", "new-horizons:bulwarkOfTheMire"},
	{"core:tiva", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:bulwarkOfTheMire"}
}};
const std::array<ExpectedStart, 4> castleStarts{{
	{"core:adelaide", "new-horizons:havocMagic", 1, "new-horizons:havocMagic.cryomancer", "new-horizons:divineMandate", MasteryLevel::ADVANCED},
	{"core:ingham", "new-horizons:wisdom", 1, "new-horizons:wisdom.mysticism", "new-horizons:divineMandate"},
	{"core:sanya", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:divineMandate"},
	{"core:caitlin", "new-horizons:wisdom", 1, "new-horizons:wisdom.intelligence", "new-horizons:divineMandate"}
}};
const std::array<ExpectedStart, 5> rampartStarts{{
	{"core:thorgrim", "new-horizons:warcasting", 2, "new-horizons:warcasting.spellward", "new-horizons:sylvanLuck"},
	{"core:coronius", "new-horizons:spellcraft", 1, "new-horizons:spellcraft.concentration", "new-horizons:sylvanLuck"},
	{"core:elleshar", "new-horizons:wisdom", 1, "new-horizons:wisdom.intelligence", "new-horizons:sylvanLuck"},
	{"core:malcom", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:sylvanLuck"},
	{"core:aeris", "new-horizons:logistics", 1, "new-horizons:logistics.scouting", "new-horizons:sylvanLuck"}
}};

const std::array<ExpectedStart, 4> towerStarts{{
	{"core:astral", "new-horizons:spellcraft", 1, "new-horizons:spellcraft.concentration", "new-horizons:metamagic", MasteryLevel::ADVANCED},
	{"core:serena", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:metamagic"},
	{"core:daremyth", "new-horizons:luck", 1, "new-horizons:luck.secondChance", "new-horizons:metamagic"},
	{"core:aine", "new-horizons:estates", 1, "new-horizons:estates.taxCollector", "new-horizons:metamagic"}
}};

const std::array<ExpectedStart, 4> infernoStarts{{
	{"core:ayden", "new-horizons:wisdom", 1, "new-horizons:wisdom.intelligence", "new-horizons:demonicGating"},
	{"core:xyron", "new-horizons:havocMagic", 1, "new-horizons:havocMagic.pyromancer", "new-horizons:demonicGating"},
	{"core:axsis", "new-horizons:wisdom", 1, "new-horizons:wisdom.mysticism", "new-horizons:demonicGating"},
	{"core:ash", "new-horizons:chaosMagic", 1, "new-horizons:chaosMagic.frenziedCurse", "new-horizons:demonicGating"}
}};

const std::array<ExpectedStart, 8> necropolisStarts{{
	{"core:straker", "new-horizons:warcasting", 1, "new-horizons:warcasting.spellward", "new-horizons:necromancy"},
	{"core:charna", "new-horizons:battlecraft", 1, "new-horizons:battlecraft.tactics", "new-horizons:necromancy"},
	{"core:isra", "new-horizons:learning", 1, "new-horizons:learning.historian", "new-horizons:necromancy", MasteryLevel::ADVANCED},
	{"core:septienna", "new-horizons:spellcraft", 1, "new-horizons:spellcraft.arcaneFocus", "new-horizons:necromancy"},
	{"core:nimbus", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:necromancy"},
	{"core:thant", "new-horizons:wisdom", 1, "new-horizons:wisdom.mysticism", "new-horizons:necromancy"},
	{"core:vidomina", "new-horizons:learning", 1, "new-horizons:learning.scholar", "new-horizons:necromancy", MasteryLevel::ADVANCED},
	{"core:nagash", "new-horizons:estates", 1, "new-horizons:estates.taxCollector", "new-horizons:necromancy"}
}};

const std::array<ExpectedStart, 6> dungeonStarts{{
	{"core:alamar", "new-horizons:shadowMagic", 1, "new-horizons:shadowMagic.bloodDrinker", "new-horizons:shroudOfMalassa"},
	{"core:jaegar", "new-horizons:wisdom", 1, "new-horizons:wisdom.mysticism", "new-horizons:shroudOfMalassa"},
	{"core:jeddite", "new-horizons:spellcraft", 1, "new-horizons:spellcraft.concentration", "new-horizons:shroudOfMalassa", MasteryLevel::ADVANCED},
	{"core:geon", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:shroudOfMalassa"},
	{"core:deemer", "new-horizons:logistics", 2, "new-horizons:logistics.scouting", "new-horizons:shroudOfMalassa"},
	{"core:sephinroth", "new-horizons:estates", 1, "new-horizons:estates.prospector", "new-horizons:shroudOfMalassa"}
}};

const std::array<ExpectedStart, 3> strongholdStarts{{
	{"core:terek", "new-horizons:battlecraft", 1, "new-horizons:battlecraft.tactics", "new-horizons:bloodrage"},
	{"core:oris", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:bloodrage"},
	{"core:saurug", "new-horizons:estates", 1, "new-horizons:estates.prospector", "new-horizons:bloodrage"}
}};

class NewHorizonsStartingDevelopmentTest : public TinyMapGameTest
{
protected:
	bool absent = false;
	bool legacy = false;
	bool explicitSkills = false;
	bool ownedTown = false;
	bool ownedCrystalMine = false;
	bool ownedGemMine = false;
	Services * gameServices() override { return LIBRARY; }
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
		if(absent)
		{
			rules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
			rules["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
			rules["startingSkills"].Struct().erase("startingBookReplacements");
			rules.Struct().erase("remainingSpellSpecialtyReplacements");
			rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
			rules.Struct().erase("defaultCreatureLineReplacements");
			std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
				[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
			rules.Struct().erase("lighthouseDeparture");
			// This control captures the preceding profile, not a later optional
			// default-book feature whose key is deliberately not down-writable.
			rules["nonDamageSpellSpecialties"].Struct().erase("remainingStartReplacements");
		}
		if(legacy)
			rules = JsonNode();
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacy ? JsonNode() : JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
		if(legacy)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		if(absent)
			isolateHistoricalAdventurePolicies(*loaded);
	}
	void prepare(const ExpectedStart & value = starts[0])
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0))
			.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(value.hero)), PlayerColor(0)).heroExperience(0);
		if(explicitSkills)
			builder.heroSecondarySkills({{SecondarySkill::OFFENCE, MasteryLevel::ADVANCED}})
				.heroSpells({SpellID::MAGIC_ARROW});
		if(ownedTown)
			builder.town({20, 20, 0}, FactionID::TOWER, PlayerColor(0));
		if(ownedCrystalMine)
			builder.mine({20, 10, 0}, MapObjectSubID(GameResID::CRYSTAL), PlayerColor(0));
		if(ownedGemMine)
			builder.mine({20, 10, 0}, MapObjectSubID(GameResID::GEMS), PlayerColor(0));
		startWithMap(std::move(builder));
		ASSERT_NE(actor(), nullptr);
	}
	CGHeroInstance * actor() const { return findHeroAt({5, 5, 0}); }
	void expectStart(const ExpectedStart & value, const CGHeroInstance & hero)
	{
		EXPECT_EQ(hero.secSkills.size(), 2);
		EXPECT_EQ(hero.getPerkSkillRank(value.parent), value.rank);
		EXPECT_EQ(hero.getPerkSkillRank(value.faction), value.factionRank);
		EXPECT_EQ(hero.getPerkState().selected,
			(std::vector<newHorizonsHeroes::PerkSelection>{{value.parent, value.perk}}));
		const auto projected = hero.getPerkState().project([&hero](const std::string & skill)
		{
			return hero.getPerkSkillRank(skill);
		});
		ASSERT_EQ(projected.size(), 1);
		EXPECT_TRUE(projected.front().enabled);
		EXPECT_EQ(projected.front().requiredRank, MasteryLevel::BASIC);
		EXPECT_EQ(hero.getHeroClass(), hero.getHeroType()->heroClass);
	}
};

TEST_F(NewHorizonsStartingDevelopmentTest, All144ShippedProfilesInitializeExactActivePackages)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto & profiles = rules["startingSkills"]["startingDevelopmentProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 144);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(72).playerActive(PlayerColor(0));
	size_t index = 0;
	for(const auto & [hero, profile] : profiles)
	{
		builder.hero({5 + static_cast<int>(index % 12) * 5,
			5 + static_cast<int>(index / 12) * 5, 0},
			HeroTypeID(HeroTypeID::decode(hero)), PlayerColor(0)).heroExperience(0);
		++index;
	}
	startWithMap(std::move(builder));
	for(const auto & [hero, profile] : profiles)
	{
		SCOPED_TRACE(hero);
		const auto * initialized = gameState()->getMap().getHero(HeroTypeID(HeroTypeID::decode(hero)));
		ASSERT_NE(initialized, nullptr);
		ASSERT_EQ(initialized->secSkills.size(), 2);
		for(const auto & skill : profile["skills"].Vector())
			EXPECT_EQ(initialized->getPerkSkillRank(skill["skill"].String()), skill["rank"].Integer());
		const auto & perk = profile["startingPerks"].Vector().front();
		EXPECT_EQ(initialized->getPerkState().selected,
			(std::vector<newHorizonsHeroes::PerkSelection>{{perk["skill"].String(), perk["perk"].String()}}));
		const auto projected = initialized->getPerkState().project([initialized](const std::string & skill)
		{
			return initialized->getPerkSkillRank(skill);
		});
		ASSERT_EQ(projected.size(), 1);
		EXPECT_TRUE(projected.front().enabled);
		EXPECT_EQ(projected.front().requiredRank, MasteryLevel::BASIC);
		EXPECT_EQ(initialized->getHeroClass(), initialized->getHeroType()->heroClass);
	}
}

class ApprovedEightStarts : public NewHorizonsStartingDevelopmentTest,
	public ::testing::WithParamInterface<ExpectedStart> {};

TEST_P(ApprovedEightStarts, ActualDefaultStartSelectsActiveBasicPerkAndPreservesParentRank)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	expectStart(GetParam(), *actor());
}
INSTANTIATE_TEST_SUITE_P(RegisteredProfiles, ApprovedEightStarts, ::testing::ValuesIn(starts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

class ApprovedFortressStarts : public NewHorizonsStartingDevelopmentTest,
	public ::testing::WithParamInterface<ExpectedStart> {};

TEST_P(ApprovedFortressStarts, ActualDefaultInstallsExactRanksAndActivePerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	expectStart(GetParam(), *actor());
}

TEST_P(ApprovedFortressStarts, ExplicitMapDevelopmentAndBookRemainAuthoritative)
{
	explicitSkills = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:offense"), MasteryLevel::ADVANCED);
	EXPECT_EQ(actor()->getPerkSkillRank(GetParam().parent), 0);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::MAGIC_ARROW));
}

TEST_P(ApprovedFortressStarts, AbsentProfilesPreserveFactionOnlyMigrationAndRank)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->getPerkSkillRank(GetParam().faction), GetParam().factionRank);
	EXPECT_EQ(actor()->getPerkSkillRank(GetParam().parent), 0);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
}

TEST_P(ApprovedFortressStarts, CapturedLegacyPreservesOriginalRoster)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->secSkills, actor()->getHeroType()->secSkillsInit);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
}

TEST_P(ApprovedFortressStarts, WorldRoundtripAndReinitializationPreserveSingleSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(actor()->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	expectStart(GetParam(), *loaded);
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(loaded->initHero(randomizer));
	expectStart(GetParam(), *loaded);
}

INSTANTIATE_TEST_SUITE_P(RegisteredFortressProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(fortressStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

// Reuse the same five production-boundary cases without changing the frozen
// Fortress case identities or any of the original eight-profile tests.
INSTANTIATE_TEST_SUITE_P(RegisteredCastleProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(castleStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

INSTANTIATE_TEST_SUITE_P(RegisteredRampartProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(rampartStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

INSTANTIATE_TEST_SUITE_P(RegisteredTowerProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(towerStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

INSTANTIATE_TEST_SUITE_P(RegisteredInfernoProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(infernoStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

INSTANTIATE_TEST_SUITE_P(RegisteredNecropolisProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(necropolisStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

INSTANTIATE_TEST_SUITE_P(RegisteredDungeonProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(dungeonStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

INSTANTIATE_TEST_SUITE_P(RegisteredStrongholdProfiles, ApprovedFortressStarts,
	::testing::ValuesIn(strongholdStarts),
	[](const auto & info) { return std::string(info.param.hero).substr(5); });

TEST_F(NewHorizonsStartingDevelopmentTest, TerekDefaultTacticsLeavesActualHasteBookAndSpecialtyUnchanged)
{
	ASSERT_NO_FATAL_FAILURE(prepare(strongholdStarts[0]));
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::HASTE));
	EXPECT_EQ(actor()->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 20);
}

TEST_F(NewHorizonsStartingDevelopmentTest, OrisDefaultEagleEyeSelectsObservedUnknownSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(strongholdStarts[1]));
	const SpellID observed(SpellID::HASTE);
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
}

TEST_F(NewHorizonsStartingDevelopmentTest, SaurugDefaultProspectorPaysActualGemMineOnceAndPreservesGemIncome)
{
	ownedGemMine = true;
	ASSERT_NO_FATAL_FAILURE(prepare(strongholdStarts[2]));
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::GEMS], 1);
	auto * mine = findObjectAt({20, 10, 0});
	ASSERT_NE(mine, nullptr);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	const auto visit = [&]()
	{
		handler.objectVisited(mine, actor());
		while(const auto query = handler.queries->topQuery(PlayerColor(0)))
			handler.queries->popQuery(query);
	};
	visit();
	ResourceSet expected;
	expected[EGameResID::GEMS] = 1;
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources - before, expected);
	EXPECT_GE(actor()->getNewHorizonsProspectorLastWeek(), 0);
	visit();
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources - before, expected);
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::GEMS], 1);
}

TEST_F(NewHorizonsStartingDevelopmentTest, JaegarDefaultMysticismFeedsActualDailyNormalMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(dungeonStarts[1]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	actor()->setNormalSpellPoints(0);
	EXPECT_EQ(actor()->getManaNewTurn(true), 10);
}

TEST_F(NewHorizonsStartingDevelopmentTest, GeonDefaultEagleEyeSelectsUnknownObservedSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(dungeonStarts[3]));
	const SpellID observed(SpellID::HASTE);
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
}

TEST_F(NewHorizonsStartingDevelopmentTest, DeemerDefaultRetainsAdvancedTrainingAndRealScoutingSight)
{
	ASSERT_NO_FATAL_FAILURE(prepare(dungeonStarts[4]));
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:logistics"), MasteryLevel::ADVANCED);
	const auto ordinary = LIBRARY->engineSettings()->getInteger(EGameSettings::HEROES_BASE_SCOUNTING_RANGE);
	EXPECT_EQ(actor()->getSightRadius(), ordinary + 5);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::METEOR_SHOWER));
	EXPECT_EQ(actor()->getDamageSpellSpecialtyBonusPercent(SpellID::METEOR_SHOWER), 15);
}

TEST_F(NewHorizonsStartingDevelopmentTest, SephinrothDefaultProspectorPaysActualOwnedCrystalVisitOnlyOnce)
{
	ownedCrystalMine = true;
	ASSERT_NO_FATAL_FAILURE(prepare(dungeonStarts[5]));
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::CRYSTAL], 1);
	auto * mine = findObjectAt({20, 10, 0});
	ASSERT_NE(mine, nullptr);
	GameHandlerTestServer server(gameState(), PlayerColor(0));
	CGameHandler handler(server, gameState());
	const auto before = gameState()->getPlayerState(PlayerColor(0))->resources;
	const auto visit = [&]()
	{
		handler.objectVisited(mine, actor());
		while(const auto query = handler.queries->topQuery(PlayerColor(0)))
			handler.queries->popQuery(query);
	};
	visit();
	ResourceSet expected;
	expected[EGameResID::CRYSTAL] = 1;
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources - before, expected);
	EXPECT_GE(actor()->getNewHorizonsProspectorLastWeek(), 0);
	visit();
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources - before, expected);
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::CRYSTAL], 1);
}

TEST_F(NewHorizonsStartingDevelopmentTest, IsraDefaultHistorianBoostsOnlyPrimaryExperienceReward)
{
	ASSERT_NO_FATAL_FAILURE(prepare(necropolisStarts[2]));
	Rewardable::Reward reward;
	reward.heroExperience = 1000;
	const auto ordinary = reward.calculateHeroExperience(actor());
	reward.primaryExperienceReward = true;
	EXPECT_EQ(reward.calculateHeroExperience(actor()), ordinary + 500);
	EXPECT_EQ(reward.calculateHeroExperience(actor(), false), ordinary);
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:necromancy"), MasteryLevel::ADVANCED);
}

TEST_F(NewHorizonsStartingDevelopmentTest, NimbusDefaultEagleEyeSelectsObservedUnknownSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(necropolisStarts[4]));
	const SpellID observed(SpellID::HASTE);
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
}

TEST_F(NewHorizonsStartingDevelopmentTest, ThantDefaultMysticismFeedsActualDailyRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare(necropolisStarts[5]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	actor()->setNormalSpellPoints(0);
	EXPECT_EQ(actor()->manaLimit(), 100);
	EXPECT_EQ(actor()->getManaNewTurn(true), 10);
}

TEST_F(NewHorizonsStartingDevelopmentTest, NagashDefaultTaxCollectorPreservesGoldSpecialtyAndCountsOwnedTown)
{
	ownedTown = true;
	ASSERT_NO_FATAL_FAILURE(prepare(necropolisStarts[7]));
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::GOLD], 525);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AydenDefaultIntelligenceFeedsRealNormalManaCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(infernoStarts[0]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 131);
	EXPECT_EQ(actor()->getHeroClass(), actor()->getHeroType()->heroClass);
}

TEST_F(NewHorizonsStartingDevelopmentTest, XyronDefaultPyromancerFeedsFireDamageWithoutChangingSpecialty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(infernoStarts[1]));
	const SpellID inferno(SpellID::INFERNO);
	EXPECT_TRUE(actor()->spellbookContainsSpell(inferno));
	EXPECT_EQ(actor()->getDamageSpellSpecialtyBonusPercent(inferno), 15);
	EXPECT_EQ(newHorizonsMagic::spellPowerDamagePerkBonusPercent(actor()->getMagicRules(), actor(), inferno.toSpell()), 15);
	EXPECT_EQ(newHorizonsMagic::spellPowerDamagePerkBonusPercent(actor()->getMagicRules(), actor(), SpellID(SpellID::FROST_RING).toSpell()), 0);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AxsisDefaultMysticismFeedsRealDailyRecoveryWithoutNewRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare(infernoStarts[2]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	actor()->setNormalSpellPoints(0);
	EXPECT_EQ(actor()->manaLimit(), 100);
	EXPECT_EQ(actor()->manaRegain(), 10);
	EXPECT_EQ(actor()->getManaNewTurn(true), 10);
}

TEST_F(NewHorizonsStartingDevelopmentTest, SerenaDefaultEagleEyeSelectsUnknownObservedSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[1]));
	const SpellID observed(SpellID::HASTE);
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
}

TEST_F(NewHorizonsStartingDevelopmentTest, AineDefaultTaxCollectorUsesActualOwnedTownAndPreservesGoldSpecialty)
{
	ownedTown = true;
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[3]));
	// Ordinary +125 Estates, unchanged +350 specialty, and +50 for one town.
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::GOLD], 525);
	EXPECT_EQ(actor()->getHeroType()->getJsonKey(), "core:aine");
}

TEST_F(NewHorizonsStartingDevelopmentTest, EllesharDefaultIntelligenceFeedsRealCapacityFloor)
{
	ASSERT_NO_FATAL_FAILURE(prepare(rampartStarts[2]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 131);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::CURSE));
}

TEST_F(NewHorizonsStartingDevelopmentTest, MalcomDefaultEagleEyeSelectsObservedSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(rampartStarts[3]));
	const SpellID observed(SpellID::HASTE);
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::MAGIC_ARROW));
}

TEST_F(NewHorizonsStartingDevelopmentTest, AerisDefaultScoutingAddsFiveRealSightHexes)
{
	ASSERT_NO_FATAL_FAILURE(prepare(rampartStarts[4]));
	const auto ordinary = LIBRARY->engineSettings()->getInteger(EGameSettings::HEROES_BASE_SCOUNTING_RANGE);
	EXPECT_EQ(actor()->getSightRadius(), ordinary + 5);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AdelaideDefaultCryomancerFeedsOnlyIceDamagePowerComponent)
{
	ASSERT_NO_FATAL_FAILURE(prepare(castleStarts[0]));
	const SpellID frost(SpellID::FROST_RING);
	ASSERT_TRUE(actor()->spellbookContainsSpell(frost));
	ASSERT_TRUE(actor()->canCastThisSpell(frost.toSpell()));
	EXPECT_EQ(actor()->getDamageSpellSpecialtyBonusPercent(frost), 15);
	EXPECT_EQ(newHorizonsMagic::spellPowerDamagePerkBonusPercent(actor()->getMagicRules(), actor(), frost.toSpell()), 20);
	EXPECT_EQ(newHorizonsMagic::spellPowerDamagePerkBonusPercent(actor()->getMagicRules(), actor(), SpellID(SpellID::FIREBALL).toSpell()), 0);
}

TEST_F(NewHorizonsStartingDevelopmentTest, InghamDefaultMysticismSuppliesRealDailyRecovery)
{
	ASSERT_NO_FATAL_FAILURE(prepare(castleStarts[1]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	actor()->setNormalSpellPoints(0);
	EXPECT_EQ(actor()->manaLimit(), 100);
	EXPECT_EQ(actor()->getManaNewTurn(true), 10);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::CURSE));
}

TEST_F(NewHorizonsStartingDevelopmentTest, SanyaDefaultEagleEyeSelectsUnknownObservedSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(castleStarts[2]));
	const SpellID observed(SpellID::HASTE);
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::DISPEL));
}

TEST_F(NewHorizonsStartingDevelopmentTest, CaitlinDefaultIntelligencePreservesCureAndCapacityFloor)
{
	ASSERT_NO_FATAL_FAILURE(prepare(castleStarts[3]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 131);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::CURE));
}

TEST_F(NewHorizonsStartingDevelopmentTest, RosicDefaultMysticismActuallyRestoresDailyNormalMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[1]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 100);
	EXPECT_EQ(actor()->manaRegain(), 10);
	actor()->setNormalSpellPoints(0);
	EXPECT_EQ(actor()->getManaNewTurn(true), 10);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AndraDefaultIntelligenceActuallyExpandsNormalCapacity)
{
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[2]));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 131);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AuthoredFactionRanksRemainStrictAndForeignOrMissingFactionRejects)
{
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[1]));
	auto rules = actor()->getPrimaryGrowthRules();
	auto pristine = actor()->getPerkState();
	pristine.selected.clear();
	rules["startingSkills"]["startingDevelopmentProfiles"]["core:rosic"]["skills"].Vector()[1]["rank"].Integer() = MasteryLevel::ADVANCED;
	EXPECT_NO_THROW(newHorizonsHeroes::startingDevelopmentProfile(rules, pristine,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId()));
	rules = actor()->getPrimaryGrowthRules();
	const auto reject = [&](const std::function<void(JsonNode &)> & corrupt)
	{
		auto invalid = rules;
		corrupt(invalid["startingSkills"]["startingDevelopmentProfiles"]["core:rosic"]);
		EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(invalid, pristine,
			actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
	};
	reject([](JsonNode & profile) { profile["skills"].Vector()[1]["rank"].Integer() = 0; });
	reject([](JsonNode & profile) { profile["skills"].Vector()[1]["rank"].Integer() = 4; });
	reject([](JsonNode & profile) { profile["skills"].Vector().pop_back(); });
	reject([](JsonNode & profile) { profile["skills"].Vector()[1]["skill"].String() = "new-horizons:metamagic"; });
	reject([](JsonNode & profile) { profile["skills"].Vector()[0]["rank"].Integer() = MasteryLevel::EXPERT; });
	rules["startingSkills"]["startingDevelopmentProfiles"]["core:rosic"]["skills"].Vector()[1]["rank"].Integer() = MasteryLevel::EXPERT;
	const auto expert = newHorizonsHeroes::startingDevelopmentProfile(rules, pristine,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId());
	ASSERT_TRUE(expert);
	EXPECT_EQ(expert->skills[1].second, MasteryLevel::EXPERT);
	EXPECT_EQ(actor()->getPerkState().selected,
		(std::vector<newHorizonsHeroes::PerkSelection>{{fortressStarts[1].parent, fortressStarts[1].perk}}));
}

TEST_F(NewHorizonsStartingDevelopmentTest, TivaDefaultEagleEyeActuallySelectsLegallyObservedUnknownSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[3]));
	const SpellID observed(SpellID::HASTE);
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
}

TEST_F(NewHorizonsStartingDevelopmentTest, ExplicitMapSkillsAndBookAreNotReplacedOrGivenProfilePerks)
{
	explicitSkills = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:offense"), MasteryLevel::ADVANCED);
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].parent), 0);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::MAGIC_ARROW));
}

TEST_F(NewHorizonsStartingDevelopmentTest, AbsentProfileKeepsExistingFactionOnlyMigration)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].faction), MasteryLevel::BASIC);
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].parent), 0);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
}

TEST_F(NewHorizonsStartingDevelopmentTest, CapturedLegacyKeepsOriginalStartingRoster)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->secSkills, actor()->getHeroType()->secSkillsInit);
	EXPECT_TRUE(actor()->getPerkState().selected.empty());
}

TEST_F(NewHorizonsStartingDevelopmentTest, WorldRoundtripAndRepeatedSavedInitializationDoNotRepeatSelection)
{
	ASSERT_NO_FATAL_FAILURE(prepare(starts[2]));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(actor()->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->getHeroTypeID(), actor()->getHeroTypeID());
	ASSERT_EQ(loaded->id, actor()->id);
	expectStart(starts[2], *loaded);
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(loaded->initHero(randomizer));
	expectStart(starts[2], *loaded);
}

TEST_F(NewHorizonsStartingDevelopmentTest, InvalidProfilesRejectWithoutMutatingCapturedPerkState)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto original = actor()->getPerkState();
	const auto check = [&](const std::function<void(JsonNode &)> & corrupt)
	{
		auto rules = actor()->getPrimaryGrowthRules();
		corrupt(rules["startingSkills"]["startingDevelopmentProfiles"][starts[0].hero]);
		EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(rules, original,
			actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
		EXPECT_EQ(actor()->getPerkState().toJson(), original.toJson());
	};
	check([](JsonNode & profile) { profile["skills"].Vector()[0]["skill"].String() = "new-horizons:missingSkill"; });
	check([](JsonNode & profile) { profile["skills"].Vector()[0]["rank"].Float() = 1.5; });
	check([](JsonNode & profile)
	{
		profile["skills"].Vector()[0]["skill"].String() = "new-horizons:wisdom";
		profile["startingPerks"].Vector()[0]["skill"].String() = "new-horizons:wisdom";
		profile["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:wisdom.insight";
	});
	check([](JsonNode & profile) { profile["skills"].Vector()[1]["skill"].String() = "new-horizons:metamagic"; });
	check([](JsonNode & profile) { profile["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:learning.scholar"; });
	check([](JsonNode & profile) { profile["startingPerks"].Vector()[0]["perk"].String() = "new-horizons:logistics.roadmaster"; });
	auto inactive = original;
	for(auto & perk : inactive.rules["skills"][starts[0].parent]["perks"].Vector())
		if(perk["id"].String() == starts[0].perk)
			perk["effect"]["status"].String() = "planned";
	EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(actor()->getPrimaryGrowthRules(), inactive,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
	EXPECT_EQ(actor()->getPerkState().toJson(), original.toJson());
}

TEST_F(NewHorizonsStartingDevelopmentTest, UnsupportedSettingsHeroMapWorldAndLobbyRejectBeforePrefix)
{
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto oldVersion = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES) - 1);
	const auto reject = [oldVersion](auto & value)
	{
		CMemorySerializer bytes;
		bytes.oser.version = oldVersion;
		EXPECT_THROW(value.serialize(bytes.oser), std::runtime_error);
		EXPECT_TRUE(bytes.extractBuffer().empty());
	};
	reject(*actor());
	reject(gameState()->getMap());
	reject(*gameState());
	LobbyStartGame lobby;
	lobby.initializedStartInfo = std::make_shared<StartInfo>(*gameState()->getStartInfo());
	lobby.initializedGameState = gameState();
	reject(lobby);
	JsonNode raw;
	raw["heroes"]["newHorizons"] = actor()->getPrimaryGrowthRules();
	GameSettings settings;
	settings.addOverride(EGameSettings::HEROES_NEW_HORIZONS, actor()->getPrimaryGrowthRules());
	reject(settings);
	CMemorySerializer incoming;
	incoming.oser & raw;
	incoming.iser.version = oldVersion;
	GameSettings decoded;
	EXPECT_THROW(decoded.serialize(incoming.iser), std::runtime_error);
}

TEST_F(NewHorizonsStartingDevelopmentTest, KeyPresenceAdmissionDistinguishesAbsentEmptyAndNull)
{
	JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	rules["startingSkills"].Struct().erase("startingDevelopmentProfiles");
	rules["damageSpellSpecialties"].Struct().erase("coroniusHolyWrathReplacement");
	rules["startingSkills"].Struct().erase("startingBookReplacements");
	rules.Struct().erase("remainingSpellSpecialtyReplacements");
	rules["skillSpecialties"].Struct().erase("navigationStartReplacements");
	rules.Struct().erase("defaultCreatureLineReplacements");
	std::erase_if(rules["nonDamageSpellSpecialties"]["spells"].Vector(),
		[](const JsonNode & value) { return value.String() == "new-horizons:phantomArmy"; });
	rules.Struct().erase("lighthouseDeparture");
	EXPECT_NO_THROW(newHorizonsHeroes::validateStartingDevelopmentSerialization(rules, false));
	rules["startingSkills"]["startingDevelopmentProfiles"].Struct();
	EXPECT_THROW(newHorizonsHeroes::validateStartingDevelopmentSerialization(rules, false), std::runtime_error);
	rules["startingSkills"]["startingDevelopmentProfiles"] = JsonNode();
	EXPECT_THROW(newHorizonsHeroes::validateStartingDevelopmentSerialization(rules, false), std::runtime_error);
	EXPECT_THROW(newHorizonsHeroes::validateHeroRules(rules, true), std::runtime_error);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AbsentCollectionRemainsWritableInPreviousFormat)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	CMemorySerializer bytes;
	bytes.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES) - 1);
	EXPECT_NO_THROW(actor()->serialize(bytes.oser));
	EXPECT_FALSE(bytes.extractBuffer().empty());
}

// Provisional starting armies reuse the original sampled ranges and existing
// per-slot clamp. No new army table, species alias or saved initialization rule.
class NewHorizonsStartingArmyTest : public NewHorizonsStartingDevelopmentTest
{
protected:
	bool legacyCapabilities = false;
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsStartingDevelopmentTest::mapLoaded(loaded);
		JsonNode chances;
		for(int slot = 0; slot < 3; ++slot)
			chances.Vector().emplace_back(100);
		loaded->overrideGameSetting(EGameSettings::HEROES_STARTING_STACKS_CHANCES, chances);
		if(legacyCapabilities)
		{
			JsonNode empty;
			empty.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, empty);
		}
	}
};

TEST_F(NewHorizonsStartingArmyTest, All144DefaultArmiesRetainCompositionAndLeadershipSafeSampledRanges)
{
	const std::array<const char *, 144> heroes{{
		"core:orrin",
		"core:valeska",
		"core:edric",
		"core:sylvia",
		"core:lordHaart",
		"core:sorsha",
		"core:christian",
		"core:tyris",
		"core:adela",
		"core:cuthbert",
		"core:adelaide",
		"core:ingham",
		"core:sanya",
		"core:loynis",
		"core:caitlin",
		"core:rion",
		"core:mephala",
		"core:ufretin",
		"core:jenova",
		"core:ryland",
		"core:thorgrim",
		"core:ivor",
		"core:clancy",
		"core:kyrre",
		"core:coronius",
		"core:uland",
		"core:elleshar",
		"core:gem",
		"core:malcom",
		"core:melodia",
		"core:alagar",
		"core:aeris",
		"core:piquedram",
		"core:josephine",
		"core:neela",
		"core:torosar",
		"core:fafner",
		"core:halon",
		"core:iona",
		"core:rissa",
		"core:astral",
		"core:serena",
		"core:daremyth",
		"core:theodorus",
		"core:solmyr",
		"core:cyra",
		"core:aine",
		"core:thane",
		"core:fiona",
		"core:rashka",
		"core:marius",
		"core:ignatius",
		"core:octavia",
		"core:calh",
		"core:pyre",
		"core:nymus",
		"core:ayden",
		"core:xyron",
		"core:axsis",
		"core:olema",
		"core:calid",
		"core:ash",
		"core:xarfax",
		"core:zydar",
		"core:straker",
		"core:vokial",
		"core:moandor",
		"core:charna",
		"core:tamika",
		"core:isra",
		"core:clavius",
		"core:galthran",
		"core:septienna",
		"core:aislinn",
		"core:sandro",
		"core:nimbus",
		"core:thant",
		"core:xsi",
		"core:vidomina",
		"core:nagash",
		"core:lorelei",
		"core:arlach",
		"core:dace",
		"core:ajit",
		"core:damacon",
		"core:gunnar",
		"core:synca",
		"core:shakti",
		"core:alamar",
		"core:jaegar",
		"core:malekith",
		"core:jeddite",
		"core:geon",
		"core:deemer",
		"core:sephinroth",
		"core:darkstorn",
		"core:yog",
		"core:gurnisson",
		"core:jabarkas",
		"core:shiva",
		"core:gretchin",
		"core:krellion",
		"core:cragHack",
		"core:tyraxor",
		"core:gird",
		"core:vey",
		"core:dessa",
		"core:terek",
		"core:zubin",
		"core:gundula",
		"core:oris",
		"core:saurug",
		"core:bron",
		"core:drakon",
		"core:wystan",
		"core:tazar",
		"core:alkin",
		"core:korbac",
		"core:gerwulf",
		"core:broghild",
		"core:mirlanda",
		"core:rosic",
		"core:voy",
		"core:verdish",
		"core:merist",
		"core:styg",
		"core:andra",
		"core:tiva",
		"core:pasis",
		"core:thunar",
		"core:ignissa",
		"core:lacus",
		"core:monere",
		"core:erdamon",
		"core:fiur",
		"core:kalt",
		"core:luna",
		"core:brissa",
		"core:ciele",
		"core:labetha",
		"core:inteus",
		"core:aenain",
		"core:gelare",
		"core:grindan",
	}};
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(72).playerActive(PlayerColor(0));
	for(size_t index = 0; index < heroes.size(); ++index)
		builder.hero({5 + static_cast<int>(index % 12) * 5,
			5 + static_cast<int>(index / 12) * 5, 0},
			HeroTypeID(HeroTypeID::decode(heroes[index])), PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	for(const auto * hero : heroes)
	{
		SCOPED_TRACE(hero);
		const auto * initialized = gameState()->getMap().getHero(HeroTypeID(HeroTypeID::decode(hero)));
		ASSERT_NE(initialized, nullptr);
		ASSERT_EQ(initialized->level, 1);
		ASSERT_FALSE(initialized->Slots().empty());
		if(std::string_view(hero) == "core:halon" || std::string_view(hero) == "core:thane")
		{
			const bool halon = std::string_view(hero) == "core:halon";
			EXPECT_EQ(initialized->getHeroClassID(), HeroClassID(HeroClassID::decode(
				halon ? "core:alchemist" : "core:wizard")));
			const std::array<int, 4> expected = halon
				? std::array<int, 4>{30, 20, 20, 30}
				: std::array<int, 4>{5, 5, 45, 45};
			const std::array<PrimarySkill, 4> attributes = {
				PrimarySkill::ATTACK, PrimarySkill::DEFENSE,
				PrimarySkill::SPELL_POWER, PrimarySkill::KNOWLEDGE};
			for(size_t index = 0; index < attributes.size(); ++index)
				EXPECT_EQ(initialized->getPrimSkillLevel(attributes[index]), expected[index]);
			const auto leadership = initialized->getLeadershipCapacity();
			ASSERT_TRUE(leadership);
			EXPECT_EQ(leadership->capacity, halon ? 875 : 650);
			const auto firstSlot = initialized->getLeadershipSlotCapacity(
				CreatureID(CreatureID::decode("core:gremlin")));
			ASSERT_TRUE(firstSlot);
			EXPECT_EQ(firstSlot->maximum, halon ? 17 : 13);
			EXPECT_EQ(initialized->getCreature(SlotID(0))->getId(),
				CreatureID(CreatureID::decode("core:gremlin")));
			EXPECT_EQ(initialized->getStackCount(SlotID(0)), firstSlot->maximum);
		}
		int ordinarySlot = 0;
		for(const auto & original : initialized->getHeroType()->initialArmy)
		{
			const auto * creature = original.creature.toCreature();
			ASSERT_NE(creature, nullptr);
			if(creature->warMachine != ArtifactID::NONE)
			{
				EXPECT_TRUE(initialized->hasArt(creature->warMachine));
				continue;
			}
			const auto capacity = initialized->getLeadershipSlotCapacity(original.creature);
			ASSERT_TRUE(capacity);
			ASSERT_GT(capacity->maximum, 0);
			const SlotID slot(ordinarySlot++);
			EXPECT_EQ(initialized->getCreature(slot), creature);
			EXPECT_GE(initialized->getStackCount(slot),
				std::min<int>(original.minAmount, capacity->maximum));
			EXPECT_LE(initialized->getStackCount(slot),
				std::min<int>(original.maxAmount, capacity->maximum));
			EXPECT_LE(initialized->getStackCount(slot), capacity->maximum);
		}
		EXPECT_EQ(initialized->stacksCount(), ordinarySlot);
	}
}

TEST_F(NewHorizonsStartingArmyTest, ExplicitMapArmyAndReinitializationAreNotClampedOrResampled)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(0))
		.heroExperience(0).heroGarrison({{CreatureID(CreatureID::decode("core:pikeman")), 123}});
	startWithMap(std::move(builder));
	ASSERT_NE(actor(), nullptr);
	const auto capacity = actor()->getLeadershipSlotCapacity(CreatureID(CreatureID::decode("core:pikeman")));
	ASSERT_TRUE(capacity);
	ASSERT_LT(capacity->maximum, 123);
	EXPECT_EQ(actor()->getStackCount(SlotID(0)), 123);
	GameRandomizer randomizer(*gameState());
	ASSERT_NO_THROW(actor()->initHero(randomizer));
	EXPECT_EQ(actor()->getStackCount(SlotID(0)), 123);
	EXPECT_EQ(actor()->stacksCount(), 1);
}

TEST_F(NewHorizonsStartingArmyTest, WorldRoundtripRetainsSampledArmyWithoutResampling)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	ASSERT_NE(actor(), nullptr);
	std::vector<std::pair<CreatureID, int>> saved;
	for(int slot = 0; slot < actor()->stacksCount(); ++slot)
		saved.emplace_back(actor()->getCreature(SlotID(slot))->getId(), actor()->getStackCount(SlotID(slot)));
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(actor()->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(loaded->initHero(randomizer));
	ASSERT_EQ(loaded->stacksCount(), saved.size());
	for(size_t slot = 0; slot < saved.size(); ++slot)
	{
		EXPECT_EQ(loaded->getCreature(SlotID(slot))->getId(), saved[slot].first);
		EXPECT_EQ(loaded->getStackCount(SlotID(slot)), saved[slot].second);
	}
}

TEST_F(NewHorizonsStartingArmyTest, CapturedLegacyAbsenceRetainsOriginalUnclampedRanges)
{
	legacyCapabilities = true;
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:rissa")), PlayerColor(0)).heroExperience(0);
	startWithMap(std::move(builder));
	ASSERT_NE(actor(), nullptr);
	ASSERT_EQ(actor()->getHeroType()->initialArmy.front().creature, CreatureID(CreatureID::decode("core:gremlin")));
	EXPECT_FALSE(actor()->getLeadershipSlotCapacity(CreatureID(CreatureID::decode("core:gremlin"))));
	EXPECT_GE(actor()->getStackCount(SlotID(0)), 30);
	EXPECT_LE(actor()->getStackCount(SlotID(0)), 40);
}

class NewHorizonsStartingPerkOverlapTest : public NewHorizonsStartingDevelopmentTest
{
protected:
	const ExpectedStart solmyr{"core:solmyr", "new-horizons:havocMagic", 1,
		"new-horizons:havocMagic.stormcaller", "new-horizons:metamagic"};
	bool conflictingProfile = false;
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsStartingDevelopmentTest::mapLoaded(loaded);
		if(conflictingProfile)
		{
			JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
			rules["startingSkills"]["startingDevelopmentProfiles"]["core:solmyr"]
				["startingPerks"].Vector().front()["perk"].String() = "new-horizons:havocMagic.cryomancer";
			rules.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		}
	}
};

TEST_F(NewHorizonsStartingPerkOverlapTest, ExactDefaultProfileAndPrototypeSelectOnceAndKeepDuplicateValidation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(solmyr));
	ASSERT_EQ(actor()->getHeroType()->startingPerks,
		(std::vector<newHorizonsHeroes::PerkSelection>{{solmyr.parent, solmyr.perk}}));
	expectStart(solmyr, *actor());
	EXPECT_THROW(actor()->applyPerkSelection({solmyr.parent, solmyr.perk}), std::runtime_error);
	EXPECT_EQ(actor()->getPerkState().selected.size(), 1);
	GameRandomizer randomizer(*gameState());
	ASSERT_NO_THROW(actor()->initHero(randomizer));
	expectStart(solmyr, *actor());
}

TEST_F(NewHorizonsStartingPerkOverlapTest, PresetParentsAndBookRetainPrototypeOnlySelection)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode(solmyr.hero)), PlayerColor(0)).heroExperience(0)
		.heroSecondarySkills({
			{SecondarySkill(SecondarySkill::decode(solmyr.parent)), MasteryLevel::ADVANCED},
			{SecondarySkill(SecondarySkill::decode(solmyr.faction)), MasteryLevel::BASIC}})
		.heroSpells({SpellID(SpellID::MAGIC_ARROW)});
	startWithMap(std::move(builder));
	ASSERT_NE(actor(), nullptr);
	const ExpectedStart expected{solmyr.hero, solmyr.parent, MasteryLevel::ADVANCED, solmyr.perk, solmyr.faction};
	expectStart(expected, *actor());
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_EQ(actor()->getPerkState().selected.size(), 1);
}

TEST_F(NewHorizonsStartingPerkOverlapTest, AbsentCapturedProfilePreservesOriginalPrototypeSelection)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare(solmyr));
	expectStart(solmyr, *actor());
	EXPECT_EQ(actor()->getPerkState().selected.size(), 1);
}

TEST_F(NewHorizonsStartingPerkOverlapTest, ConflictingDifferentBasicPerkStillRejects)
{
	conflictingProfile = true;
	try
	{
		prepare(solmyr);
		FAIL() << "Different Basic perks from profile and prototype must not silently replace each other";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "New Horizons perk tier already occupied for skill");
	}
}
}
