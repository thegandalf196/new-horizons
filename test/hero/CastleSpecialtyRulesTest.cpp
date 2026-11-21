/* Part of VCMI / New Horizons. GPL-2.0-or-later; see license.txt. */
#include "StdInc.h"
#include "../../lib/entities/hero/NewHorizonsCastleSpecialties.h"
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
}

TEST(CastleSpecialtyRules, ShippedTableCapturesEveryCombatRow)
{
	const auto rules = castleSpecialtyRules(shippedHeroes());
	ASSERT_TRUE(rules.volleyCommand && rules.aerialSupremacy && rules.benedictionOfArms && rules.pariah
		&& rules.frostMantle && rules.quickStudy && rules.sermon);
	EXPECT_EQ(rules.pariah->morale, -2);
	EXPECT_EQ(rules.frostMantle->rounds, 2);
	EXPECT_EQ(rules.frostMantle->initiativePenaltyPercent, 20);
	EXPECT_EQ(rules.frostMantle->chillRounds, 1);
	EXPECT_EQ(rules.quickStudy->secondUseLevel, 10);
}

TEST(CastleSpecialtyRules, LevelScalingReachesTheTableCapsAtLevelTwenty)
{
	const auto rules = castleSpecialtyRules(shippedHeroes());
	// Orrin: 30% at level 1, +1% per further level, max 50%.
	EXPECT_EQ(rules.volleyCommand->percentAt(1), 30);
	EXPECT_EQ(rules.volleyCommand->percentAt(21), 50);
	EXPECT_EQ(rules.volleyCommand->percentAt(40), 50);
	// Edric: counter +0% at level 1, +2% per further level, max +40%.
	EXPECT_EQ(rules.aerialSupremacy->percentAt(1), 0);
	EXPECT_EQ(rules.aerialSupremacy->percentAt(20), 38);
	EXPECT_EQ(rules.aerialSupremacy->percentAt(30), 40);
	// Adela and Loynis: prayer potency +0% at level 1, +3% per further level, max +60%.
	EXPECT_EQ(rules.benedictionOfArms->percentAt(10), 27);
	EXPECT_EQ(rules.benedictionOfArms->percentAt(25), 60);
	EXPECT_EQ(rules.sermon->percentAt(1), 0);
	EXPECT_EQ(rules.sermon->percentAt(20), 57);
	// Cuthbert: x1.05 at level 1, x2 at level 20 and beyond.
	EXPECT_EQ(rules.pariah->shadowPowerBonus.percentAt(1), 5);
	EXPECT_EQ(rules.pariah->shadowPowerBonus.percentAt(20), 100);
	EXPECT_EQ(rules.pariah->shadowPowerBonus.percentAt(35), 100);
	// Adelaide: 10% at level 1, +1% per further level, max 40%.
	EXPECT_EQ(rules.frostMantle->returned.percentAt(1), 10);
	EXPECT_EQ(rules.frostMantle->returned.percentAt(31), 40);
	EXPECT_EQ(rules.frostMantle->returned.percentAt(50), 40);
	// Sanya: Spell Power 50% at level 1, +2.5% per further level, max 100%, in tenths of a percent.
	EXPECT_EQ(rules.quickStudy->spellPower.permilleAt(1), 500);
	EXPECT_EQ(rules.quickStudy->spellPower.permilleAt(9), 700);
	EXPECT_EQ(rules.quickStudy->spellPower.permilleAt(21), 1000);
	EXPECT_EQ(rules.quickStudy->spellPower.permilleAt(30), 1000);
}

TEST(CastleSpecialtyRules, TableValuesHoldAtLevelOneAndLevelTwenty)
{
	const auto rules = castleSpecialtyRules(shippedHeroes());
	// The listed value is the level 1 value: Orrin's volley 30%, Cuthbert x1.05.
	EXPECT_EQ(rules.volleyCommand->percentAt(1), 30);
	EXPECT_EQ(rules.volleyCommand->percentAt(20), 49);
	EXPECT_EQ(100 + rules.pariah->shadowPowerBonus.percentAt(1), 105);
	EXPECT_EQ(100 + rules.pariah->shadowPowerBonus.percentAt(20), 200) << "x2 at level 20: 5 + 5 x 19";
}

TEST(CastleSpecialtyRules, PotencyBoostScalesPotencyFaithOnly)
{
	EXPECT_EQ(boostedPotencyFaith(200, 0), 200);
	EXPECT_EQ(boostedPotencyFaith(200, 30), 260);
	EXPECT_EQ(boostedPotencyFaith(250, 60), 400);
	EXPECT_EQ(boostedPotencyFaith(-5, 60), 0);
}

TEST(CastleSpecialtyRules, HistoricalCaptureKeepsEveryOldSpecialty)
{
	auto heroes = shippedHeroes();
	heroes.Struct().erase("revisedSpecialties");
	EXPECT_EQ(castleSpecialtyRules(heroes), CastleSpecialtyRules{});
	EXPECT_FALSE(requiresRevisedSpecialtyFormat(heroes));
	// A 1166 capture without the Castle children (the first 0.93.1 step) also keeps them.
	auto partial = shippedHeroes();
	for(const auto * key : {"volleyCommand", "aerialSupremacy", "benedictionOfArms", "pariah", "frostMantle",
		"quickStudy", "sermon"})
		partial["revisedSpecialties"].Struct().erase(key);
	EXPECT_EQ(castleSpecialtyRules(partial), CastleSpecialtyRules{});
}

TEST(CastleSpecialtyRules, NewRowsRequireTheRevisedSpecialtyFormat)
{
	JsonNode document;
	document["heroes"]["newHorizons"] = shippedHeroes();
	EXPECT_TRUE(requiresRevisedSpecialtyFormat(document));
}

TEST(CastleSpecialtyRules, MalformedRowsAreRejected)
{
	const auto rejects = [](const char * child, const char * field, int value)
	{
		auto heroes = shippedHeroes();
		heroes["revisedSpecialties"][child][field].Integer() = value;
		EXPECT_THROW(castleSpecialtyRules(heroes), std::runtime_error) << child << '.' << field;
	};
	rejects("volleyCommand", "damagePercent", 101);
	rejects("aerialSupremacy", "counterDamagePercentPerLevel", 11);
	rejects("pariah", "morale", 1);
	rejects("frostMantle", "rounds", 0);
	rejects("quickStudy", "secondUseLevel", 0);
	rejects("sermon", "maximumPotencyPercent", 201);
	auto extra = shippedHeroes();
	extra["revisedSpecialties"]["benedictionOfArms"]["unknown"].Integer() = 1;
	EXPECT_THROW(castleSpecialtyRules(extra), std::runtime_error);
}
