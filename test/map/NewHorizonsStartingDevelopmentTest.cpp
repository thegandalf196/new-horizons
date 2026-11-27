/*
 * NewHorizonsStartingDevelopmentTest.cpp, part of VCMI engine
 * License: GNU General Public License v2 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsAcademySpecialties.h"
#include "../../lib/entities/hero/NewHorizonsInfernoSpecialties.h"
#include "../PerkStateFixture.h"
#include "../server/LegacyEstatesCapture.h"
#include "../NewHorizonsHistoricalAdventurePolicyTestUtils.h"
#include "../hero/HistoricalCapabilityRules.h"
#include "../mock/TinyMapGameTest.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/CCreatureHandler.h"
#include "../../lib/GameSettings.h"
#include "../../lib/gameState/QuestInfo.h"
#include "../../lib/spells/CSpellHandler.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/creature/NewHorizonsAssimilation.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/mapObjects/CGHeroInstance.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/networkPacks/PacksForLobby.h"
#include "../server/battles/FullGameSnapshotTypes.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include "../../lib/spells/NewHorizonsEagleEye.h"
#include "../../lib/spells/NewHorizonsMagic.h"
#include "../../lib/spells/NewHorizonsSorceryRevision.h"
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
	{"core:thane", "new-horizons:learning", 2, "new-horizons:learning.historian", "new-horizons:metamagic"},
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
	{"core:serena", "new-horizons:wisdom", 1, "new-horizons:wisdom.intelligence", "new-horizons:metamagic"},
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
	{"core:sephinroth", "new-horizons:estates", 1, "new-horizons:estates.prosperity", "new-horizons:shroudOfMalassa"}
}};

const std::array<ExpectedStart, 3> strongholdStarts{{
	{"core:terek", "new-horizons:battlecraft", 1, "new-horizons:battlecraft.tactics", "new-horizons:bloodrage"},
	{"core:oris", "new-horizons:learning", 1, "new-horizons:learning.eagleEye", "new-horizons:bloodrage"},
	{"core:saurug", "new-horizons:estates", 1, "new-horizons:estates.prosperity", "new-horizons:bloodrage"}
}};

const ExpectedStart solmyrStart{"core:solmyr", "new-horizons:havocMagic", 1,
	"new-horizons:havocMagic.stormcaller", "new-horizons:metamagic"};

JsonNode historicalStartingDevelopmentRules()
{
	// A complete historical one-authored-perk capture, independent of the
	// current skills-only rows. The policy2 helper authors both parents first;
	// this earlier policy retains only its explicit generic starting choice.
	auto rules = estatesTest::legacyEstatesHeroes();
	auto & startsPolicy = rules["startingSkills"];
	startsPolicy.Struct().erase("startingPerksPolicyVersion");
	startsPolicy.Struct().erase("startingPerkProfiles");
	const auto restore = [&](const auto & rows)
	{
		for(const auto & row : rows)
		{
			auto & profile = startsPolicy["startingDevelopmentProfiles"][row.hero];
			profile["skills"].Vector().clear();
			for(const auto & [skill, rank] : {std::pair{row.faction, row.factionRank}, std::pair{row.parent, row.rank}})
			{
				JsonNode entry;
				entry["skill"].String() = skill;
				entry["rank"].Integer() = rank;
				profile["skills"].Vector().push_back(entry);
			}
			JsonNode choice;
			choice["skill"].String() = row.parent;
			choice["perk"].String() = row.perk;
			profile["startingPerks"].Vector() = {choice};
		}
	};
	restore(starts);
	restore(fortressStarts);
	restore(castleStarts);
	restore(rampartStarts);
	restore(towerStarts);
	restore(infernoStarts);
	restore(necropolisStarts);
	restore(dungeonStarts);
	restore(strongholdStarts);
	restore(std::array{solmyrStart});
	return rules;
}

class NewHorizonsStartingDevelopmentTest : public TinyMapGameTest
{
protected:
	bool absent = false;
	bool absentDruidSpecialty = false;
	bool absentKindlingSpecialty = false;
	bool isolateCoreMysticism = false;
	bool isolateCoreIntelligence = false;
	bool previousFormatContext = false;
	bool legacy = false;
	bool explicitSkills = false;
	bool ownedTown = false;
	bool ownedCrystalMine = false;
	bool ownedGemMine = false;
	void configurePlayer(PlayerSettings & settings) const override
	{
		// These are authored hero/perk regressions, independent of the random
		// lobby artifact bonus, which can modify effective Knowledge or income.
		settings.bonus = PlayerStartingBonus::GOLD;
	}
	Services * gameServices() override { return LIBRARY; }
	void SetUp() override
	{
		TinyMapGameTest::SetUp();
		ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	}
	void mapLoaded(CMap * loaded) override
	{
		TinyMapGameTest::mapLoaded(loaded);
		auto rules = historicalStartingDevelopmentRules();
		if(absentKindlingSpecialty)
			rules["revisedSpecialties"].Struct().erase("kindling");
		if(absentDruidSpecialty || previousFormatContext)
			rules.Struct().erase("druidSpecialties");
		if(absent)
		{
			rules.Struct().erase("fortuneChildSpecialty");
			rules.Struct().erase("arcanePreparationSpecialty");
			rules.Struct().erase("bottomlessMindSpecialty");
			rules.Struct().erase("masterSpellwardSpecialty");
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
		if(isolateCoreMysticism)
			rules["recoverySpecialties"].Struct().erase("ritualReserve");
		if(isolateCoreIntelligence)
			rules.Struct().erase("bottomlessMindSpecialty");
		if(previousFormatContext)
		{
			// Optional families captured after the pre-starting-profile epoch.
			// Retain its actual hero schema, class rows and unrelated content.
			for(const auto * key : {"individualSpecialties", "schoolSpellcraftSpecialties",
				"additionalSpecialties", "recoverySpecialties", "learningExchangeSpecialties",
				"swampSeerSpecialty", "ghostSpecialty", "petrifyingBloodSpecialty",
				"livingInfernoSpecialty", "dreadCavalrySpecialty", "genieSageSpecialties", "chosenSpecialties"})
				rules.Struct().erase(key);
		}
		if(legacy)
			rules = JsonNode();
		rules.setOverrideFlag(true);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS,
			legacy ? JsonNode() : estatesTest::legacyEstatesPerks());
		if(legacy)
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, JsonNode());
		if(absent)
			isolateHistoricalAdventurePolicies(*loaded);
		if(previousFormatContext)
		{
			// The positive writer targets the epoch before starting profiles.
			// Capture the absence of later spell clauses, weighted offers and
			// Crisis Command, rather than bypass their production downwrite guards.
			auto magic = loaded->getSettings().getValue(EGameSettings::MAGIC_NEW_HORIZONS);
			magic["spells"]["core:implosion"].Struct().erase("implosion");
			magic["spells"]["core:teleport"].Struct().erase("ignoreInterveningBarriers");
			magic["spells"]["core:dispel"].Struct().erase("temporaryMagicalEffectsOnly");
			magic["spells"]["core:curse"].Struct().erase("schoolRankDurations");
			magic["spells"]["core:fireWall"].Struct().erase("burnGroundedFlyers");
			for(auto & [identity, row] : magic["spells"].Struct())
				for(const auto * key : {"focusMagic", "slow", "livingArmor"})
					row.Struct().erase(key);
			magic.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::MAGIC_NEW_HORIZONS, magic);
			auto perks = loaded->getSettings().getValue(EGameSettings::HEROES_NEW_HORIZONS_PERKS);
			perks.Struct().erase("offerWeightPolicyVersion");
			perks.Struct().erase("perkOfferNormalization");
			perks.Struct().erase("perkProgressionPolicyVersion");
			perks.Struct().erase("unifiedSkillProgressionVersion");
			for(const auto * key : {"sylvanTacticsPolicy", "necromancyPolicy", "connoisseurPolicy",
				"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
				perks.Struct().erase(key);
			for(auto & perk : perks["skills"]["new-horizons:command"]["perks"].Vector())
				if(perk["id"].String() == "new-horizons:command.crisisCommand")
					perk["effect"]["status"].String() = "planned";
			perks.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_PERKS, perks);
		}
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
		EXPECT_EQ(hero.getPerkState().getSelected(),
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

TEST_F(NewHorizonsStartingDevelopmentTest, All163ProfilesInitializeExactHistoricalCapturePackages)
{
	const auto rules = historicalStartingDevelopmentRules();
	const auto & profiles = rules["startingSkills"]["startingDevelopmentProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 163);
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
		const auto & captured = initialized->getPrimaryGrowthRules()["startingSkills"]["startingDevelopmentProfiles"][hero];
		EXPECT_EQ(captured, profile);
		for(const auto & skill : profile["skills"].Vector())
			EXPECT_EQ(initialized->getPerkSkillRank(skill["skill"].String()), skill["rank"].Integer());
		ASSERT_EQ(profile["startingPerks"].Vector().size(), 1u);
		const auto & perk = profile["startingPerks"].Vector().front();
		EXPECT_EQ(initialized->getPerkState().getSelected(),
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
	EXPECT_TRUE(actor()->getPerkState().getSelected().empty());
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::MAGIC_ARROW));
}

TEST_P(ApprovedFortressStarts, AbsentProfilesPreserveFactionOnlyMigrationAndRank)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->getPerkSkillRank(GetParam().faction), GetParam().factionRank);
	EXPECT_EQ(actor()->getPerkSkillRank(GetParam().parent), 0);
	EXPECT_TRUE(actor()->getPerkState().getSelected().empty());
}

TEST_P(ApprovedFortressStarts, CapturedLegacyPreservesOriginalRoster)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare(GetParam()));
	EXPECT_EQ(actor()->secSkills, actor()->getHeroType()->secSkillsInit);
	EXPECT_TRUE(actor()->getPerkState().getSelected().empty());
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

TEST_F(NewHorizonsStartingDevelopmentTest, RegisteredFocusSpecialtyPrototypesHaveFixedPremium)
{
	const SpellID focus(SpellID::decode("new-horizons:focusMagic"));
	ASSERT_TRUE(focus.hasValue());
	for(const auto * identity : {"core:cyra", "core:brissa", "core:terek"})
	{
		SCOPED_TRACE(identity);
		const auto * type = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		ASSERT_NE(type, nullptr);
		const auto matching = std::ranges::count_if(type->specialty, [focus](const auto & bonus)
		{
			return bonus && bonus->type == BonusType::SPECIAL_SPELL_LEV
				&& bonus->subtype == BonusSubtypeID(focus) && bonus->val == 10;
		});
		EXPECT_EQ(matching, 1);
	}
}

TEST_F(NewHorizonsStartingDevelopmentTest, TerekDefaultTacticsUsesCurrentFocusMagicBookAndSpecialty)
{
	ASSERT_NO_FATAL_FAILURE(prepare(strongholdStarts[0]));
	const SpellID focus(SpellID::decode("new-horizons:focusMagic"));
	EXPECT_FALSE(actor()->spellbookContainsSpell(SpellID::HASTE));
	EXPECT_TRUE(actor()->spellbookContainsSpell(focus));
	EXPECT_EQ(actor()->getNonDamageSpellSpecialtyBonusPercent(SpellID::HASTE), 0);
	// Focus's fixed specialty premium belongs to its dedicated enchantment
	// path; the generic spell multiplier must not apply it a second time.
	EXPECT_EQ(actor()->getNonDamageSpellSpecialtyBonusPercent(focus), 0);
	EXPECT_EQ(newHorizonsSorceryRevision::focusAdditionalRangedPercent(actor(), focus), 10);
}

TEST_F(NewHorizonsStartingDevelopmentTest, OrisDefaultEagleEyeSelectsObservedUnknownSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(strongholdStarts[1]));
	const SpellID observed(SpellID::decode("new-horizons:focusMagic"));
	actor()->removeSpellFromSpellbook(observed);
	ASSERT_TRUE(actor()->canLearnSpell(observed.toSpell()));
	ASSERT_TRUE(newHorizonsEagleEye::enabled(actor()));
	EXPECT_EQ(newHorizonsEagleEye::selectSpell(actor(), {observed}), std::optional<SpellID>(observed));
	EXPECT_FALSE(actor()->spellbookContainsSpell(observed));
}

TEST_F(NewHorizonsStartingDevelopmentTest, SaurugDefaultProsperityPreservesGemSpecialtyWithoutRetiredMineWindfall)
{
	ownedGemMine = true;
	ASSERT_NO_FATAL_FAILURE(prepare(strongholdStarts[2]));
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::WOOD], 1);
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::ORE], 1);
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
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources - before, expected);
	EXPECT_EQ(actor()->getNewHorizonsProspectorLastWeek(), -1);
	visit();
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources - before, expected);
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::GEMS], 1);
}

TEST_F(NewHorizonsStartingDevelopmentTest, JaegarAuthoredMysticismWithoutRecoverySpecialtyFeedsDailyNormalMana)
{
	isolateCoreMysticism = true;
	ASSERT_NO_FATAL_FAILURE(prepare(dungeonStarts[1]));
	EXPECT_FALSE(actor()->getPrimaryGrowthRules()["recoverySpecialties"].Struct().contains("ritualReserve"));
	expectStart(dungeonStarts[1], *actor());
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	actor()->setNormalSpellPoints(0);
	EXPECT_EQ(actor()->getManaNewTurn(true), 10);
}

TEST_F(NewHorizonsStartingDevelopmentTest, GeonDefaultEagleEyeSelectsUnknownObservedSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(dungeonStarts[3]));
	const SpellID observed(SpellID::decode("new-horizons:focusMagic"));
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

TEST_F(NewHorizonsStartingDevelopmentTest, SephinrothDefaultProsperityPreservesCrystalSpecialtyWithoutRetiredMineWindfall)
{
	ownedCrystalMine = true;
	ASSERT_NO_FATAL_FAILURE(prepare(dungeonStarts[5]));
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::WOOD], 1);
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::ORE], 1);
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
	EXPECT_EQ(gameState()->getPlayerState(PlayerColor(0))->resources - before, expected);
	EXPECT_EQ(actor()->getNewHorizonsProspectorLastWeek(), -1);
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
	const SpellID observed(SpellID::decode("new-horizons:focusMagic"));
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
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::GOLD], 575);
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
	// Capture the original Inferno specialty before the optional Kindling replacement.
	absentKindlingSpecialty = true;
	ASSERT_NO_FATAL_FAILURE(prepare(infernoStarts[1]));
	const SpellID inferno(SpellID::INFERNO);
	EXPECT_TRUE(actor()->spellbookContainsSpell(inferno));
	EXPECT_EQ(actor()->getDamageSpellSpecialtyBonusPercent(inferno), 15);
	EXPECT_FALSE(newHorizonsHeroes::usesKindlingHistory(actor()));
	EXPECT_EQ(newHorizonsMagic::spellPowerDamagePerkBonusPercent(actor()->getMagicRules(), actor(), inferno.toSpell()), 15);
	EXPECT_EQ(newHorizonsMagic::spellPowerDamagePerkBonusPercent(actor()->getMagicRules(), actor(), SpellID(SpellID::FROST_RING).toSpell()), 0);
}

TEST_F(NewHorizonsStartingDevelopmentTest, XyronKindlingReplacesInfernoSpecialtyAndPreservesPyromancer)
{
	ASSERT_NO_FATAL_FAILURE(prepare(infernoStarts[1]));
	const SpellID inferno(SpellID::INFERNO);
	EXPECT_TRUE(actor()->spellbookContainsSpell(inferno));
	EXPECT_TRUE(newHorizonsHeroes::usesKindlingHistory(actor()));
	EXPECT_EQ(actor()->getDamageSpellSpecialtyBonusPercent(inferno), 0);
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

TEST_F(NewHorizonsStartingDevelopmentTest, SerenaDefaultIntelligenceFeedsArcanePreparationWithoutCapacityRider)
{
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[1]));
	EXPECT_FALSE(newHorizonsEagleEye::enabled(actor()));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 131);
	EXPECT_EQ(actor()->getArcanePreparationPercent(), 30);
	EXPECT_EQ(actor()->getArcanePreparationBufferBonus(), 9);
	const auto view = newHorizonsHeroes::resolveSpecialtyPresentation(*actor()->getHeroType(),
		actor()->getPrimaryGrowthRules(), actor()->getMagicRules(), std::nullopt, actor());
	EXPECT_EQ(view.name, "Arcane Preparation");
	EXPECT_NE(view.description.find("Current: 30%, +9 temporary Buffer"), std::string::npos);
	EXPECT_EQ(actor()->getBufferSpellPoints(), 0);
	while(actor()->level < 15)
		actor()->levelUp({});
	EXPECT_EQ(actor()->getArcanePreparationPercent(), 100);
	EXPECT_EQ(actor()->getArcanePreparationBufferBonus(), 30);
	EXPECT_EQ(actor()->manaLimit(), 131);
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 4, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->getArcanePreparationBufferBonus(), 1);
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 0, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->getArcanePreparationBufferBonus(), 0);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AineDefaultTaxCollectorUsesActualOwnedTownAndPreservesGoldSpecialty)
{
	ownedTown = true;
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[3]));
	// Ordinary +125 Estates and +100 for one town; Wellspring of Wealth replaces the +350
	// specialty with Gold from her Mana.
	EXPECT_GT(newHorizonsHeroes::wellspringOfWealthGold(*actor()), 0);
	EXPECT_EQ(actor()->dailyIncome()[EGameResID::GOLD], 225 + newHorizonsHeroes::wellspringOfWealthGold(*actor()));
	EXPECT_EQ(actor()->getHeroType()->getJsonKey(), "core:aine");
}

TEST_F(NewHorizonsStartingDevelopmentTest, EllesharDefaultIntelligenceFeedsRealCapacityFloor)
{
	ASSERT_NO_FATAL_FAILURE(prepare(rampartStarts[2]));
	ASSERT_TRUE(actor()->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::HIGH_LORE));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->getHighLoreManaBonus(), 3); // floor(Intelligence extra30 * level1 10%).
	EXPECT_EQ(actor()->manaLimit(), 134);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::CURSE));
}

TEST_F(NewHorizonsStartingDevelopmentTest, EllesharOlderCapturedAbsenceKeepsOrdinaryIntelligenceCapacity)
{
	absentDruidSpecialty = true;
	ASSERT_NO_FATAL_FAILURE(prepare(rampartStarts[2]));
	EXPECT_TRUE(actor()->getPrimaryGrowthRules()["druidSpecialties"].isNull());
	EXPECT_FALSE(actor()->getDruidSpecialtyRules(newHorizonsHeroes::DruidSpecialtyKind::HIGH_LORE));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->getHighLoreManaBonus(), 0);
	EXPECT_EQ(actor()->manaLimit(), 131);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::CURSE));
}

TEST_F(NewHorizonsStartingDevelopmentTest, MalcomDefaultEagleEyeSelectsObservedSpellWithoutMutation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(rampartStarts[3]));
	const SpellID observed(SpellID::decode("new-horizons:focusMagic"));
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
	// Frost Mantle replaces the historical +15% Frost Ring specialty in new games.
	EXPECT_EQ(actor()->getDamageSpellSpecialtyBonusPercent(frost), 0);
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
	const SpellID observed(SpellID::decode("new-horizons:focusMagic"));
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
	// This fixture explicitly captures the historical authored Basic perk;
	// current skills-only default heroes are covered independently.
	ASSERT_TRUE(actor()->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.intelligence"));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(actor()->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 101);
	EXPECT_EQ(actor()->manaLimit(), 131);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::CURE));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 3, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 3); // floor(3 * 1.30), not an extra Mana point.
}

TEST_F(NewHorizonsStartingDevelopmentTest, RosicDefaultMysticismActuallyRestoresDailyNormalMana)
{
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[1]));
	ASSERT_TRUE(actor()->hasActivePerk("new-horizons:wisdom", "new-horizons:wisdom.mysticism"));
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 100, ChangeValueMode::ABSOLUTE);
	ASSERT_EQ(actor()->getPrimSkillLevel(PrimarySkill::KNOWLEDGE), 100);
	EXPECT_EQ(actor()->manaLimit(), 100);
	EXPECT_EQ(actor()->manaRegain(), 10);
	actor()->setNormalSpellPoints(0);
	EXPECT_EQ(actor()->getManaNewTurn(true), 10);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AndraAuthoredIntelligenceWithoutIndependentSpecialtyExpandsNormalCapacity)
{
	isolateCoreIntelligence = true;
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[2]));
	EXPECT_FALSE(actor()->getPrimaryGrowthRules().Struct().contains("bottomlessMindSpecialty"));
	expectStart(fortressStarts[2], *actor());
	EXPECT_EQ(actor()->getBottomlessMindManaBonus(), 0);
	actor()->setPrimarySkill(PrimarySkill::KNOWLEDGE, 101, ChangeValueMode::ABSOLUTE);
	EXPECT_EQ(actor()->manaLimit(), 131);
}

TEST_F(NewHorizonsStartingDevelopmentTest, AuthoredFactionRanksRemainStrictAndForeignOrMissingFactionRejects)
{
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[1]));
	auto rules = actor()->getPrimaryGrowthRules();
	auto pristine = actor()->getPerkState();
	pristine.replaceSelected({});
	const auto & fixture = fortressStarts[1];
	const auto row = [](JsonNode & profile, const std::string & identity) -> JsonNode &
	{
		auto & skills = profile["skills"].Vector();
		const auto found = std::ranges::find_if(skills, [&](const auto & skill)
			{ return skill["skill"].String() == identity; });
		if(found == skills.end()) throw std::runtime_error("Missing authored faction-rank fixture parent");
		return *found;
	};
	row(rules["startingSkills"]["startingDevelopmentProfiles"][fixture.hero], fixture.faction)
		["rank"].Integer() = MasteryLevel::ADVANCED;
	EXPECT_NO_THROW(newHorizonsHeroes::startingDevelopmentProfile(rules, pristine,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId()));
	rules = actor()->getPrimaryGrowthRules();
	const auto reject = [&](const std::function<void(JsonNode &)> & corrupt)
	{
		auto invalid = rules;
		corrupt(invalid["startingSkills"]["startingDevelopmentProfiles"][fixture.hero]);
		EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(invalid, pristine,
			actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
	};
	reject([&](JsonNode & profile) { row(profile, fixture.faction)["rank"].Integer() = 0; });
	reject([&](JsonNode & profile) { row(profile, fixture.faction)["rank"].Integer() = 4; });
	reject([&](JsonNode & profile)
	{
		std::erase_if(profile["skills"].Vector(), [&](const auto & skill)
			{ return skill["skill"].String() == fixture.faction; });
	});
	reject([&](JsonNode & profile) { row(profile, fixture.faction)["skill"].String() = "new-horizons:metamagic"; });
	reject([&](JsonNode & profile) { row(profile, fixture.parent)["rank"].Integer() = MasteryLevel::EXPERT; });
	row(rules["startingSkills"]["startingDevelopmentProfiles"][fixture.hero], fixture.faction)
		["rank"].Integer() = MasteryLevel::EXPERT;
	const auto expert = newHorizonsHeroes::startingDevelopmentProfile(rules, pristine,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId());
	ASSERT_TRUE(expert);
	const SecondarySkill faction(SecondarySkill::decode(fixture.faction));
	const auto factionRow = std::ranges::find_if(expert->skills, [&](const auto & skill) { return skill.first == faction; });
	ASSERT_NE(factionRow, expert->skills.end());
	EXPECT_EQ(factionRow->second, MasteryLevel::EXPERT);
	EXPECT_EQ(actor()->getPerkState().getSelected(),
		(std::vector<newHorizonsHeroes::PerkSelection>{{fortressStarts[1].parent, fortressStarts[1].perk}}));
}

TEST_F(NewHorizonsStartingDevelopmentTest, TivaDefaultEagleEyeActuallySelectsLegallyObservedUnknownSpell)
{
	ASSERT_NO_FATAL_FAILURE(prepare(fortressStarts[3]));
	const SpellID observed(SpellID::decode("new-horizons:focusMagic"));
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
	EXPECT_TRUE(actor()->getPerkState().getSelected().empty());
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID::MAGIC_ARROW));
}

TEST_F(NewHorizonsStartingDevelopmentTest, AbsentProfileKeepsExistingFactionOnlyMigration)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].faction), MasteryLevel::BASIC);
	EXPECT_EQ(actor()->getPerkSkillRank(starts[0].parent), 0);
	EXPECT_TRUE(actor()->getPerkState().getSelected().empty());
}

TEST_F(NewHorizonsStartingDevelopmentTest, CapturedLegacyKeepsOriginalStartingRoster)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	EXPECT_EQ(actor()->secSkills, actor()->getHeroType()->secSkillsInit);
	EXPECT_TRUE(actor()->getPerkState().getSelected().empty());
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
		auto & profile = rules["startingSkills"]["startingDevelopmentProfiles"][starts[0].hero];
		ASSERT_EQ(profile["startingPerks"].Vector().size(), 1u);
		corrupt(profile);
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
	// A planned row cannot remain acquired in a valid strict saved state.
	// Exercise profile admission against an unselected inactive alternative.
	inactive.replaceSelected({});
	perkStateFixture::editRules(inactive, [&](JsonNode & rules)
	{
		for(auto & perk : rules["skills"][starts[0].parent]["perks"].Vector())
			if(perk["id"].String() == starts[0].perk)
				perk["effect"]["status"].String() = "planned";
	});
	const auto inactiveBefore = inactive.toJson();
	EXPECT_THROW(newHorizonsHeroes::startingDevelopmentProfile(actor()->getPrimaryGrowthRules(), inactive,
		actor()->getHeroTypeID(), actor()->getHeroClass()->getId()), std::runtime_error);
	EXPECT_EQ(inactive.toJson(), inactiveBefore);
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

TEST_F(NewHorizonsStartingDevelopmentTest, AbsentCollectionStillRefusesPriorFormatAndRemainsWritableCurrently)
{
	absent = true;
	previousFormatContext = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const JsonNode installed(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto & captured = actor()->getPrimaryGrowthRules();
	for(const auto * key : {"schemaVersion", "rulesetVersion"})
		EXPECT_EQ(captured[key], installed[key]);
	const auto & worldRules = gameState()->getHeroDevelopmentRules();
	EXPECT_EQ(worldRules["classProfiles"], installed["classProfiles"]);
	EXPECT_FALSE(worldRules["startingSkills"].Struct().contains("startingDevelopmentProfiles"));
	EXPECT_FALSE(captured.Struct().contains("classProfiles"));
	const auto classId = HeroClassID::encode(actor()->getHeroClassID().getNum());
	EXPECT_EQ(captured["profile"], worldRules["classProfiles"][classId]);
	EXPECT_FALSE(captured["startingSkills"].Struct().contains("startingDevelopmentProfiles"));
	EXPECT_FALSE(captured.Struct().contains("recoverySpecialties"));
	EXPECT_FALSE(captured.Struct().contains("additionalSpecialties"));
	const auto & capturedPerks = actor()->getPerkState().getRules();
	for(const auto * key : {"sylvanTacticsPolicy", "necromancyPolicy", "connoisseurPolicy",
		"metamagicPolicy", "divineMandatePolicy", "demonicGatingPolicy", "bloodragePolicy"})
		EXPECT_FALSE(capturedPerks.Struct().contains(key));
	CMemorySerializer bytes;
	bytes.oser.version = static_cast<ESerializationVersion>(
		static_cast<int>(ESerializationVersion::NEW_HORIZONS_STARTING_DEVELOPMENT_PROFILES) - 1);
	const auto before = actor()->getPerkState().toJson();
	EXPECT_THROW(actor()->serialize(bytes.oser), std::runtime_error);
	EXPECT_TRUE(bytes.extractBuffer().empty());
	EXPECT_EQ(actor()->getPerkState().toJson(), before);
	bytes.oser.version = ESerializationVersion::CURRENT;
	EXPECT_NO_THROW(actor()->serialize(bytes.oser));
	EXPECT_FALSE(bytes.extractBuffer().empty());
}

// Current starting armies retain authored sampled ranges. Leadership efficiency
// is an offensive readout and never clamps an army during initialization.
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

class NewHorizonsCampaignStartingArmyTest : public NewHorizonsStartingArmyTest
{
protected:
	bool oldSpecialRequirements = false;
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsStartingArmyTest::mapLoaded(loaded);
		if(oldSpecialRequirements)
		{
			auto rules = historicalCapabilityRulesV4();
			rules["leadership"]["creatureRequirements"].Struct().erase("core:enchanter");
			rules["leadership"]["creatureRequirements"].Struct().erase("core:sharpshooter");
			rules.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS_CAPABILITIES, rules);
		}
	}
	void prepareCampaignHeroes(bool explicitArmies = false)
	{
		TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
		builder.size(36).playerActive(PlayerColor(0));
		for(int index = 0; index < 2; ++index)
		{
			builder.hero({5 + index * 5, 5, 0}, HeroTypeID(HeroTypeID::decode(
				index == 0 ? "core:dracon" : "core:gelu")), PlayerColor(0)).heroExperience(0);
			if(explicitArmies)
				builder.heroGarrison({{CreatureID(CreatureID::decode(
					index == 0 ? "core:enchanter" : "core:sharpshooter")), 123}});
		}
		startWithMap(std::move(builder));
	}
};

TEST_F(NewHorizonsCampaignStartingArmyTest, DefaultSpeciesRetainUnrestrictedAuthoredSampledRanges)
{
	prepareCampaignHeroes();
	for(const char * identity : {"core:dracon", "core:gelu"})
	{
		SCOPED_TRACE(identity);
		const auto * hero = gameState()->getMap().getHero(HeroTypeID(HeroTypeID::decode(identity)));
		ASSERT_NE(hero, nullptr);
		const auto & original = hero->getHeroType()->initialArmy;
		ASSERT_FALSE(original.empty());
		const CreatureID special(CreatureID::decode(std::string(identity) == "core:dracon"
			? "core:enchanter" : "core:sharpshooter"));
		ASSERT_TRUE(std::ranges::any_of(original, [special](const auto & row){ return row.creature == special; }));
		int ordinarySlot = 0;
		for(const auto & row : original)
		{
			if(row.creature.toCreature()->warMachine != ArtifactID::NONE)
			{
				EXPECT_TRUE(hero->hasArt(row.creature.toCreature()->warMachine));
				continue;
			}
			const SlotID slot(ordinarySlot++);
			EXPECT_EQ(hero->getCreature(slot)->getId(), row.creature);
			const auto capacity = hero->getLeadershipSlotCapacity(row.creature);
			EXPECT_FALSE(capacity); // Current efficiency is a readout, never army admission.
			EXPECT_GE(hero->getStackCount(slot), row.minAmount);
			EXPECT_LE(hero->getStackCount(slot), row.maxAmount);
		}
		EXPECT_EQ(hero->stacksCount(), ordinarySlot);
		const auto capacity = hero->getLeadershipSlotCapacity(special);
		EXPECT_FALSE(capacity);
		const auto readout = newHorizonsHeroes::leadershipStackReadout(hero, special, 123);
		EXPECT_TRUE(readout.active);
		EXPECT_EQ(readout.requirement, special == CreatureID(CreatureID::decode("core:enchanter")) ? 380 : 170);
	}
}

TEST_F(NewHorizonsCampaignStartingArmyTest, ExplicitMapSpeciesAndCountsAreNotClamped)
{
	prepareCampaignHeroes(true);
	GameRandomizer randomizer(*gameState());
	for(const char * identity : {"core:dracon", "core:gelu"})
	{
		auto * hero = gameState()->getMap().getHero(HeroTypeID(HeroTypeID::decode(identity)));
		ASSERT_NE(hero, nullptr);
		const auto species = hero->getCreature(SlotID(0))->getId();
		const CreatureID authoredSpecies(CreatureID::decode(std::string(identity) == "core:dracon"
			? "core:enchanter" : "core:sharpshooter"));
		ASSERT_EQ(species, authoredSpecies);
		const auto capacity = hero->getLeadershipSlotCapacity(species);
		EXPECT_FALSE(capacity);
		ASSERT_NO_THROW(hero->initHero(randomizer));
		EXPECT_EQ(hero->getCreature(SlotID(0))->getId(), species);
		EXPECT_EQ(hero->getStackCount(SlotID(0)), 123);
		EXPECT_EQ(hero->stacksCount(), 1);
	}
}

TEST_F(NewHorizonsCampaignStartingArmyTest, CapturedTableWithoutOnlySpecialRowsSurvivesWorldRoundtrip)
{
	oldSpecialRequirements = true;
	prepareCampaignHeroes(true);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	GameRandomizer randomizer(restored);
	for(const char * identity : {"core:dracon", "core:gelu"})
	{
		const HeroTypeID id(HeroTypeID::decode(identity));
		const auto * original = gameState()->getMap().getHero(id);
		auto * loaded = restored.getMap().getHero(id);
		ASSERT_NE(original, nullptr);
		ASSERT_NE(loaded, nullptr);
		const auto captured = original->getCapabilityRules();
		auto expected = historicalCapabilityRulesV4();
		expected["leadership"]["creatureRequirements"].Struct().erase("core:enchanter");
		expected["leadership"]["creatureRequirements"].Struct().erase("core:sharpshooter");
		EXPECT_EQ(captured["leadership"], expected["leadership"]);
		const auto species = original->getCreature(SlotID(0))->getId();
		const CreatureID authoredSpecies(CreatureID::decode(std::string(identity) == "core:dracon"
			? "core:enchanter" : "core:sharpshooter"));
		ASSERT_EQ(species, authoredSpecies);
		EXPECT_FALSE(original->getLeadershipSlotCapacity(species));
		EXPECT_TRUE(original->getLeadershipSlotCapacity(CreatureID(CreatureID::decode("core:pikeman"))));
		EXPECT_EQ(loaded->getCapabilityRules(), captured);
		EXPECT_FALSE(loaded->getLeadershipSlotCapacity(species));
		ASSERT_NO_THROW(loaded->initHero(randomizer));
		EXPECT_EQ(loaded->getCapabilityRules(), captured);
		EXPECT_FALSE(loaded->getLeadershipSlotCapacity(species));
		EXPECT_EQ(loaded->getCreature(SlotID(0))->getId(), species);
		EXPECT_EQ(loaded->getStackCount(SlotID(0)), 123);
	}
}

TEST_F(NewHorizonsStartingArmyTest, All144DefaultArmiesRetainCompositionAndUnrestrictedSampledRanges)
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
			const JsonNode capabilities(JsonPath::builtin("config/newHorizonsCapabilities"));
			const auto classId = HeroClassID::encode(initialized->getHeroClassID().getNum());
			EXPECT_EQ(leadership->capacity, capabilities["classProfiles"][classId]["base"].Integer());
			const auto firstSlot = initialized->getLeadershipSlotCapacity(
				CreatureID(CreatureID::decode("core:gremlin")));
			EXPECT_FALSE(firstSlot);
			EXPECT_EQ(initialized->getCreature(SlotID(0))->getId(),
				CreatureID(CreatureID::decode("core:gremlin")));
			EXPECT_GE(initialized->getStackCount(SlotID(0)), initialized->getHeroType()->initialArmy.front().minAmount);
			EXPECT_LE(initialized->getStackCount(SlotID(0)), initialized->getHeroType()->initialArmy.front().maxAmount);
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
			// The accepted Castle package changes generated forms, never prototype rows or count rolls.
			auto expectedCreature = original.creature;
			if(initialized->getHeroClass()->faction == FactionID::CASTLE)
			{
				ASSERT_EQ(initialized->getCapabilityRules()["defaultStartingArmyVersion"].Integer(), 1);
				const bool specialist = newHorizonsCreatures::isCommandSpecialistCreature(
					*initialized->getHeroType(), initialized->getPrimaryGrowthRules(), initialized->getMagicRules(),
					initialized->getCreatureLineSpecialtyTarget(), original.creature);
				if(std::string_view(hero) == "core:edric" && original.creature == CreatureID(CreatureID::decode("core:griffin")))
					EXPECT_TRUE(specialist); // His resolved Griffin line preserves the authored starting form.
				if(!specialist)
				{
					if(original.creature == CreatureID(CreatureID::decode("core:griffin")))
						expectedCreature = CreatureID(CreatureID::decode("core:swordsman"));
					if(original.creature == CreatureID(CreatureID::decode("core:royalGriffin")))
						expectedCreature = CreatureID(CreatureID::decode("core:crusader"));
				}
			}
			const auto capacity = initialized->getLeadershipSlotCapacity(expectedCreature);
			EXPECT_FALSE(capacity);
			const SlotID slot(ordinarySlot++);
			EXPECT_EQ(initialized->getCreature(slot)->getId(), expectedCreature);
			EXPECT_GE(initialized->getStackCount(slot),
				original.minAmount);
			EXPECT_LE(initialized->getStackCount(slot),
				original.maxAmount);
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
	EXPECT_FALSE(capacity);
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
	const ExpectedStart solmyr = solmyrStart;
	bool conflictingProfile = false;
	std::vector<newHorizonsHeroes::PerkSelection> historicalPrototype() const
	{
		return {{solmyr.parent, solmyr.perk}};
	}
	newHorizonsHeroes::PerkState prototypeSelection(
		const std::vector<newHorizonsHeroes::PerkSelection> & profile) const
	{
		newHorizonsHeroes::PerkState state;
		state.replaceRules(actor()->getPerkState().getRules());
		newHorizonsHeroes::selectStartingHeroPerks(state, profile, historicalPrototype(),
			[this](const std::string & skill) { return actor()->getPerkSkillRank(skill); });
		return state;
	}
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsStartingDevelopmentTest::mapLoaded(loaded);
		if(conflictingProfile)
		{
			auto rules = historicalStartingDevelopmentRules();
			JsonNode conflicting;
			conflicting["skill"].String() = solmyr.parent;
			conflicting["perk"].String() = "new-horizons:havocMagic.cryomancer";
			rules["startingSkills"]["startingDevelopmentProfiles"][solmyr.hero]["startingPerks"].Vector() = {conflicting};
			rules.setOverrideFlag(true);
			loaded->overrideGameSetting(EGameSettings::HEROES_NEW_HORIZONS, rules);
		}
	}
};

TEST_F(NewHorizonsStartingPerkOverlapTest, ExactDefaultProfileAndPrototypeSelectOnceAndKeepDuplicateValidation)
{
	ASSERT_NO_FATAL_FAILURE(prepare(solmyr));
	ASSERT_TRUE(actor()->getHeroType()->startingPerks.empty());
	const auto prototype = historicalPrototype();
	ASSERT_EQ(prototype,
		(std::vector<newHorizonsHeroes::PerkSelection>{{solmyr.parent, solmyr.perk}}));
	const auto projected = prototypeSelection(prototype);
	EXPECT_EQ(projected.getSelected(), prototype);
	expectStart(solmyr, *actor());
	EXPECT_THROW(actor()->applyPerkSelection({solmyr.parent, solmyr.perk}), std::runtime_error);
	EXPECT_EQ(actor()->getPerkState().getSelected().size(), 1);
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
	ASSERT_TRUE(actor()->getPerkState().getSelected().empty());
	const auto projected = prototypeSelection({});
	EXPECT_EQ(projected.getSelected(), historicalPrototype());
	EXPECT_EQ(actor()->getPerkSkillRank(solmyr.parent), MasteryLevel::ADVANCED);
	EXPECT_EQ(actor()->getPerkSkillRank(solmyr.faction), MasteryLevel::BASIC);
	EXPECT_TRUE(actor()->spellbookContainsSpell(SpellID(SpellID::MAGIC_ARROW)));
	EXPECT_TRUE(actor()->getPerkState().getSelected().empty());
}

TEST_F(NewHorizonsStartingPerkOverlapTest, AbsentCapturedProfilePreservesOriginalPrototypeSelection)
{
	absent = true;
	ASSERT_NO_FATAL_FAILURE(prepare(solmyr));
	ASSERT_TRUE(actor()->getPerkState().getSelected().empty());
	const auto projected = prototypeSelection({});
	EXPECT_EQ(projected.getSelected(), historicalPrototype());
	EXPECT_EQ(actor()->getPerkSkillRank(solmyr.parent), solmyr.rank);
	EXPECT_EQ(actor()->getPerkSkillRank(solmyr.faction), solmyr.factionRank);
	EXPECT_TRUE(actor()->getHeroType()->startingPerks.empty());
}

TEST_F(NewHorizonsStartingPerkOverlapTest, ConflictingDifferentBasicPerkStillRejects)
{
	conflictingProfile = true;
	ASSERT_NO_FATAL_FAILURE(prepare(solmyr));
	const auto originalState = actor()->getPerkState().toJson();
	const auto originalDefinition = actor()->getHeroType()->startingPerks;
	try
	{
		(void)prototypeSelection(actor()->getPerkState().getSelected());
		FAIL() << "Different Basic perks from profile and prototype must not silently replace each other";
	}
	catch(const std::runtime_error & error)
	{
		EXPECT_STREQ(error.what(), "Unavailable strict New Horizons perk selection");
	}
	EXPECT_THROW(actor()->applyPerkSelection({solmyr.parent, solmyr.perk}), std::runtime_error);
	EXPECT_EQ(actor()->getPerkState().toJson(), originalState);
	EXPECT_EQ(actor()->getHeroType()->startingPerks, originalDefinition);
	EXPECT_TRUE(originalDefinition.empty());
}
}

namespace
{
newHorizonsHeroes::StartingHeroContext capturedStartingContext(const CGHeroInstance & hero)
{
	auto result = newHorizonsHeroes::startingHeroContext(*LIBRARY->engineSettings());
	result.development = hero.getPrimaryGrowthRules();
	result.capabilities = hero.getCapabilityRules();
	result.perks = hero.getPerkState().getRules();
	result.magic = hero.getMagicRules();
	result.resolved = true;
	return result;
}
}

TEST_F(NewHorizonsStartingDevelopmentTest, StartingProjectionSerenaMatchesRealInitialization)
{
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[1]));
	const auto & hero = *actor();
	const auto view = newHorizonsHeroes::projectStartingHero(*hero.getHeroType(), capturedStartingContext(hero));
	for(size_t i = 0; i < view.primary.size(); ++i)
		EXPECT_EQ(view.primary[i], hero.getBasePrimarySkillValue(static_cast<PrimarySkill>(i)));
	EXPECT_EQ(view.skills, hero.secSkills);
	EXPECT_EQ(view.perks, hero.getPerkState().getSelected());
	EXPECT_EQ(view.spells, hero.getRawStartingSpellIds());
	EXPECT_EQ(view.primary, (std::array<int, 4>{5, 5, 45, 45}));
	EXPECT_EQ(view.army.front().creature, hero.getHeroType()->initialArmy.front().creature);
	EXPECT_EQ(view.army.front().minAmount, hero.getHeroType()->initialArmy.front().minAmount);
	EXPECT_EQ(view.army.front().maxAmount, hero.getHeroType()->initialArmy.front().maxAmount);
	EXPECT_FALSE(hero.getLeadershipSlotCapacity(view.army.front().creature));
	for(const auto & [slot, stack] : hero.Slots())
	{
		const auto & row = view.army.at(slot.getNum());
		EXPECT_EQ(stack->getCreatureID(), row.creature);
		EXPECT_GE(stack->getCount(), row.minAmount);
		EXPECT_LE(stack->getCount(), row.maxAmount);
	}
}

TEST_F(NewHorizonsStartingDevelopmentTest, StartingProjectionAll163MatchesActualRanksPerksAndBooks)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto & profiles = rules["startingSkills"]["startingDevelopmentProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 163);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(72).playerActive(PlayerColor(0));
	size_t index = 0;
	for(const auto & [key, profile] : profiles)
	{
		builder.hero({5 + static_cast<int>(index % 12) * 5, 5 + static_cast<int>(index / 12) * 5, 0},
			HeroTypeID(HeroTypeID::decode(key)), PlayerColor(0)).heroExperience(0);
		++index;
	}
	startWithMap(std::move(builder));
	for(const auto & [key, profile] : profiles)
	{
		SCOPED_TRACE(key);
		const auto * hero = gameState()->getMap().getHero(HeroTypeID(HeroTypeID::decode(key)));
		ASSERT_NE(hero, nullptr);
		const auto view = newHorizonsHeroes::projectStartingHero(*hero->getHeroType(), capturedStartingContext(*hero));
		EXPECT_EQ(view.skills, hero->secSkills);
		EXPECT_EQ(view.perks, hero->getPerkState().getSelected());
		EXPECT_EQ(view.spells, hero->getRawStartingSpellIds());
		for(size_t i = 0; i < view.primary.size(); ++i)
			EXPECT_EQ(view.primary[i], hero->getBasePrimarySkillValue(static_cast<PrimarySkill>(i)));
	}
}

TEST_F(NewHorizonsStartingDevelopmentTest, StartingProjectionIndependentPresetFieldsPreserveActualMapBook)
{
	explicitSkills = true;
	ASSERT_NO_FATAL_FAILURE(prepare());
	const auto & hero = *actor();
	newHorizonsHeroes::StartingHeroOverrides authored;
	authored.skills = std::vector<std::pair<SecondarySkill, ui8>>{{SecondarySkill::OFFENCE, MasteryLevel::ADVANCED}};
	authored.spells = std::set<SpellID>{SpellID::MAGIC_ARROW};
	authored.primary = std::array<int, 4>{7, 9, 11, 13};
	authored.army = std::vector<CHero::InitialArmyStack>{{999, 999, CreatureID(CreatureID::decode("core:gremlin"))}};
	const auto view = newHorizonsHeroes::projectStartingHero(*hero.getHeroType(), capturedStartingContext(hero), authored);
	EXPECT_EQ(view.skills, hero.secSkills);
	EXPECT_EQ(view.perks, hero.getPerkState().getSelected());
	EXPECT_EQ(view.spells, hero.getRawStartingSpellIds());
	EXPECT_EQ(view.primary, *authored.primary);
	EXPECT_EQ(view.army.front().minAmount, 999);
	EXPECT_EQ(view.army.front().maxAmount, 999);
	// A book override alone must not suppress a default development profile.
	authored.skills.reset();
	const auto independent = newHorizonsHeroes::projectStartingHero(*hero.getHeroType(), capturedStartingContext(hero), authored);
	EXPECT_EQ(independent.spells, *authored.spells);
	EXPECT_EQ(independent.skills.size(), 2);
	EXPECT_EQ(independent.perks.size(), 1);
}

TEST_F(NewHorizonsStartingDevelopmentTest, StartingProjectionEmptyCapturedPolicyNeverBackfillsInstalledDefaults)
{
	legacy = true;
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[1]));
	const auto & hero = *actor();
	auto context = capturedStartingContext(hero);
	context.capabilities = JsonNode();
	const auto view = newHorizonsHeroes::projectStartingHero(*hero.getHeroType(), context);
	EXPECT_EQ(view.skills, hero.getHeroType()->secSkillsInit);
	EXPECT_EQ(view.spells, hero.getHeroType()->spells);
	EXPECT_TRUE(view.perks.empty());
	for(size_t i = 0; i < view.primary.size(); ++i)
		EXPECT_EQ(view.primary[i], hero.getHeroClass()->primarySkillInitial[i]);
	EXPECT_EQ(view.army.front().minAmount, hero.getHeroType()->initialArmy.front().minAmount);
	EXPECT_EQ(view.army.front().maxAmount, hero.getHeroType()->initialArmy.front().maxAmount);
}

TEST_F(NewHorizonsStartingDevelopmentTest, StartingProjectionSolmyrExactOverlapStillSelectsOnce)
{
	ExpectedStart solmyr{"core:solmyr", "new-horizons:havocMagic", 1,
		"new-horizons:havocMagic.stormcaller", "new-horizons:metamagic"};
	ASSERT_NO_FATAL_FAILURE(prepare(solmyr));
	const auto & hero = *actor();
	const auto view = newHorizonsHeroes::projectStartingHero(*hero.getHeroType(), capturedStartingContext(hero));
	EXPECT_EQ(view.perks, hero.getPerkState().getSelected());
	EXPECT_EQ(std::ranges::count(view.perks,
		newHorizonsHeroes::PerkSelection{"new-horizons:havocMagic", "new-horizons:havocMagic.stormcaller"}), 1);
}

TEST_F(NewHorizonsStartingDevelopmentTest, StartingProjectionConflictingPerkRejectsWithoutMutatingInput)
{
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[1]));
	auto state = actor()->getPerkState();
	state.replaceSelected({});
	const auto before = state.toJson();
	EXPECT_THROW(newHorizonsHeroes::selectStartingHeroPerks(state,
		{{"new-horizons:learning", "new-horizons:learning.eagleEye"}},
		{{"new-horizons:learning", "new-horizons:learning.scholar"}},
		[](const std::string &){ return 1; }), std::runtime_error);
	EXPECT_EQ(state.toJson(), before);
}

namespace
{
class NewHorizonsStartingProjectionAuthoredTest : public NewHorizonsStartingDevelopmentTest
{
protected:
	bool authoredBookWithoutSentinel = false;
	std::optional<newHorizonsHeroes::StartingHeroProjection> beforeInitialization;
	std::optional<newHorizonsHeroes::StartingHeroOverrides> authored;
	void mapLoaded(CMap * loaded) override
	{
		NewHorizonsStartingDevelopmentTest::mapLoaded(loaded);
		for(const auto & object : loaded->objects)
			if(auto * hero = dynamic_cast<CGHeroInstance *>(object.get());
				hero && hero->anchorPos() == int3(5, 5, 0))
			{
				if(authoredBookWithoutSentinel)
				{
					// H3M adds this sentinel for all authored equipment. Model
					// the supported independent book/artifact input through the
					// public loader-time API, never private inventory access.
					hero->removeSpellFromSpellbook(SpellID(SpellID::SPELLBOOK_PRESET));
					ASSERT_FALSE(hero->getRawStartingSpellIds().contains(SpellID::SPELLBOOK_PRESET));
					const auto * bookSlot = hero->getSlot(ArtifactPosition::SPELLBOOK);
					ASSERT_NE(bookSlot, nullptr);
					ASSERT_FALSE(bookSlot->locked);
					ASSERT_TRUE(bookSlot->getID().hasValue());
					ASSERT_FALSE(hero->getHeroType()->haveSpellBook);
				}
				authored = newHorizonsHeroes::authoredStartingHeroOverrides(*hero);
				beforeInitialization = newHorizonsHeroes::projectStartingHero(*hero->getHeroType(),
					newHorizonsHeroes::startingHeroContext(loaded->getSettings()), *authored);
				return;
			}
	}
};
}

TEST_F(NewHorizonsStartingProjectionAuthoredTest, StartingProjectionRawAuthoredPrimaryPreservesZeroAndAbove99)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:serena")), PlayerColor(0))
		.heroExperience(0).heroPrimary(7, 9, 120, 0);
	startWithMap(std::move(builder));
	ASSERT_TRUE(authored && authored->primary && beforeInitialization);
	EXPECT_EQ(*authored->primary, (std::array<int, 4>{7, 9, 120, 0}));
	EXPECT_EQ(beforeInitialization->primary, *authored->primary);
	for(size_t i = 0; i < beforeInitialization->primary.size(); ++i)
		EXPECT_EQ(beforeInitialization->primary[i], actor()->getBasePrimarySkillValue(static_cast<PrimarySkill>(i)));
	EXPECT_EQ(actor()->getBasePrimarySkillValue(PrimarySkill::SPELL_POWER), 120);
	EXPECT_EQ(actor()->getBasePrimarySkillValue(PrimarySkill::KNOWLEDGE), 0);
}

TEST_F(NewHorizonsStartingProjectionAuthoredTest, StartingProjectionExistingPhysicalBookWithoutPresetIsRetained)
{
	authoredBookWithoutSentinel = true;
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:orrin")), PlayerColor(0))
		.heroExperience(0).heroEquipped({{ArtifactPosition::SPELLBOOK, ArtifactID::SPELLBOOK}});
	startWithMap(std::move(builder));
	ASSERT_TRUE(authored && beforeInitialization);
	ASSERT_TRUE(authored->spellBook.has_value());
	EXPECT_TRUE(*authored->spellBook);
	EXPECT_TRUE(beforeInitialization->spellBook);
	EXPECT_TRUE(actor()->hasSpellbook());
	EXPECT_EQ(beforeInitialization->skills, actor()->secSkills);
	EXPECT_EQ(beforeInitialization->perks, actor()->getPerkState().getSelected());
	EXPECT_EQ(beforeInitialization->spells, actor()->getRawStartingSpellIds());
}

namespace
{
class BasicStartingDefaultsTest : public NewHorizonsStartingDevelopmentTest
{
protected:
	void mapLoaded(CMap * loaded) override
	{
		// Use current single-perk defaults, independently of the historical suite above.
		TinyMapGameTest::mapLoaded(loaded);
	}
};
}

TEST_F(BasicStartingDefaultsTest, AstralStartsWithTwoBasicSkillsAndOneAuthoredPerk)
{
	ASSERT_NO_FATAL_FAILURE(prepare(towerStarts[0]));
	ASSERT_NE(actor(), nullptr);
	ASSERT_EQ(actor()->secSkills.size(), 2u);
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:spellcraft"), MasteryLevel::BASIC);
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:metamagic"), MasteryLevel::BASIC);
	const auto & perks = actor()->getPerkState();
	ASSERT_EQ(perks.getSelected().size(), 1u);
	EXPECT_TRUE(actor()->hasSpellbook());
	const auto view = newHorizonsHeroes::projectStartingHero(*actor()->getHeroType(), capturedStartingContext(*actor()));
	EXPECT_EQ(view.skills, actor()->secSkills);
	EXPECT_EQ(view.perks, perks.getSelected());
}

TEST_F(BasicStartingDefaultsTest, All163BasicDefaultsOnePerkAndSpellbookMatchPreview)
{
	const JsonNode rules(JsonPath::builtin("config/newHorizonsHeroes"));
	const auto & profiles = rules["startingSkills"]["startingDevelopmentProfiles"].Struct();
	ASSERT_EQ(profiles.size(), 163u);
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(108).playerActive(PlayerColor(0));
	size_t index = 0;
	for(const auto & [identity, profile] : profiles)
	{
		builder.hero({5 + static_cast<int>(index % 14) * 6, 5 + static_cast<int>(index / 14) * 6, 0},
			HeroTypeID(HeroTypeID::decode(identity)), PlayerColor(0)).heroExperience(0);
		++index;
	}
	startWithMap(std::move(builder));
	for(const auto & [identity, profile] : profiles)
	{
		SCOPED_TRACE(identity);
		const auto * hero = gameState()->getMap().getHero(HeroTypeID(HeroTypeID::decode(identity)));
		ASSERT_NE(hero, nullptr);
		// Cuthbert is the deliberate three-Skill, no-perk exception.
		const bool threeSkills = newHorizonsHeroes::usesThreeSkillStart(rules["startingSkills"], identity);
		ASSERT_EQ(hero->secSkills.size(), threeSkills ? 3u : 2u);
		EXPECT_NE(hero->secSkills[0].first, hero->secSkills[1].first);
		for(const auto & [skill, rank] : hero->secSkills)
			EXPECT_EQ(rank, MasteryLevel::BASIC);
		const auto & perks = hero->getPerkState();
		ASSERT_EQ(perks.getSelected().size(), threeSkills ? 0u : 1u);
		EXPECT_TRUE(hero->hasSpellbook());
		const auto view = newHorizonsHeroes::projectStartingHero(*hero->getHeroType(), capturedStartingContext(*hero));
		EXPECT_EQ(view.skills, hero->secSkills);
		EXPECT_EQ(view.perks, perks.getSelected());
		const auto actual = hero->getRawStartingSpellIds();
		if(threeSkills)
		{
			// Pariah mirrors Cuthbert's Light start with one random Shadow spell that
			// the deterministic preview cannot name.
			EXPECT_TRUE(std::ranges::includes(actual, view.spells));
			EXPECT_EQ(actual.size(), view.spells.size() + 1);
		}
		else
			EXPECT_EQ(view.spells, actual);
	}
}

TEST_F(BasicStartingDefaultsTest, ExplicitAdvancedMapRanksAndPerksSurviveSaveAndReinitialization)
{
	TinyH3M::TinyH3MBuilder builder(EMapFormat::SOD);
	builder.size(36).playerActive(PlayerColor(0))
		.hero({5, 5, 0}, HeroTypeID(HeroTypeID::decode("core:astral")), PlayerColor(0)).heroExperience(0)
		.heroSecondarySkills({
			{SecondarySkill(SecondarySkill::decode("new-horizons:spellcraft")), MasteryLevel::EXPERT},
			{SecondarySkill(SecondarySkill::decode("new-horizons:metamagic")), MasteryLevel::ADVANCED}});
	startWithMap(std::move(builder));
	ASSERT_NE(actor(), nullptr);
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:spellcraft"), MasteryLevel::EXPERT);
	EXPECT_EQ(actor()->getPerkSkillRank("new-horizons:metamagic"), MasteryLevel::ADVANCED);
	// Explicit scenario ranks remain authored; only one Basic default is granted.
	// Acquire the remaining earned tiers before testing persistence and re-init.
	ASSERT_EQ(actor()->getPerkState().getSelected().size(), 1u);
	for(const auto & [identity, rank] : {std::pair{"new-horizons:spellcraft", 3}, std::pair{"new-horizons:metamagic", 2}})
		for(int tier = 1; tier <= rank; ++tier)
		{
			if(actor()->getPerkState().hasPerkForTier(identity, tier)) continue;
			const auto pool = actor()->getPerkState().candidatesForSkillTier(identity, tier);
			ASSERT_FALSE(pool.empty());
			actor()->applyPerkSelection(pool.front().selection);
			EXPECT_TRUE(actor()->getPerkState().hasPerkForTier(identity, tier));
		}
	const auto skills = actor()->secSkills;
	const auto perks = actor()->getPerkState().toJson();
	ASSERT_EQ(actor()->getPerkState().getSelected().size(), 5u);
	const auto duplicate = actor()->getPerkState().getSelected().front();
	EXPECT_THROW(actor()->applyPerkSelection(duplicate), std::runtime_error);
	EXPECT_EQ(actor()->getPerkState().toJson(), perks);
	CGameState restored;
	restored.preInit(LIBRARY);
	restored.loadFromMemory(gameState()->saveToMemory());
	auto * loaded = restored.getMap().getHero(actor()->getHeroTypeID());
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->secSkills, skills);
	EXPECT_EQ(loaded->getPerkState().toJson(), perks);
	GameRandomizer randomizer(restored);
	ASSERT_NO_THROW(loaded->initHero(randomizer));
	EXPECT_EQ(loaded->secSkills, skills);
	EXPECT_EQ(loaded->getPerkState().toJson(), perks);
}
