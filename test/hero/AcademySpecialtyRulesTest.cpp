/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../OriginalDataRequirement.h"
#include "../../lib/entities/hero/CHero.h"
#include "../../lib/entities/hero/CHeroClass.h"
#include "../../lib/entities/hero/HeroStartingProjection.h"
#include "../../lib/entities/hero/NewHorizonsAcademySpecialties.h"
#include "../../lib/entities/hero/NewHorizonsHeroRules.h"
#include "../../lib/entities/hero/NewHorizonsPerkState.h"
#include "../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsStartingPerks.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/filesystem/ResourcePath.h"
#include <map>
#include <set>

namespace
{
using namespace newHorizonsHeroes;

JsonNode academyShippedHeroes()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
}

constexpr std::array<const char *, 11> ACADEMY_ROWS{"hardened", "spoilsOfAlchemy", "practiceMakesPerfect",
	"wellspringOfWealth", "stormLord", "serpentsEmbraceBound", "goldStandardDiscount", "inexhaustibleKnowledge",
	"halonNullifyingShot", "focusMagicSpecialty", "sageOfTheAges"};

StartingHeroContext academyStartContext(JsonNode rules = academyShippedHeroes())
{
	StartingHeroContext result;
	result.development = std::move(rules);
	result.capabilities = JsonNode(JsonPath::builtin("config/newHorizonsCapabilities"));
	PerkState perks;
	perks.replaceRules(JsonNode(JsonPath::builtin("config/newHorizonsPerks")));
	result.perks = perks.getRules();
	result.magic = JsonNode(JsonPath::builtin("config/newHorizonsMagic"));
	return result;
}

const CHero & academyHero(const char * key)
{
	return *HeroTypeID(HeroTypeID::decode(key)).toHeroType();
}
}

TEST(AcademySpecialtyRules, ShippedTableCapturesEveryRow)
{
	const auto rules = academySpecialtyRules(academyShippedHeroes());
	ASSERT_TRUE(rules.hardened && rules.spoilsOfAlchemy && rules.practiceMakesPerfect && rules.wellspringOfWealth
		&& rules.stormLord && rules.serpentsEmbraceBoundPercent && rules.goldStandardDiscountPercent && rules.inexhaustibleKnowledge && rules.halonNullifyingShot
		&& rules.focusMagicPermille && rules.sageOfTheAges);
	EXPECT_EQ(rules.hardened->hitPermille, 50);
	EXPECT_EQ(rules.spoilsOfAlchemy->maximumMercury, 10);
	EXPECT_EQ(rules.practiceMakesPerfect->percentPerCast, 1);
	EXPECT_EQ(rules.stormLord->chainLightningLevel, 10);
	EXPECT_EQ(rules.inexhaustibleKnowledge->minimumMana, 1);
	EXPECT_EQ(rules.sageOfTheAges->spellPowerPerLevel, 2);
	EXPECT_EQ(rules.sageOfTheAges->knowledgePerLevel, 2);
}

TEST(AcademySpecialtyRules, LevelScalingFollowsTheTable)
{
	const auto rules = academySpecialtyRules(academyShippedHeroes());
	// Neela: total up to 10%, +0.5%/level, max 20% (permille).
	EXPECT_EQ(rules.hardened->totalPermille.at(1), 100);
	EXPECT_EQ(rules.hardened->totalPermille.at(11), 150);
	EXPECT_EQ(rules.hardened->totalPermille.at(21), 200);
	EXPECT_EQ(rules.hardened->totalPermille.at(40), 200);
	// Solmyr: +3%/level, max +60%.
	EXPECT_EQ(rules.stormLord->damagePercent.at(1), 3);
	EXPECT_EQ(rules.stormLord->damagePercent.at(20), 60);
	EXPECT_EQ(rules.stormLord->damagePercent.at(30), 60);
	// Fafner: +2%/level, max +40%.
	EXPECT_EQ(rules.serpentsEmbraceBoundPercent->at(1), 2);
	EXPECT_EQ(rules.serpentsEmbraceBoundPercent->at(20), 40);
	// Josephine: 10%, +1%/level, max 30%.
	EXPECT_EQ(rules.goldStandardDiscountPercent->at(1), 10);
	EXPECT_EQ(rules.goldStandardDiscountPercent->at(21), 30);
	// Theodorus: 4% of his maximum Mana, +0.2 points per further level (8% at level 21).
	EXPECT_EQ(rules.inexhaustibleKnowledge->manaPermille.at(1), 40);
	EXPECT_EQ(rules.inexhaustibleKnowledge->manaPermille.at(21), 80);
	// Cyra: +10pp, +0.5pp/level, max +20pp (permille of a percentage point x 10).
	EXPECT_EQ(rules.focusMagicPermille->at(1), 100);
	EXPECT_EQ(rules.focusMagicPermille->at(2), 105);
	EXPECT_EQ(rules.focusMagicPermille->at(21), 200);
	// Rissa: 2000 - 50 x level Gold per Mercury, at least 1000 (the table's own formula).
	const auto & spoils = *rules.spoilsOfAlchemy;
	const auto divisor = [&spoils](int level)
	{ return std::max(spoils.minimumGoldPerMercury, spoils.goldPerMercury - spoils.goldPerMercuryPerLevel * level); };
	EXPECT_EQ(divisor(1), 1950);
	EXPECT_EQ(divisor(20), 1000);
	EXPECT_EQ(divisor(30), 1000);
	// Aine: x(1.5 + 0.075 x level), x3 at level 20 and above.
	const auto & wealth = *rules.wellspringOfWealth;
	const auto multiplier = [&wealth](int level)
	{ return std::min(wealth.maximumGoldPerManaPermille, wealth.goldPerManaPermille + wealth.goldPerManaPermillePerLevel * level); };
	EXPECT_EQ(multiplier(1), 1575);
	EXPECT_EQ(multiplier(10), 2250);
	EXPECT_EQ(multiplier(20), 3000);
	EXPECT_EQ(multiplier(25), 3000);
}

TEST(AcademySpecialtyRules, BetaOneCaptureKeepsEveryOldAcademySpecialty)
{
	// A 0.94.0-beta.1 capture (format 1166 with the Castle and Rampart rows only) keeps them,
	// including Thane's +1/+1 Sage of the Ages.
	auto beta = academyShippedHeroes();
	for(const auto * key : ACADEMY_ROWS)
		beta["revisedSpecialties"].Struct().erase(key);
	EXPECT_EQ(academySpecialtyRules(beta), AcademySpecialtyRules{});
	EXPECT_NO_THROW(validateRevisedSpecialtiesSerialization(beta));
	auto none = academyShippedHeroes();
	none.Struct().erase("revisedSpecialties");
	EXPECT_EQ(academySpecialtyRules(none), AcademySpecialtyRules{});
}

TEST(AcademySpecialtyRules, MalformedRowsAreRejected)
{
	const auto rejects = [](const char * child, const char * field, int value)
	{
		auto heroes = academyShippedHeroes();
		heroes["revisedSpecialties"][child][field].Integer() = value;
		EXPECT_THROW(academySpecialtyRules(heroes), std::runtime_error) << child << '.' << field;
	};
	rejects("hardened", "hitPermille", 0);
	rejects("hardened", "maximumTotalPermille", 751);
	rejects("spoilsOfAlchemy", "minimumGoldPerMercury", 3000);
	rejects("spoilsOfAlchemy", "maximumMercury", 0);
	rejects("practiceMakesPerfect", "capPercentPerLevel", 0);
	rejects("wellspringOfWealth", "maximumGoldPerManaPermille", 1000);
	rejects("stormLord", "chainLightningLevel", 0);
	rejects("serpentsEmbraceBound", "maximumDamagePercent", 1);
	rejects("goldStandardDiscount", "maximumDiscountPercent", 91);
	rejects("inexhaustibleKnowledge", "manaPermille", 0);
	rejects("inexhaustibleKnowledge", "minimumMana", -1);
	rejects("focusMagicSpecialty", "rangedPermillePerLevel", -1);
	rejects("sageOfTheAges", "spellPowerPerLevel", 11);
	auto extra = academyShippedHeroes();
	extra["revisedSpecialties"]["stormLord"]["unknown"].Integer() = 1;
	EXPECT_THROW(academySpecialtyRules(extra), std::runtime_error);
	auto unknownRow = academyShippedHeroes();
	unknownRow["revisedSpecialties"]["prodigy"]["percent"].Integer() = 1;
	auto disabledSwap = academyShippedHeroes();
	disabledSwap["revisedSpecialties"]["halonNullifyingShot"]["enabled"].Bool() = false;
	EXPECT_THROW(academySpecialtyRules(disabledSwap), std::runtime_error) << "Absence, not false, keeps the old holder";
	EXPECT_THROW(validateRevisedSpecialtiesSerialization(unknownRow), std::runtime_error);
}

TEST(AcademySpecialtyRules, RetiredSpellweaverIsNeverShippedButStillLoadsFromACapture)
{
	auto heroes = academyShippedHeroes();
	for(const auto & [row, value] : heroes["revisedSpecialties"].Struct())
		EXPECT_FALSE(isRetiredRevisedSpecialtyRow(row)) << row << " is retired and must not reach a new capture";
	EXPECT_TRUE(isRetiredRevisedSpecialtyRow("spellweaver"));
	EXPECT_FALSE(academySpecialtyRules(heroes).spellweaver);
	// A capture that holds the retired row still loads it.
	auto captured = heroes;
	captured["revisedSpecialties"].Struct().erase("halonNullifyingShot");
	captured["revisedSpecialties"]["spellweaver"]["additionalUses"].Integer() = 1;
	captured["revisedSpecialties"]["spellweaver"]["increasedAdditionalUses"].Integer() = 2;
	captured["revisedSpecialties"]["spellweaver"]["increasedFromLevel"].Integer() = 15;
	EXPECT_NO_THROW(validateRevisedSpecialtiesSerialization(captured));
	const auto rules = academySpecialtyRules(captured);
	ASSERT_TRUE(rules.spellweaver);
	EXPECT_EQ(rules.spellweaver->increasedFromLevel, 15);
}

using AcademyStartingProfiles = vcmiTestData::OriginalDataTest;

TEST_F(AcademyStartingProfiles, TheodorusStartsWithManaSiphonAndSolmyrWithLightningBolt)
{
	if(!HeroTypeID::decode("core:theodorus"))
		GTEST_SKIP() << "Requires New Horizons content";
	std::set<std::string> theodorus;
	for(const auto & perk : projectStartingHero(academyHero("core:theodorus"), academyStartContext()).perks)
		theodorus.insert(perk.perkId);
	EXPECT_EQ(theodorus, std::set<std::string>{"new-horizons:metamagic.manaSiphon"});
	std::set<std::string> iona;
	for(const auto & perk : projectStartingHero(academyHero("core:iona"), academyStartContext()).perks)
		iona.insert(perk.perkId);
	EXPECT_EQ(iona, std::set<std::string>{"new-horizons:learning.scholar"}) << "Iona, listed first, keeps her start";
	const auto solmyr = projectStartingHero(academyHero("core:solmyr"), academyStartContext());
	EXPECT_TRUE(solmyr.spells.contains(SpellID(SpellID::LIGHTNING_BOLT)));
	EXPECT_FALSE(solmyr.spells.contains(SpellID(SpellID::decode("new-horizons:masterChainLightning"))))
		<< "Master Chain Lightning is retired";
}

TEST_F(AcademyStartingProfiles, BetaOneCaptureKeepsMasterChainLightning)
{
	if(!HeroTypeID::decode("core:solmyr"))
		GTEST_SKIP() << "Requires New Horizons content";
	auto rules = academyShippedHeroes();
	rules["startingSkills"]["startingBookReplacements"].Struct().erase("core:solmyr");
	const auto solmyr = projectStartingHero(academyHero("core:solmyr"), academyStartContext(rules));
	EXPECT_TRUE(solmyr.spells.contains(SpellID(SpellID::decode("new-horizons:masterChainLightning"))));
	EXPECT_FALSE(solmyr.spells.contains(SpellID(SpellID::LIGHTNING_BOLT)));
	// The 24-row Castle table and the 22-row historical table stay valid; half a revision does not.
	EXPECT_NO_THROW(validateStartingBookSerialization(rules, true));
	rules["startingSkills"]["startingBookReplacements"].Struct().erase("core:loynis");
	EXPECT_THROW(validateStartingBookSerialization(rules, true), std::runtime_error);
	rules["startingSkills"]["startingBookReplacements"].Struct().erase("core:cuthbert");
	EXPECT_NO_THROW(validateStartingBookSerialization(rules, true)) << "The historical 22 rows";
}

TEST_F(AcademyStartingProfiles, NoTwoAcademyHeroesShareBothSkillsAndThePerk)
{
	const auto rules = academyShippedHeroes();
	const auto & starts = rules["startingSkills"];
	std::map<std::pair<std::set<std::string>, std::string>, std::string> seen;
	for(const auto & [identity, profile] : starts["startingDevelopmentProfiles"].Struct())
	{
		const auto * prototype = HeroTypeID(HeroTypeID::decode(identity)).toHeroType();
		if(!prototype || prototype->special || prototype->heroClass->faction != FactionID::TOWER)
			continue;
		std::set<std::string> skills;
		for(const auto & row : profile["skills"].Vector())
			skills.insert(row["skill"].String());
		const auto & starting = starts["startingPerkProfiles"][identity].Vector();
		const auto key = std::pair(skills, starting.empty() ? std::string() : starting.front()["perk"].String());
		const auto [existing, inserted] = seen.emplace(key, identity);
		EXPECT_TRUE(inserted) << identity << " repeats " << existing->second;
	}
	EXPECT_EQ(seen.size(), 16u);
}

TEST_F(AcademyStartingProfiles, RevisedSpecialtiesPresentTheirNameLevelValuesAndArtwork)
{
	if(!HeroTypeID::decode("core:neela"))
		GTEST_SKIP() << "Requires New Horizons content";
	const auto rules = academyShippedHeroes();
	const JsonNode magic(JsonPath::builtin("config/newHorizonsMagic"));
	const std::vector<std::tuple<const char *, const char *, const char *>> expected{
		{"core:neela", "Hardened", "10% at hero level 1, +0.5% per further level, up to 20%"},
		{"core:rissa", "Spoils of Alchemy", "every 1950 Gold"},
		{"core:astral", "Practice Makes Perfect", "capped at 2% per hero level"},
		{"core:aine", "Wellspring of Wealth", "x 1.575"},
		{"core:solmyr", "Storm Lord", "3% at hero level 1, +3% per further level, up to 60%"},
		{"core:fafner", "Serpent’s Embrace", "2% at hero level 1, +2% per further level, up to 40%"},
		{"core:josephine", "Gold Standard", "10% at hero level 1, +1% per further level, up to 30%"},
		{"core:halon", "Nullifying Shot", "a 20% chance to dispel"},
		{"core:theodorus", "Inexhaustible Knowledge", "4% at hero level 1, +0.2% per further level, up to 20%"},
		{"core:cyra", "Focus Magic", "10 additional percentage points"},
		{"core:thane", "Sage of the Ages", "+2 Spell Power and +2 Knowledge"}};
	for(const auto & [key, name, scaling] : expected)
	{
		const auto presentation = resolveSpecialtyPresentation(academyHero(key), rules, magic, std::nullopt);
		EXPECT_EQ(presentation.name, name) << key;
		EXPECT_NE(presentation.description.find(scaling), std::string::npos) << key << ": " << presentation.description;
		EXPECT_TRUE(presentation.artwork.has_value() && !presentation.artwork->empty()) << key;
	}
}
