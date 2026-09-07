/*
 * NewHorizonsCreatureCategoryRulesTest.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
#include "StdInc.h"
#include "../../lib/entities/creature/NewHorizonsCreatureCategoryRules.h"
#include "../../lib/serializer/CMemorySerializer.h"
#include <stdexcept>

using namespace newHorizonsCreatures;

namespace
{
JsonNode categoryFixture()
{
	// Structural fixture only, NOT an approved canonical roster or active config.
	JsonNode rules;
	rules["schemaVersion"].Integer() = 1;
	rules["rulesetVersion"].Integer() = CREATURE_CATEGORY_RULESET_VERSION;
	rules["sourceRulesetId"].String() = "new-horizons:creatureCategories";
	for(const std::string name : {"core", "elite", "champion"})
	{
		rules["categories"][name]["nameTextId"].String() = "new-horizons.category." + name + ".name";
		rules["categories"][name]["descriptionTextId"].String() = "new-horizons.category." + name + ".description";
	}
	rules["creatures"]["core:pixie"].String() = "core";
	rules["creatures"]["core:airElemental"].String() = "elite";
	rules["creatures"]["core:phoenix"].String() = "champion";
	return rules;
}
}

TEST(NewHorizonsCreatureCategoryRulesTest, ExplicitRowsReturnCopiedNamedViews)
{
	const CreatureCategoryRules rules(categoryFixture());
	const auto core = rules.lookup("core:pixie");
	ASSERT_TRUE(core);
	EXPECT_EQ(core->category, CreatureCategory::CORE);
	EXPECT_EQ(core->nameTextId, "new-horizons.category.core.name");
	EXPECT_EQ(core->descriptionTextId, "new-horizons.category.core.description");
	EXPECT_EQ(core->sourceRulesetId, "new-horizons:creatureCategories");
	EXPECT_EQ(core->rulesetVersion, CREATURE_CATEGORY_RULESET_VERSION);
	ASSERT_TRUE(rules.lookup("core:airElemental"));
	EXPECT_EQ(rules.lookup("core:airElemental")->category, CreatureCategory::ELITE);
	ASSERT_TRUE(rules.lookup("core:phoenix"));
	EXPECT_EQ(rules.lookup("core:phoenix")->category, CreatureCategory::CHAMPION);
}

TEST(NewHorizonsCreatureCategoryRulesTest, AbsentAndUnmappedContextsNeverInventCategories)
{
	EXPECT_FALSE(CreatureCategoryRules().lookup("core:pixie"));
	EXPECT_FALSE(CreatureCategoryRules(JsonNode()).lookup("core:pixie"));
	JsonNode emptyObject;
	emptyObject.Struct();
	EXPECT_FALSE(CreatureCategoryRules(emptyObject).lookup("core:pixie"));
	const CreatureCategoryRules rules(categoryFixture());
	EXPECT_FALSE(rules.lookup("core:sprite")) << "An upgrade needs its own explicit assignment";
	EXPECT_FALSE(rules.lookup("core:firebird"));
	EXPECT_FALSE(rules.lookup("core:ballista"));
	EXPECT_FALSE(rules.lookup("pixie")) << "No implicit scope resolution";
	EXPECT_FALSE(rules.lookup(""));
}

TEST(NewHorizonsCreatureCategoryRulesTest, IndependentSnapshotsDoNotSupplyAnAbsentContextFallback)
{
	const CreatureCategoryRules world(categoryFixture());
	auto different = categoryFixture();
	different["creatures"]["core:pixie"].String() = "elite";
	const CreatureCategoryRules battle(different);
	const CreatureCategoryRules oldBattle;
	EXPECT_EQ(world.lookup("core:pixie")->category, CreatureCategory::CORE);
	EXPECT_EQ(battle.lookup("core:pixie")->category, CreatureCategory::ELITE);
	EXPECT_FALSE(oldBattle.lookup("core:pixie"));
	// This is primitive-level isolation, NOT proof of future callback/save wiring.
}

TEST(NewHorizonsCreatureCategoryRulesTest, CaptureAndReturnedViewDoNotAdoptLaterMutations)
{
	auto source = categoryFixture();
	const CreatureCategoryRules captured(source);
	source["creatures"]["core:pixie"].String() = "champion";
	source["categories"]["core"]["nameTextId"].String() = "changed";
	auto view = captured.lookup("core:pixie");
	ASSERT_TRUE(view);
	view->nameTextId = "also changed";
	EXPECT_EQ(captured.lookup("core:pixie")->category, CreatureCategory::CORE);
	EXPECT_EQ(captured.lookup("core:pixie")->nameTextId, "new-horizons.category.core.name");
	EXPECT_EQ(captured.getRules()["creatures"]["core:pixie"].String(), "core");
}

TEST(NewHorizonsCreatureCategoryRulesTest, RejectsUnknownFieldsAndNumericTierAssignments)
{
	auto rules = categoryFixture();
	rules["legacyTier"].Integer() = 2;
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	rules = categoryFixture();
	rules["creatures"]["core:pixie"].Integer() = 2;
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	rules = categoryFixture();
	rules["creatures"]["core:pixie"].String() = "tier2";
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
}

TEST(NewHorizonsCreatureCategoryRulesTest, RejectsMissingDefinitionsTextAndUnqualifiedIdentifiers)
{
	auto rules = categoryFixture();
	rules["categories"].Struct().erase("champion");
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	rules = categoryFixture();
	rules["categories"]["core"]["nameTextId"].String().clear();
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	rules = categoryFixture();
	rules["creatures"]["pixie"].String() = "core";
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	rules = categoryFixture();
	rules["creatures"].Struct().clear();
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
}

TEST(NewHorizonsCreatureCategoryRulesTest, RejectsUnsupportedVersionsAndForeignRulesetIdentity)
{
	auto rules = categoryFixture();
	rules["schemaVersion"].Integer() = 2;
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	rules = categoryFixture();
	rules["rulesetVersion"].Float() = 1.5;
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	rules = categoryFixture();
	rules["sourceRulesetId"].String() = "foreign:creatureCategories";
	EXPECT_THROW(CreatureCategoryRules{rules}, std::runtime_error);
	JsonNode wrongShape;
	wrongShape.Bool() = false;
	EXPECT_THROW(CreatureCategoryRules{wrongShape}, std::runtime_error);
}

TEST(NewHorizonsCreatureCategoryRulesTest, BinarySnapshotRoundTripRebuildsCopiedViews)
{
	CreatureCategoryRules source(categoryFixture());
	CMemorySerializer wire;
	wire.oser & source;
	CreatureCategoryRules restored;
	wire.iser & restored;
	EXPECT_EQ(restored.getRules(), source.getRules());
	EXPECT_EQ(restored.lookup("core:pixie"), source.lookup("core:pixie"));
	EXPECT_FALSE(restored.lookup("core:sprite"));
}

TEST(NewHorizonsCreatureCategoryRulesTest, InvalidBinarySnapshotDoesNotReplacePreviousRules)
{
	auto invalid = categoryFixture();
	invalid["rulesetVersion"].Integer() = 2;
	CMemorySerializer wire;
	wire.oser & invalid; // Exact single-JsonNode wire shape of the rules object.
	CreatureCategoryRules restored(categoryFixture());
	const auto original = restored.getRules();
	EXPECT_THROW(wire.iser & restored, std::runtime_error);
	EXPECT_EQ(restored.getRules(), original);
	ASSERT_TRUE(restored.lookup("core:pixie"));
	EXPECT_EQ(restored.lookup("core:pixie")->category, CreatureCategory::CORE);
}
