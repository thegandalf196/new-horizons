/*
 * GeneratedImageRecipesTest.cpp, part of VCMI engine
 * License: GNU General Public License v2.0 or later; see license.txt
 */
#include "StdInc.h"
#include "../../lib/filesystem/GeneratedImageRecipes.h"
#include "../../lib/filesystem/Filesystem.h"
#include "../../lib/GameLibrary.h"
#include "../../lib/GameConstants.h"
#include "../../lib/constants/StringConstants.h"
#include "../../lib/modding/CModHandler.h"
#include "../../lib/json/JsonValidator.h"

namespace
{
constexpr std::string_view mage = "NH_academy_mageHolding_icon_large.png";
std::set<ResourcePath> physicalInputs(const generatedImageRecipes::CreaturePortrait & recipe,
	const std::string & backdropPrefix = "Sprites/")
{
	return {ResourcePath("Sprites/" + std::string(recipe.source), recipe.sourceType),
		ResourcePath(backdropPrefix + std::string(recipe.backdrop), EResType::IMAGE)};
}
bool available(std::string_view name, const std::set<ResourcePath> & inputs)
{
	return generatedImageRecipes::hasPhysicalDependencies(name,
		[&](const ResourcePath & resource) { return inputs.contains(resource); });
}
std::string validateImage(const std::string & name, const std::string & scope)
{
	JsonNode schema;
	schema["type"].String() = "string";
	schema["format"].String() = "imageFile";
	JsonNode value(name);
	value.setModScope(scope);
	return JsonValidator().check(schema, value);
}
}

TEST(GeneratedImageRecipesTest, ExactlyThreeTypedPhysicalRecipes)
{
	ASSERT_EQ(generatedImageRecipes::creaturePortraits().size(), 3u);
	std::set<std::string> outputs;
	for(const auto & recipe : generatedImageRecipes::creaturePortraits())
	{
		EXPECT_TRUE(outputs.insert(std::string(recipe.output)).second);
		EXPECT_EQ(generatedImageRecipes::findCreaturePortrait(recipe.output), &recipe);
		EXPECT_TRUE(available(recipe.output, physicalInputs(recipe)));
	}
	EXPECT_EQ(generatedImageRecipes::findCreaturePortrait(mage)->sourceType, EResType::IMAGE);
	EXPECT_EQ(generatedImageRecipes::findCreaturePortrait("NH_academy_stoneGargoyle_icon_large.png")->sourceType,
		EResType::ANIMATION);
}

TEST(GeneratedImageRecipesTest, ResourceIdentityMatchesCaseButRejectsUnknownAndTypos)
{
	EXPECT_EQ(generatedImageRecipes::findCreaturePortrait("nh_ACADEMY_mageholding_ICON_large.PNG"),
		generatedImageRecipes::findCreaturePortrait(mage));
	int requests = 0;
	for(const auto * unknown : {"NH_academy_mageHolding_icon_lagre.png", "NH_arbitrary_icon_large.png",
		"Elsewhere/NH_academy_mageHolding_icon_large.png"})
	{
		EXPECT_FALSE(generatedImageRecipes::hasPhysicalDependencies(unknown, [&](const ResourcePath &)
			{ ++requests; return true; }));
	}
	EXPECT_EQ(requests, 0);
}

TEST(GeneratedImageRecipesTest, MissingSourceOrBackdropNeverAdmitsGeneratedOutput)
{
	for(const auto & recipe : generatedImageRecipes::creaturePortraits())
	{
		auto inputs = physicalInputs(recipe);
		inputs.erase(ResourcePath("Sprites/" + std::string(recipe.source), recipe.sourceType));
		EXPECT_FALSE(available(recipe.output, inputs));
		inputs = physicalInputs(recipe);
		inputs.erase(ResourcePath("Sprites/" + std::string(recipe.backdrop), EResType::IMAGE));
		// A generated-output entry is not a substitute for either physical input.
		inputs.insert(ResourcePath("Sprites/" + std::string(recipe.output), EResType::IMAGE));
		EXPECT_FALSE(available(recipe.output, inputs));
	}
}

TEST(GeneratedImageRecipesTest, WrongResourceTypesAndAnimationAliasesAreNotPhysicalSources)
{
	for(const auto & recipe : generatedImageRecipes::creaturePortraits())
	{
		auto inputs = physicalInputs(recipe);
		inputs.erase(ResourcePath("Sprites/" + std::string(recipe.source), recipe.sourceType));
		inputs.insert(ResourcePath("Sprites/" + std::string(recipe.source), EResType::JSON));
		EXPECT_FALSE(available(recipe.output, inputs));
		inputs = physicalInputs(recipe);
		inputs.erase(ResourcePath("Sprites/" + std::string(recipe.backdrop), EResType::IMAGE));
		inputs.insert(ResourcePath("Sprites/" + std::string(recipe.backdrop), EResType::ANIMATION));
		EXPECT_FALSE(available(recipe.output, inputs));
	}
}

TEST(GeneratedImageRecipesTest, BackdropAliasesMatchExistingCompositor)
{
	const auto * recipe = generatedImageRecipes::findCreaturePortrait(mage);
	ASSERT_NE(recipe, nullptr);
	for(const auto * prefix : {"Sprites/", "Data/", ""})
		EXPECT_TRUE(available(mage, physicalInputs(*recipe, prefix)));
}

TEST(GeneratedImageRecipesTest, CallerScopePolicyIsAppliedToEveryPhysicalDependency)
{
	const auto * recipe = generatedImageRecipes::findCreaturePortrait(mage);
	ASSERT_NE(recipe, nullptr);
	const auto allowed = physicalInputs(*recipe);
	std::vector<ResourcePath> requests;
	EXPECT_FALSE(generatedImageRecipes::hasPhysicalDependencies(mage, [&](const ResourcePath & resource)
	{
		requests.push_back(resource);
		// An unrelated scope cannot access the selected Mage frame.
		return resource.getType() == EResType::IMAGE && resource.getName().find("BACKDROP") != std::string::npos;
	}));
	ASSERT_EQ(requests.size(), 1u);
	EXPECT_EQ(requests.front(), ResourcePath("Sprites/" + std::string(recipe->source), EResType::IMAGE));
	EXPECT_TRUE(available(mage, allowed));
}

TEST(GeneratedImageRecipesTest, ActualImageValidatorAcceptsKnownCuratedPhysicalRecipes)
{
	ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	for(const auto & recipe : generatedImageRecipes::creaturePortraits())
	{
		// Headless filesystem validation only; this is not a DEF frame decode/render gate.
		ASSERT_TRUE(generatedImageRecipes::hasPhysicalDependencies(recipe.output,
			[](const ResourcePath & resource) { return CResourceHandler::get()->existsResource(resource); }));
		EXPECT_TRUE(validateImage(std::string(recipe.output), GameConstants::NEW_HORIZONS_MOD_SCOPE).empty())
			<< recipe.output;
	}
}

TEST(GeneratedImageRecipesTest, ActualImageValidatorStillRejectsTyposAndUnrelatedScope)
{
	ASSERT_TRUE(vstd::contains(LIBRARY->modh->getActiveMods(), GameConstants::NEW_HORIZONS_MOD_SCOPE));
	EXPECT_FALSE(validateImage("NH_academy_mageHolding_icon_lagre.png", GameConstants::NEW_HORIZONS_MOD_SCOPE).empty());
	EXPECT_FALSE(validateImage("NH_arbitrary_generated.png", GameConstants::NEW_HORIZONS_MOD_SCOPE).empty());
	EXPECT_FALSE(validateImage(std::string(mage), "not-installed-recipe-test").empty());
}
