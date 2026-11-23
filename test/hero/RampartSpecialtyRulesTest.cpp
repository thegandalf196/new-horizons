/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsRampartSpecialties.h"
#include "../../lib/entities/hero/NewHorizonsRevisedSpecialties.h"
#include "../../lib/json/JsonNode.h"
#include "../../lib/filesystem/ResourcePath.h"

namespace
{
using namespace newHorizonsHeroes;

JsonNode shippedHeroes()
{
	return JsonNode(JsonPath::builtin("config/newHorizonsHeroes"));
}

constexpr std::array<const char *, 11> RAMPART_ROWS{"useTheGround", "elusive", "patroness", "giantSlayer",
	"livingRemedy", "sorceressOfEnroth", "brittle", "againstTheOdds", "perfectMount", "treantmoot", "forestAmbush"};
}

TEST(RampartSpecialtyRules, ShippedTableCapturesEveryRow)
{
	const auto rules = rampartSpecialtyRules(shippedHeroes());
	ASSERT_TRUE(rules.useTheGroundPermille && rules.elusive && rules.patronessPermille && rules.giantSlayerPermillePerStep
		&& rules.livingRemedy && rules.sorceressOfEnroth && rules.brittlePermille && rules.againstTheOdds
		&& rules.perfectMountPercent && rules.treantmootPercent && rules.forestAmbushShotPercent);
	EXPECT_EQ(rules.elusive->eliteLevel, 10);
	EXPECT_EQ(rules.elusive->championLevel, 20);
	EXPECT_EQ(rules.livingRemedy->rounds, 2);
}

TEST(RampartSpecialtyRules, LevelScalingFollowsTheTable)
{
	const auto rules = rampartSpecialtyRules(shippedHeroes());
	// Mephala: 5%, +0.5%/level, max 15% (permille).
	EXPECT_EQ(rules.useTheGroundPermille->at(1), 50);
	EXPECT_EQ(rules.useTheGroundPermille->at(2), 55);
	EXPECT_EQ(rules.useTheGroundPermille->at(21), 150);
	EXPECT_EQ(rules.useTheGroundPermille->at(40), 150);
	// Jenova: 25%, +2.5%/level, max 100%.
	EXPECT_EQ(rules.patronessPermille->at(1), 250);
	EXPECT_EQ(rules.patronessPermille->at(30), 975);
	EXPECT_EQ(rules.patronessPermille->at(31), 1000);
	// Coronius: +10% per step, +0.5%/level, max +20% per step.
	EXPECT_EQ(rules.giantSlayerPermillePerStep->at(1), 100);
	EXPECT_EQ(rules.giantSlayerPermillePerStep->at(11), 150);
	EXPECT_EQ(rules.giantSlayerPermillePerStep->at(25), 200);
	// Uland: 2% of the stack's Health, +0.25%/level, max 7% (basis points).
	EXPECT_EQ(rules.livingRemedy->healingBasisPoints.at(1), 200);
	EXPECT_EQ(rules.livingRemedy->healingBasisPoints.at(5), 300);
	EXPECT_EQ(rules.livingRemedy->healingBasisPoints.at(21), 700);
	// Alagar: 5%, +0.5%/level, max 15%.
	EXPECT_EQ(rules.brittlePermille->at(1), 50);
	EXPECT_EQ(rules.brittlePermille->at(21), 150);
	// Aeris: 43%, +3%/level, 100% (immunity) at level 20.
	EXPECT_EQ(rules.perfectMountPercent->at(1), 43);
	EXPECT_EQ(rules.perfectMountPercent->at(19), 97);
	EXPECT_EQ(rules.perfectMountPercent->at(20), 100);
	// Ryland: 10%, +1%/level, max 30%.
	EXPECT_EQ(rules.treantmootPercent->at(1), 10);
	EXPECT_EQ(rules.treantmootPercent->at(21), 30);
	EXPECT_EQ(rules.treantmootPercent->at(30), 30);
	// Ivor: shot from hiding +2% damage per level.
	EXPECT_EQ(rules.forestAmbushShotPercent->at(1), 2);
	EXPECT_EQ(rules.forestAmbushShotPercent->at(15), 30);
	EXPECT_EQ(rules.forestAmbushShotPercent->at(60), 100);
}

TEST(RampartSpecialtyRules, StepRulesFollowHeroLevel)
{
	const auto rules = rampartSpecialtyRules(shippedHeroes());
	// Gem: free spell level 1 + floor(level / 5): level 2 spells at 5, level 5 at 20.
	const auto & sorceress = *rules.sorceressOfEnroth;
	const auto spellLevel = [&sorceress](int level)
	{ return std::min(5, sorceress.baseSpellLevel + level / sorceress.heroLevelsPerSpellLevel); };
	EXPECT_EQ(spellLevel(1), 1);
	EXPECT_EQ(spellLevel(4), 1);
	EXPECT_EQ(spellLevel(5), 2);
	EXPECT_EQ(spellLevel(20), 5);
	EXPECT_EQ(spellLevel(40), 5);
	// Melodia: +1 Luck, +1 per 10 levels, max +3.
	const auto & odds = *rules.againstTheOdds;
	const auto luck = [&odds](int level) { return std::min(odds.maximumLuck, odds.luck + level / odds.levelsPerLuck); };
	EXPECT_EQ(luck(1), 1);
	EXPECT_EQ(luck(9), 1);
	EXPECT_EQ(luck(10), 2);
	EXPECT_EQ(luck(20), 3);
	EXPECT_EQ(luck(35), 3);
}

TEST(RampartSpecialtyRules, HistoricalCaptureKeepsEveryOldSpecialty)
{
	auto heroes = shippedHeroes();
	heroes.Struct().erase("revisedSpecialties");
	EXPECT_EQ(rampartSpecialtyRules(heroes), RampartSpecialtyRules{});
	// A 0.93.1 capture (format 1166 without the Rampart rows) keeps them too.
	auto castleOnly = shippedHeroes();
	for(const auto * key : RAMPART_ROWS)
		castleOnly["revisedSpecialties"].Struct().erase(key);
	EXPECT_EQ(rampartSpecialtyRules(castleOnly), RampartSpecialtyRules{});
	EXPECT_NO_THROW(validateRevisedSpecialtiesSerialization(castleOnly));
}

TEST(RampartSpecialtyRules, MalformedRowsAreRejected)
{
	const auto rejects = [](const char * child, const char * field, int value)
	{
		auto heroes = shippedHeroes();
		heroes["revisedSpecialties"][child][field].Integer() = value;
		EXPECT_THROW(rampartSpecialtyRules(heroes), std::runtime_error) << child << '.' << field;
	};
	rejects("useTheGround", "maximumReductionPermille", 751);
	rejects("useTheGround", "maximumReductionPermille", 40);
	rejects("elusive", "championLevel", 5);
	rejects("patroness", "incomePermille", 0);
	rejects("giantSlayer", "maximumDamagePermillePerStep", 1001);
	rejects("livingRemedy", "rounds", 0);
	rejects("sorceressOfEnroth", "baseSpellLevel", 6);
	rejects("brittle", "damagePermillePerLevel", -1);
	rejects("againstTheOdds", "maximumLuck", 0);
	rejects("perfectMount", "maximumReductionPercent", 101);
	rejects("treantmoot", "maximumDendroidReductionPercent", 76);
	rejects("forestAmbush", "shotDamagePercentPerLevel", 0);
	auto extra = shippedHeroes();
	extra["revisedSpecialties"]["patroness"]["unknown"].Integer() = 1;
	EXPECT_THROW(rampartSpecialtyRules(extra), std::runtime_error);
}
